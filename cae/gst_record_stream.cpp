// gst_record_stream.cpp
//
// GStreamer-based recording pipeline for caed(8)
//
// Records audio from a JACK ringbuffer (fed by the
// GstPipelineManager JACK process callback) through
// appsrc into a GStreamer encoding pipeline.
//
//   (C) Copyright 2025 Fred Gleason <fredg@paravelsystems.com>
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License version 2 as
//   published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public
//   License along with this program; if not, write to the Free Software
//   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <string.h>

#include <QFile>

#include <rdapplication.h>

#include "gst_record_stream.h"


GstRecordStream::GstRecordStream(int card,int port,
                                 jack_ringbuffer_t *ringbuffer,
                                 unsigned jack_rate,
                                 QObject *parent)
  :QObject(parent)
{
  d_card=card;
  d_port=port;
  d_ringbuffer=ringbuffer;
  d_jack_rate=jack_rate;
  d_state=Idle;
  d_pipeline=NULL;
  d_src=NULL;
  d_audioconvert=NULL;
  d_audioresample=NULL;
  d_capsfilter=NULL;
  d_level=NULL;
  d_encoder=NULL;
  d_filesink=NULL;
  d_sample_rate=44100;
  d_meter_levels[0]=0;
  d_meter_levels[1]=0;
  d_coding=0;
  d_channels=0;
  d_bitrate=0;
  d_recorded_frames=0;
  d_temp_dir=NULL;

  d_bus_timer=new QTimer(this);
  d_bus_timer->setSingleShot(false);
  connect(d_bus_timer,SIGNAL(timeout()),
          this,SLOT(busTimerData()));

  d_stop_timer=new QTimer(this);
  d_stop_timer->setSingleShot(true);
  connect(d_stop_timer,SIGNAL(timeout()),
          this,SLOT(stopTimerData()));
}


GstRecordStream::~GstRecordStream()
{
  DestroyPipeline();
  delete d_temp_dir;
}


int GstRecordStream::card() const
{
  return d_card;
}


int GstRecordStream::port() const
{
  return d_port;
}


GstRecordStream::State GstRecordStream::state() const
{
  return d_state;
}


bool GstRecordStream::loadRecord(int coding,int chans,
                                 int samprate,int bitrate,
                                 const QString &filename)
{
  if(d_state!=Idle) {
    return false;
  }
  DestroyPipeline();
  d_sample_rate=(unsigned)samprate;
  if(!BuildPipeline(coding,chans,samprate,bitrate,
                    filename)) {
    return false;
  }
  d_state=Armed;
  d_bus_timer->start(10);
  rda->syslog(LOG_DEBUG,
              "GstRecordStream: loadRecord "
              "card=%d port=%d coding=%d "
              "chans=%d rate=%d bitrate=%d",
              d_card,d_port,coding,chans,
              samprate,bitrate);
  return true;
}


bool GstRecordStream::record(int length_ms,int threshold_cb)
{
  if(d_state!=Armed) {
    return false;
  }

  //
  // Flush any stale data from the ringbuffer
  //
  jack_ringbuffer_reset(d_ringbuffer);

  d_state=Recording;
  gst_element_set_state(d_pipeline,GST_STATE_PLAYING);

  if(length_ms>0) {
    d_stop_timer->start(length_ms);
  }

  //
  // Signal: Recording (state=0)
  //
  emit recordStateChanged(d_card,d_port,0);

  //
  // Signal: Record started (state=4)
  // For VOX, this would fire only when threshold is
  // exceeded.  Fire immediately here (no VOX logic).
  //
  (void)threshold_cb;
  emit recordStateChanged(d_card,d_port,4);

  rda->syslog(LOG_DEBUG,
              "GstRecordStream: record started "
              "card=%d port=%d length=%d",
              d_card,d_port,length_ms);
  return true;
}


bool GstRecordStream::stopRecord()
{
  if(d_state!=Recording) {
    return false;
  }
  d_state=Stopping;
  d_stop_timer->stop();

  //
  // Flush remaining ringbuffer data into appsrc
  //
  FeedFromRingbuffer();

  gst_app_src_end_of_stream(GST_APP_SRC(d_src));
  rda->syslog(LOG_DEBUG,
              "GstRecordStream: stopRecord "
              "card=%d port=%d",d_card,d_port);
  return true;
}


