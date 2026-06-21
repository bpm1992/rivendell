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
    // EOS already fired -- wrap temp MPEG into WAV+levl
    // and copy to destination (may block on S3 FUSE)
    //
    WrapMpegToWav();
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
  WrapMpegToWav();
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

  d_pipeline=gst_pipeline_new(NULL);
  d_src=gst_element_factory_make("appsrc",NULL);
  d_audioconvert=
    gst_element_factory_make("audioconvert",NULL);
  d_audioresample=
    gst_element_factory_make("audioresample",NULL);
  d_capsfilter=
    gst_element_factory_make("capsfilter",NULL);
  d_level=gst_element_factory_make("level",NULL);
  d_encoder=gst_element_factory_make(enc_name,NULL);
  d_filesink=gst_element_factory_make("filesink",NULL);

  if(!d_pipeline||!d_src||!d_audioconvert||
     !d_audioresample||!d_capsfilter||!d_level||
     !d_encoder||!d_filesink) {
    rda->syslog(LOG_ERR,
                "GstRecordStream: element creation "
                "failed (enc=%s) card=%d port=%d",
                enc_name,d_card,d_port);
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
  // Capsfilter: desired recording channels/rate
  //
  GstCaps *rec_caps=gst_caps_new_simple("audio/x-raw",
    "channels",G_TYPE_INT,chans,
    "rate",G_TYPE_INT,samprate,
    NULL);
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
  // Configure output destination
  //
  if(coding==1||coding==2) {
    d_temp_mpeg_path=
      QString::asprintf("/tmp/caed_%d_%d.mp2",
                        d_card,d_port);
    d_temp_wav_path=
      QString::asprintf("/tmp/caed_%d_%d.wav",
                        d_card,d_port);
    g_object_set(d_filesink,"location",
                 d_temp_mpeg_path.toUtf8().constData(),
                 NULL);
  }
  else {
    g_object_set(d_filesink,"location",
                 filename.toUtf8().constData(),NULL);
  }

  gst_bin_add_many(GST_BIN(d_pipeline),d_src,
    d_audioconvert,d_audioresample,d_capsfilter,
    d_level,d_encoder,d_filesink,NULL);

  if(!gst_element_link_many(d_src,d_audioconvert,
     d_audioresample,d_capsfilter,d_level,
     d_encoder,d_filesink,NULL)) {
    rda->syslog(LOG_ERR,
                "GstRecordStream: pipeline link failed "
                "card=%d port=%d",d_card,d_port);
    DestroyPipeline();
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


void GstRecordStream::WrapMpegToWav()
{
  if(d_coding!=1&&d_coding!=2) {
    rda->syslog(LOG_DEBUG,
      "DBG WrapMpegToWav: skipped, coding=%d",
      d_coding);
    return;
  }
  if(d_temp_mpeg_path.isEmpty()) {
    rda->syslog(LOG_DEBUG,
      "DBG WrapMpegToWav: skipped, empty temp path");
    return;
  }

  rda->syslog(LOG_DEBUG,
    "DBG WrapMpegToWav: START mpeg=%s wav=%s dest=%s",
    d_temp_mpeg_path.toUtf8().constData(),
    d_temp_wav_path.toUtf8().constData(),
    d_dest_filename.toUtf8().constData());

  //
  // Open raw MPEG source
  //
  QFile src(d_temp_mpeg_path);
  if(!src.open(QIODevice::ReadOnly)) {
    rda->syslog(LOG_ERR,
      "DBG WrapMpegToWav: FAIL open mpeg %s: %s",
      d_temp_mpeg_path.toUtf8().constData(),
      src.errorString().toUtf8().constData());
    return;
  }
  qint64 mpeg_size=src.size();
  rda->syslog(LOG_DEBUG,
    "DBG WrapMpegToWav: opened mpeg, size=%lld",
    (long long)mpeg_size);

  //
  // Create WAV on local disk (needs seek access)
  // (mirrors RDAudioConvert::Stage3Layer2Wav)
  //
  RDWaveFile *wav=new RDWaveFile(d_temp_wav_path);
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
  wav->setBextChunk(true);
  wav->setMextChunk(true);
  wav->setCartChunk(false);
  wav->setLevlChunk(true);

  rda->syslog(LOG_DEBUG,
    "DBG WrapMpegToWav: RDWaveFile configured "
    "chans=%d rate=%u layer=2 bitrate=%d",
    d_channels,d_sample_rate,d_bitrate);

  if(!wav->createWave()) {
    rda->syslog(LOG_ERR,
      "DBG WrapMpegToWav: FAIL createWave for %s",
      d_temp_wav_path.toUtf8().constData());
    src.close();
    delete wav;
    QFile::remove(d_temp_mpeg_path);
    d_temp_mpeg_path.clear();
    return;
  }
  rda->syslog(LOG_DEBUG,
    "DBG WrapMpegToWav: createWave OK");

  //
  // Stream raw MPEG through RDWaveFile::writeWave()
  // which extracts energy-level-extension data
  //
  char buf[65536];
  qint64 n;
  qint64 total_written=0;
  int chunk_count=0;
  while((n=src.read(buf,sizeof(buf)))>0) {
    int w=wav->writeWave(buf,n);
    total_written+=w;
    chunk_count++;
  }
  src.close();

  rda->syslog(LOG_DEBUG,
    "DBG WrapMpegToWav: wrote %lld bytes in %d chunks "
    "(mpeg was %lld bytes)",
    (long long)total_written,chunk_count,
    (long long)mpeg_size);

  //
  // Close WAV (writes levl chunk, patches sizes)
  //
  wav->closeWave(d_recorded_frames);
  rda->syslog(LOG_DEBUG,
    "DBG WrapMpegToWav: closeWave done, frames=%u",
    d_recorded_frames);
  delete wav;

  //
  // Verify temp WAV exists and get size
  //
  QFile wav_check(d_temp_wav_path);
  if(wav_check.exists()) {
    rda->syslog(LOG_DEBUG,
      "DBG WrapMpegToWav: temp WAV exists, size=%lld",
      (long long)wav_check.size());
  }
  else {
    rda->syslog(LOG_ERR,
      "DBG WrapMpegToWav: temp WAV MISSING after "
      "closeWave: %s",
      d_temp_wav_path.toUtf8().constData());
  }

  QFile::remove(d_temp_mpeg_path);
  d_temp_mpeg_path.clear();

  rda->syslog(LOG_DEBUG,
    "DBG WrapMpegToWav: calling CopyToDestination");

  //
  // Copy finished WAV to final destination
  //
  CopyToDestination();
}


void GstRecordStream::CopyToDestination()
{
  if(d_temp_wav_path.isEmpty()) {
    rda->syslog(LOG_DEBUG,
      "DBG CopyToDestination: skipped, empty path");
    return;
  }

  rda->syslog(LOG_DEBUG,
    "DBG CopyToDestination: src=%s dst=%s",
    d_temp_wav_path.toUtf8().constData(),
    d_dest_filename.toUtf8().constData());

  QFile src(d_temp_wav_path);
  if(!src.open(QIODevice::ReadOnly)) {
    rda->syslog(LOG_ERR,
      "DBG CopyToDestination: FAIL open src %s: %s",
      d_temp_wav_path.toUtf8().constData(),
      src.errorString().toUtf8().constData());
    return;
  }
  qint64 src_size=src.size();
  rda->syslog(LOG_DEBUG,
    "DBG CopyToDestination: src opened, size=%lld",
    (long long)src_size);

  QFile dst(d_dest_filename);
  if(!dst.open(QIODevice::WriteOnly|
               QIODevice::Truncate)) {
    rda->syslog(LOG_ERR,
      "DBG CopyToDestination: FAIL open dst %s: %s",
      d_dest_filename.toUtf8().constData(),
      dst.errorString().toUtf8().constData());
    src.close();
    return;
  }
  rda->syslog(LOG_DEBUG,
    "DBG CopyToDestination: dst opened for writing");

  QByteArray buf;
  qint64 total_written=0;
  int chunk_count=0;
  while(!(buf=src.read(65536)).isEmpty()) {
    qint64 w=dst.write(buf);
    if(w<0) {
      rda->syslog(LOG_ERR,
        "DBG CopyToDestination: FAIL write chunk %d: %s",
        chunk_count,
        dst.errorString().toUtf8().constData());
      break;
    }
    total_written+=w;
    chunk_count++;
  }

  rda->syslog(LOG_DEBUG,
    "DBG CopyToDestination: wrote %lld/%lld bytes "
    "in %d chunks",
    (long long)total_written,(long long)src_size,
    chunk_count);

  dst.flush();
  if(dst.error()!=QFile::NoError) {
    rda->syslog(LOG_ERR,
      "DBG CopyToDestination: flush error: %s",
      dst.errorString().toUtf8().constData());
  }
  dst.close();
  src.close();

  //
  // Verify destination exists
  //
  QFile dst_check(d_dest_filename);
  if(dst_check.exists()) {
    rda->syslog(LOG_DEBUG,
      "DBG CopyToDestination: dst exists, size=%lld",
      (long long)dst_check.size());
  }
  else {
    rda->syslog(LOG_ERR,
      "DBG CopyToDestination: dst MISSING after "
      "close: %s",
      d_dest_filename.toUtf8().constData());
  }

  QFile::remove(d_temp_wav_path);
  d_temp_wav_path.clear();
  rda->syslog(LOG_INFO,
    "DBG CopyToDestination: DONE copied to %s",
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

  default:  // PCM (0=Pcm16, 4=Pcm24) -> WAV
    return "wavenc";
  }
}