bool GstRecordStream::unloadRecord(unsigned *frames)
{
  if(d_state==Idle) {
    //
    // EOS already fired -- wrap temp audio into WAV+levl
    // and copy to destination (may block on S3 FUSE)
    //
    WrapAndCopy();
    if(frames) {
      *frames=d_recorded_frames;
    }
    rda->syslog(LOG_DEBUG,
                "GstRecordStream: unloadRecord "
                "(post-EOS) card=%d port=%d frames=%u",
                d_card,d_port,d_recorded_frames);
    d_recorded_frames=0;
    return true;
  }

  //
  // Query recorded duration via pipeline position
  //
  d_recorded_frames=0;
  if(d_pipeline!=NULL) {
    gint64 pos=0;
    if(gst_element_query_position(d_pipeline,
       GST_FORMAT_TIME,&pos)&&pos>0) {
      d_recorded_frames=
        (unsigned)((double)pos/(double)GST_SECOND*
                   (double)d_sample_rate);
    }
    DestroyPipeline();
  }
  WrapAndCopy();
  if(frames) {
    *frames=d_recorded_frames;
  }
  rda->syslog(LOG_DEBUG,
              "GstRecordStream: unloadRecord "
              "card=%d port=%d frames=%u",
              d_card,d_port,d_recorded_frames);
  d_recorded_frames=0;
  d_state=Idle;
  return true;
}


bool GstRecordStream::getInputMeters(short levels[2])
{
  if(d_state!=Recording) {
    return false;
  }
  levels[0]=d_meter_levels[0];
  levels[1]=d_meter_levels[1];
  return true;
}


void GstRecordStream::busTimerData()
{
  if(d_pipeline==NULL) {
    return;
  }

  //
  // Feed audio from JACK ringbuffer into appsrc
  //
  if(d_state==Recording) {
    FeedFromRingbuffer();
  }

  GstBus *bus=gst_element_get_bus(d_pipeline);
  GstMessage *msg=NULL;
  while((msg=gst_bus_pop(bus))!=NULL) {
    switch(GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      d_stop_timer->stop();
      d_bus_timer->stop();
      if(d_state==Stopping||d_state==Recording) {
        //
        // Capture frame count before destroying pipeline
        //
        d_recorded_frames=0;
        if(d_pipeline!=NULL) {
          gint64 pos=0;
          if(gst_element_query_position(d_pipeline,
             GST_FORMAT_TIME,&pos)&&pos>0) {
            d_recorded_frames=
              (unsigned)((double)pos/
                         (double)GST_SECOND*
                         (double)d_sample_rate);
          }
        }
        rda->syslog(LOG_DEBUG,
          "GstRecordStream: EOS card=%d port=%d "
          "frames=%u",
          d_card,d_port,d_recorded_frames);
        DestroyPipeline();
        d_state=Idle;
        emit recordStateChanged(d_card,d_port,3);
      }
      break;

    case GST_MESSAGE_ERROR: {
      GError *err=NULL;
      gchar *dbg=NULL;
      gst_message_parse_error(msg,&err,&dbg);
      rda->syslog(LOG_WARNING,
                  "GstRecordStream error "
                  "card=%d port=%d: %s",
                  d_card,d_port,err->message);
      g_error_free(err);
      g_free(dbg);
      d_stop_timer->stop();
      d_bus_timer->stop();
      DestroyPipeline();
      delete d_temp_dir;
      d_temp_dir=NULL;
      d_temp_raw_path.clear();
      d_state=Idle;
      emit recordStateChanged(d_card,d_port,3);
      break;
    }

    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s=
        gst_message_get_structure(msg);
      if(s&&gst_structure_has_name(s,"level")) {
        const GValue *peak=
          gst_structure_get_value(s,"peak");
        if(peak&&GST_VALUE_HOLDS_LIST(peak)) {
          unsigned nch=gst_value_list_get_size(peak);
          for(unsigned i=0;i<nch&&i<2;i++) {
            const GValue *v=
              gst_value_list_get_value(peak,i);
            double db=g_value_get_double(v);
            if(db<-100.0) {
              d_meter_levels[i]=-10000;
            }
            else {
              d_meter_levels[i]=(short)(db*100.0);
            }
          }
          if(nch==1) {
            d_meter_levels[1]=d_meter_levels[0];
          }
        }
      }
      break;
    }

    default:
      break;
    }
    gst_message_unref(msg);
  }
  gst_object_unref(bus);
}


void GstRecordStream::stopTimerData()
{
  stopRecord();
}


//
// Push available data from the JACK ringbuffer into appsrc.
// Called from bus timer (10ms interval) during recording.
//
void GstRecordStream::FeedFromRingbuffer()
{
  if(d_src==NULL||d_ringbuffer==NULL) {
    return;
  }
  size_t avail=jack_ringbuffer_read_space(d_ringbuffer);
  if(avail==0) {
    return;
  }

  GstBuffer *buf=gst_buffer_new_allocate(NULL,avail,NULL);
  GstMapInfo map;
  if(gst_buffer_map(buf,&map,GST_MAP_WRITE)) {
    jack_ringbuffer_read(d_ringbuffer,
                         (char *)map.data,avail);
    gst_buffer_unmap(buf,&map);
    gst_app_src_push_buffer(GST_APP_SRC(d_src),buf);
  }
  else {
    gst_buffer_unref(buf);
  }
}


bool GstRecordStream::BuildPipeline(int coding,int chans,
                                    int samprate,int bitrate,
                                    const QString &filename)
{
  d_coding=coding;
  d_channels=chans;
  d_bitrate=bitrate;
  d_dest_filename=filename;

  const char *enc_name=EncoderName(coding);
  bool need_encoder=(coding==1)||(coding==2)||(coding==3);

  d_pipeline=gst_pipeline_new(NULL);
  d_src=gst_element_factory_make("appsrc",NULL);
  d_audioconvert=
    gst_element_factory_make("audioconvert",NULL);
  d_audioresample=
    gst_element_factory_make("audioresample",NULL);
  d_capsfilter=
    gst_element_factory_make("capsfilter",NULL);
  d_level=gst_element_factory_make("level",NULL);
  d_encoder=need_encoder?gst_element_factory_make(enc_name,NULL):NULL;
  d_filesink=gst_element_factory_make("filesink",NULL);

  if(!d_pipeline||!d_src||!d_audioconvert||
     !d_audioresample||!d_capsfilter||!d_level||
     (need_encoder&&!d_encoder)||!d_filesink) {
    rda->syslog(LOG_ERR,
                "GstRecordStream: element creation "
                "failed (enc=%s) card=%d port=%d",
                need_encoder?enc_name:"none",d_card,d_port);
    DestroyPipeline();
    return false;
  }

  //
  // Configure appsrc: live, F32LE stereo at JACK rate
  //
  GstCaps *src_caps=gst_caps_new_simple("audio/x-raw",
    "format",G_TYPE_STRING,"F32LE",
    "channels",G_TYPE_INT,2,
    "rate",G_TYPE_INT,(int)d_jack_rate,
    "layout",G_TYPE_STRING,"interleaved",
    NULL);
  g_object_set(d_src,
               "caps",src_caps,
               "is-live",TRUE,
               "format",GST_FORMAT_TIME,
               "stream-type",
               GST_APP_STREAM_TYPE_STREAM,
               NULL);
  gst_caps_unref(src_caps);

  //
  // Capsfilter: desired recording channels/rate.  For PCM (no encoder
  // downstream), also force the sample format to match exactly what
  // RDWaveFile::writeWave()'s PCM branch expects byte-for-byte
  // (packed, little-endian, no padding); for MPEG codings the encoder
  // negotiates its own input format upstream, so format is left
  // unconstrained here.
  //
  GstCaps *rec_caps;
  if(coding==0||coding==4) {
    rec_caps=gst_caps_new_simple("audio/x-raw",
      "format",G_TYPE_STRING,(coding==4)?"S24LE":"S16LE",
      "channels",G_TYPE_INT,chans,
      "rate",G_TYPE_INT,samprate,
      "layout",G_TYPE_STRING,"interleaved",
      NULL);
  }
  else {
    rec_caps=gst_caps_new_simple("audio/x-raw",
      "channels",G_TYPE_INT,chans,
      "rate",G_TYPE_INT,samprate,
      NULL);
  }
  g_object_set(d_capsfilter,"caps",rec_caps,NULL);
  gst_caps_unref(rec_caps);

  g_object_set(d_level,
               "interval",(guint64)(50*GST_MSECOND),
               "peak-ttl",(guint64)(100*GST_MSECOND),
               "peak-falloff",10.0,
               NULL);

  //
  // Configure encoder (MP2/MP3 bitrate in kbps)
  //
  if(coding==1||coding==2) {
    g_object_set(d_encoder,"bitrate",
                 bitrate/1000,NULL);
    g_object_set(d_encoder,
                 "energy-level-extension",TRUE,NULL);
  }
  else if(coding==3) {
    g_object_set(d_encoder,"bitrate",
                 bitrate/1000,NULL);
  }

  //
  // Configure output destination.  Every coding that needs RDWaveFile
  // framing (PCM and MPEG L1/L2) records into a local RDTempDirectory
  // first -- RDWaveFile::closeWave() needs real read-write-seek access
  // to patch header sizes and append the levl/mext chunks, which an
  // audio store on a network or object-storage filesystem may not
  // provide.  The finished, fully-formed file is copied to its
  // final /var/snd destination only once, by WrapAndCopy(), after
  // closeWave() completes.
  //
  QString err_msg;
  d_temp_dir=new RDTempDirectory("rdrecord");
  if(!d_temp_dir->create(&err_msg)) {
    rda->syslog(LOG_ERR,
                "GstRecordStream: unable to create temp directory "
                "\"%s\": %s",
                d_temp_dir->path().toUtf8().constData(),
                err_msg.toUtf8().constData());
    delete d_temp_dir;
    d_temp_dir=NULL;
    DestroyPipeline();
    return false;
  }
  if(coding==1||coding==2) {
    d_temp_raw_path=d_temp_dir->path()+"/audio.mp2";
  }
  else {
    d_temp_raw_path=d_temp_dir->path()+"/audio.raw";
  }
  g_object_set(d_filesink,"location",
               d_temp_raw_path.toUtf8().constData(),
               NULL);

  //
  // PCM has no encoder element: capsfilter/level feed filesink
  // directly with raw audio/x-raw; RDWaveFile supplies the WAV
  // container instead of an encoder+muxer doing it.
  //
  bool linked=false;
  if(d_encoder!=NULL) {
    gst_bin_add_many(GST_BIN(d_pipeline),d_src,
      d_audioconvert,d_audioresample,d_capsfilter,
      d_level,d_encoder,d_filesink,NULL);
    linked=gst_element_link_many(d_src,d_audioconvert,
      d_audioresample,d_capsfilter,d_level,
      d_encoder,d_filesink,NULL);
  }
  else {
    gst_bin_add_many(GST_BIN(d_pipeline),d_src,
      d_audioconvert,d_audioresample,d_capsfilter,
      d_level,d_filesink,NULL);
    if(coding==4) {
      //
      // level takes no S24LE (packed 24-bit): it measures the F32LE input
      // instead, before the conversion to the recording's format.
      //
      linked=gst_element_link_many(d_src,d_level,d_audioconvert,
	d_audioresample,d_capsfilter,d_filesink,NULL);
    }
    else {
      linked=gst_element_link_many(d_src,d_audioconvert,
	d_audioresample,d_capsfilter,d_level,
	d_filesink,NULL);
    }
  }
  if(!linked) {
    rda->syslog(LOG_ERR,
                "GstRecordStream: pipeline link failed "
                "card=%d port=%d",d_card,d_port);
    DestroyPipeline();
    delete d_temp_dir;
    d_temp_dir=NULL;
    return false;
  }

  //
  // Set to READY (not PAUSED — appsrc is live, so
  // PAUSED would post PAUSED message immediately)
  //
  gst_element_set_state(d_pipeline,GST_STATE_READY);

  return true;
}


void GstRecordStream::DestroyPipeline()
{
  if(d_pipeline!=NULL) {
    d_bus_timer->stop();
    d_stop_timer->stop();
    gst_element_set_state(d_pipeline,GST_STATE_NULL);
    gst_object_unref(d_pipeline);
    d_pipeline=NULL;
    d_src=NULL;
    d_audioconvert=NULL;
    d_audioresample=NULL;
    d_capsfilter=NULL;
    d_level=NULL;
    d_encoder=NULL;
    d_filesink=NULL;
  }
}


void GstRecordStream::WrapAndCopy()
{
  if(d_temp_dir==NULL||d_temp_raw_path.isEmpty()) {
    rda->syslog(LOG_DEBUG,
      "GstRecordStream: WrapAndCopy: skipped, no temp recording");
    return;
  }

  //
  // Open raw recorded audio (MPEG or PCM) from local temp storage
  //
  QFile src(d_temp_raw_path);
  if(!src.open(QIODevice::ReadOnly)) {
    rda->syslog(LOG_ERR,
      "GstRecordStream: WrapAndCopy: FAIL open %s: %s",
      d_temp_raw_path.toUtf8().constData(),
      src.errorString().toUtf8().constData());
    delete d_temp_dir;
    d_temp_dir=NULL;
    d_temp_raw_path.clear();
    return;
  }

  //
  // Create WAV on local disk (needs seek access) -- same
  // temp directory as the raw recording, so RDWaveFile::closeWave()'s
  // seek-back-and-patch has real read-write-seek access throughout.
  // (mirrors RDAudioConvert::Stage3Layer2Wav / Stage3Pcm16 / Stage3Pcm24)
  //
  QString wav_path=d_temp_dir->path()+"/audio.wav";
  RDWaveFile *wav=new RDWaveFile(wav_path);
  switch(d_coding) {
  case 0:  // PCM16
    wav->setFormatTag(WAVE_FORMAT_PCM);
    wav->setChannels(d_channels);
    wav->setSamplesPerSec(d_sample_rate);
    wav->setBitsPerSample(16);
    break;

  case 4:  // PCM24
    wav->setFormatTag(WAVE_FORMAT_PCM);
    wav->setChannels(d_channels);
    wav->setSamplesPerSec(d_sample_rate);
    wav->setBitsPerSample(24);
    break;

  case 1:  // MPEG Layer 1
  case 2:  // MPEG Layer 2
  default:
    wav->setFormatTag(WAVE_FORMAT_MPEG);
    wav->setChannels(d_channels);
    if(d_channels==1) {
      wav->setHeadMode(ACM_MPEG_SINGLECHANNEL);
    }
    else {
      wav->setHeadMode(ACM_MPEG_STEREO);
    }
    wav->setSamplesPerSec(d_sample_rate);
    wav->setHeadLayer(2);
    wav->setHeadBitRate(d_bitrate);
    wav->setMextChunk(true);
    break;
  }
  wav->setBextChunk(true);
  wav->setCartChunk(false);
  wav->setLevlChunk(true);

  if(!wav->createWave()) {
    rda->syslog(LOG_ERR,
      "GstRecordStream: WrapAndCopy: FAIL createWave for %s",
      wav_path.toUtf8().constData());
    src.close();
    delete wav;
    delete d_temp_dir;
    d_temp_dir=NULL;
    d_temp_raw_path.clear();
    return;
  }

  //
  // Stream raw audio through RDWaveFile::writeWave(), which computes
  // levl peak data as it goes (from PCM samples, or from TwoLAME's
  // per-frame energy-level-extension bytes for MPEG)
  //
  char buf[65536];
  qint64 n;
  while((n=src.read(buf,sizeof(buf)))>0) {
    wav->writeWave(buf,n);
  }
  src.close();

  //
  // Close WAV (writes levl chunk, patches sizes)
  //
  wav->closeWave(d_recorded_frames);
  delete wav;

  //
  // Copy finished WAV to final destination, then discard the whole
  // temp directory (RDTempDirectory's destructor removes every file
  // and the directory itself)
  //
  CopyToDestination(wav_path);
  delete d_temp_dir;
  d_temp_dir=NULL;
  d_temp_raw_path.clear();
}


void GstRecordStream::CopyToDestination(const QString &src_path)
{
  //
  // Manual sequential copy, not rename()/link() -- the audio store may
  // be on a filesystem that does not support those operations reliably
  // (see RDAudioConvert::convert()'s identical approach).
  //
  QFile src(src_path);
  if(!src.open(QIODevice::ReadOnly)) {
    rda->syslog(LOG_ERR,
      "GstRecordStream: CopyToDestination: FAIL open src %s: %s",
      src_path.toUtf8().constData(),
      src.errorString().toUtf8().constData());
    return;
  }

  QFile dst(d_dest_filename);
  if(!dst.open(QIODevice::WriteOnly|
               QIODevice::Truncate)) {
    rda->syslog(LOG_ERR,
      "GstRecordStream: CopyToDestination: FAIL open dst %s: %s",
      d_dest_filename.toUtf8().constData(),
      dst.errorString().toUtf8().constData());
    src.close();
    return;
  }

  QByteArray buf;
  while(!(buf=src.read(65536)).isEmpty()) {
    if(dst.write(buf)!=buf.size()) {
      rda->syslog(LOG_ERR,
        "GstRecordStream: CopyToDestination: write error copying "
        "%s to %s: %s",
        src_path.toUtf8().constData(),
        d_dest_filename.toUtf8().constData(),
        dst.errorString().toUtf8().constData());
      break;
    }
  }
  dst.close();
  src.close();

  rda->syslog(LOG_DEBUG,
    "GstRecordStream: CopyToDestination: copied %s to %s",
    src_path.toUtf8().constData(),
    d_dest_filename.toUtf8().constData());
}


const char *GstRecordStream::EncoderName(int coding) const
{
  switch(coding) {
  case 1:  // MPEG Layer 1
  case 2:  // MPEG Layer 2
    return "twolamemp2enc";

  case 3:  // MPEG Layer 3
    return "lamemp3enc";

  default:  // PCM (0=Pcm16, 4=Pcm24) -- no encoder; RDWaveFile
            // writes the WAV container in WrapAndCopy() instead
    return NULL;
  }
}
