// gst_playback_stream.cpp
//
// GStreamer-based playback pipeline for caed(8)
//
// Decodes audio files to interleaved F32LE stereo and writes
// to a JACK ringbuffer via appsink.  The JACK process callback
// in GstPipelineManager reads the ringbuffer and mixes into
// output ports.
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

#include <math.h>
#include <string.h>

#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstdirectcontrolbinding.h>

#include <QStringList>

#include <rdapplication.h>

#include "gst_playback_stream.h"


GstPlaybackStream::GstPlaybackStream(int card,int stream,
                                     jack_ringbuffer_t *ringbuffer,
                                     unsigned jack_rate,
                                     volatile uint64_t *frames_played,
                                     volatile bool *draining_flag,
                                     volatile bool *play_active,
                                     QObject *parent)
  :QObject(parent)
{
  d_card=card;
  d_stream=stream;
  d_ringbuffer=ringbuffer;
  d_jack_rate=jack_rate;
  d_state=Stopped;
  d_pipeline=NULL;
  d_uridecodebin=NULL;
  d_audioconvert=NULL;
  d_audioresample=NULL;
  d_scaletempo=NULL;
  d_volume=NULL;
  d_level=NULL;
  d_sink=NULL;
  d_output_level=0;
  d_meter_levels[0]=0;
  d_meter_levels[1]=0;
  d_pad_linked=false;
  d_frames_played_ptr=frames_played;
  d_draining_ptr=draining_flag;
  d_play_active_ptr=play_active;
  d_start_pos_ms=0;
  d_play_rate=1.0;
  d_target_stop_ms=0;
  d_shutting_down=false;
  d_seeking=false;
  d_onsample_bytes=0;
  d_onsample_calls=0;

  d_bus_timer=new QTimer(this);
  d_bus_timer->setSingleShot(false);
  connect(d_bus_timer,SIGNAL(timeout()),
          this,SLOT(busTimerData()));

  d_stop_timer=new QTimer(this);
  d_stop_timer->setSingleShot(true);
  connect(d_stop_timer,SIGNAL(timeout()),
          this,SLOT(stopTimerData()));
}


GstPlaybackStream::~GstPlaybackStream()
{
  DestroyPipeline();
}


int GstPlaybackStream::card() const
{
  return d_card;
}


int GstPlaybackStream::stream() const
{
  return d_stream;
}


GstPlaybackStream::State GstPlaybackStream::state() const
{
  return d_state;
}


bool GstPlaybackStream::load(const QString &uri)
{
  if(d_state!=Stopped&&d_state!=Finished&&
     d_state!=Error&&d_state!=Loaded) {
    return false;
  }
  DestroyPipeline();
  d_uri=uri;
  if(!BuildPipeline(uri)) {
    return false;
  }
  d_state=Loading;
  d_start_pos_ms=0;
  d_play_rate=1.0;
  d_target_stop_ms=0;
  if(d_frames_played_ptr!=NULL) {
    *d_frames_played_ptr=0;
  }
  gst_element_set_state(d_pipeline,GST_STATE_PAUSED);
  GstStateChangeReturn ret=
    gst_element_get_state(d_pipeline,NULL,NULL,
                          5*GST_SECOND);
  if(ret==GST_STATE_CHANGE_FAILURE) {
    rda->syslog(LOG_WARNING,
                "GstPlaybackStream: preroll failed "
                "card=%d stream=%d",d_card,d_stream);
    DestroyPipeline();
    d_state=Error;
    return false;
  }
  d_state=Loaded;
  d_bus_timer->start(10);

  rda->syslog(LOG_DEBUG,
              "GstPlaybackStream: loaded "
              "card=%d stream=%d uri=%s "
              "prefill=%zu",
              d_card,d_stream,
              uri.toUtf8().constData(),
              jack_ringbuffer_read_space(d_ringbuffer));
  return true;
}


bool GstPlaybackStream::play(int length,int speed,bool pitch)
{
  if(d_state!=Loaded) {
    return false;
  }

  //
  // If the pipeline was destroyed by an EOS that arrived
  // during prefill (short file or seek near end decoded
  // everything), rebuild it from the stored URI so we
  // can play properly.
  //
  if(d_pipeline==NULL&&!d_uri.isEmpty()) {
    rda->syslog(LOG_DEBUG,
                "GstPlaybackStream: play-rebuild "
                "card=%d stream=%d "
                "start_pos=%u",
                d_card,d_stream,
                d_start_pos_ms);
    unsigned saved_pos=d_start_pos_ms;
    d_state=Finished;
    jack_ringbuffer_reset(d_ringbuffer);
    if(!load(d_uri)) {
      return false;
    }
    if(saved_pos>0) {
      seek(saved_pos);
    }
  }

  d_state=Playing;

  double rate=(double)speed/100000.0;
  d_play_rate=rate;

  //
  // Only do a flush-seek if the playback rate differs
  // from 1.0.  When rate==1.0, the ringbuffer is
  // already pre-filled from load() or seek() and a
  // flush would discard that cached audio, causing
  // crackle on the first few JACK cycles.
  //
  bool need_seek=(fabs(rate-1.0)>0.001);
  if(need_seek) {
    //
    // Query the current GStreamer decode position
    // before the flush-seek so we know where in
    // source time we're starting from.
    //
    gint64 pos=0;
    gst_element_query_position(d_pipeline,
                               GST_FORMAT_TIME,&pos);
    d_start_pos_ms=(unsigned)(pos/GST_MSECOND);
    jack_ringbuffer_reset(d_ringbuffer);
    gst_element_seek(d_pipeline,rate,GST_FORMAT_TIME,
      (GstSeekFlags)(GST_SEEK_FLAG_FLUSH|
                     GST_SEEK_FLAG_ACCURATE),
      GST_SEEK_TYPE_SET,pos,
      GST_SEEK_TYPE_NONE,GST_CLOCK_TIME_NONE);
  }

  //
  // Reset the JACK frame counter.  Position is now
  // d_start_pos_ms + frames_played * rate / jack_rate.
  //
  if(d_frames_played_ptr!=NULL) {
    *d_frames_played_ptr=0;
  }

  //
  // Closed-loop stop: compute the source-time position
  // at which playback should end.  busTimerData() checks
  // position() against this target every 10ms, ensuring
  // the stop is driven by the actual JACK frame counter
  // rather than a drifting wall-clock timer.
  //
  // length is wall-clock ms; at rate R, we cover
  // length*R source ms in that time.
  //
  if(length>0) {
    d_target_stop_ms=
      d_start_pos_ms+(unsigned)((double)length*rate);
    //
    // Safety fallback: keep d_stop_timer with generous
    // margin in case busTimerData is starved.  This
    // should never fire under normal operation.
    //
    d_stop_timer->start(length+2000);
  }
  else {
    d_target_stop_ms=0;
  }
  d_onsample_bytes=0;
  d_onsample_calls=0;
  GstStateChangeReturn play_ret=
    gst_element_set_state(d_pipeline,GST_STATE_PLAYING);

  //
  // Wait for the ringbuffer to accumulate decoded audio
  // before signaling playStateChanged.  JACK does not
  // start consuming until GstPipelineManager::play()
  // sets d_play_active=true AFTER this method returns,
  // so the ringbuffer fills without contention.  This
  // eliminates start-of-track crackle and avoids the
  // fragile PLAYING->PAUSED->PLAYING dance that
  // PreFillRingbuffer used.
  //
  size_t rb_capacity=
    jack_ringbuffer_read_space(d_ringbuffer)+
    jack_ringbuffer_write_space(d_ringbuffer);
  size_t fill_target=rb_capacity*3/4;
  int fill_wait=0;
  while(jack_ringbuffer_read_space(d_ringbuffer)<
        fill_target&&fill_wait<500) {
    g_usleep(1000);
    fill_wait++;
  }

  rda->syslog(LOG_DEBUG,
              "GstPlaybackStream: play "
              "card=%d stream=%d rate=%.3f "
              "length=%d start_pos=%u "
              "target_stop=%u "
              "rb_avail=%zu "
              "set_state_ret=%d "
              "fill_wait=%dms",
              d_card,d_stream,rate,length,
              d_start_pos_ms,d_target_stop_ms,
              jack_ringbuffer_read_space(
                d_ringbuffer),
              (int)play_ret,fill_wait);
  rda->syslog(LOG_DEBUG,
              "SEGUE_DEBUG: play-start "
              "card=%d stream=%d length=%d "
              "speed=%d rate=%.3f "
              "start_pos=%u target_stop=%u",
              d_card,d_stream,length,speed,
              rate,d_start_pos_ms,
              d_target_stop_ms);
  emit playStateChanged(d_card,d_stream,1);
  return true;
}


bool GstPlaybackStream::stop()
{
  //
  // If we're draining (EOS received, ringbuffer still
  // playing out), allow an explicit stop to abort the
  // drain immediately.  Pipeline is already destroyed.
  //
  if(d_state==Draining) {
    rda->syslog(LOG_DEBUG,
                "SEGUE_DEBUG: stop-during-drain "
                "card=%d stream=%d "
                "rb_remaining=%zu "
                "stop_timer_active=%d",
                d_card,d_stream,
                jack_ringbuffer_read_space(
                  d_ringbuffer),
                d_stop_timer->isActive());
    d_bus_timer->stop();
    if(d_draining_ptr!=NULL) {
      *d_draining_ptr=false;
    }
    jack_ringbuffer_reset(d_ringbuffer);
    d_target_stop_ms=0;
    if(d_frames_played_ptr!=NULL) {
      *d_frames_played_ptr=0;
    }
    d_play_rate=1.0;
    d_state=Finished;
    emit playStateChanged(d_card,d_stream,0);
    return true;
  }

  if(d_state!=Playing&&d_state!=Loaded) {
    return false;
  }
  d_stop_timer->stop();

  //
  // Capture the current playout position before
  // resetting so the deck remembers where it stopped.
  //
  unsigned cur_pos=position();

  rda->syslog(LOG_DEBUG,
              "GstPlaybackStream: stop "
              "card=%d stream=%d pos=%u",
              d_card,d_stream,cur_pos);
  rda->syslog(LOG_DEBUG,
              "SEGUE_DEBUG: stop "
              "card=%d stream=%d pos=%u "
              "rb_remaining=%zu",
              d_card,d_stream,cur_pos,
              jack_ringbuffer_read_space(
                d_ringbuffer));

  //
  // If the pipeline was already destroyed by EOS
  // (e.g., short file decoded entirely during prefill),
  // skip pipeline operations but still reset state.
  // The next seek() will rebuild the pipeline.
  //
  if(d_pipeline!=NULL) {
    gst_element_set_state(d_pipeline,GST_STATE_PAUSED);
  }
  jack_ringbuffer_reset(d_ringbuffer);

  //
  // Reset frame counter and set start position to
  // where we stopped so position() returns the
  // correct value after stop.
  //
  d_start_pos_ms=cur_pos;
  d_target_stop_ms=0;
  if(d_frames_played_ptr!=NULL) {
    *d_frames_played_ptr=0;
  }
  d_play_rate=1.0;

  //
  // Seek the pipeline to the stop position so that
  // GStreamer's internal decode position matches
  // d_start_pos_ms.  Without this, a subsequent
  // play() would start decoding from wherever
  // GStreamer was (ahead by the ringbuffer amount).
  //
  if(d_pipeline!=NULL) {
    gint64 stop_ns=
      (gint64)cur_pos*(gint64)GST_MSECOND;
    gst_element_seek_simple(d_pipeline,
      GST_FORMAT_TIME,
      (GstSeekFlags)(GST_SEEK_FLAG_FLUSH|
                     GST_SEEK_FLAG_ACCURATE),
      stop_ns);
    gst_element_get_state(d_pipeline,NULL,NULL,
                          2*GST_SECOND);
  }

  d_state=Loaded;

  emit playStateChanged(d_card,d_stream,0);
  return true;
}


bool GstPlaybackStream::seek(unsigned pos_ms)
{
  //
  // If the pipeline was destroyed by EOS (during prefill
  // while Loaded, or during Playing), rebuild it from the
  // stored URI so the stream can continue to be used.
  //
  if(d_pipeline==NULL&&!d_uri.isEmpty()&&
     (d_state==Loaded||d_state==Draining||
      d_state==Finished)) {
    rda->syslog(LOG_DEBUG,
                "GstPlaybackStream: seek-rebuild "
                "card=%d stream=%d state=%d "
                "pos=%ums",
                d_card,d_stream,d_state,pos_ms);
    if(d_state==Draining) {
      d_bus_timer->stop();
      if(d_draining_ptr!=NULL) {
        *d_draining_ptr=false;
      }
    }
    jack_ringbuffer_reset(d_ringbuffer);
    d_state=Finished;
    QString uri=d_uri;
    if(!load(uri)) {
      return false;
    }
    //
    // Pipeline is rebuilt at position 0, state is
    // Loaded.  Fall through to do the actual seek.
    //
  }

  if(d_state!=Loaded&&d_state!=Playing) {
    return false;
  }
  gint64 pos=(gint64)pos_ms*(gint64)GST_MSECOND;
  rda->syslog(LOG_DEBUG,
              "GstPlaybackStream: seek "
              "card=%d stream=%d pos=%ums",
              d_card,d_stream,pos_ms);

  //
  // Signal the onNewSample streaming thread to
  // discard any buffer it's currently holding.
  // Without this, the currently spin-waiting
  // onNewSample would write pre-seek audio into
  // the ringbuffer as soon as we reset it.
  //
  d_seeking=true;

  //
  // When seeking during playback, JACK is actively
  // consuming the ringbuffer.  Gate JACK off so it
  // doesn't read stale/garbage data while we flush
  // and refill.
  //
  bool was_playing=(d_state==Playing);
  if(was_playing&&d_play_active_ptr!=NULL) {
    *d_play_active_ptr=false;
  }

  jack_ringbuffer_reset(d_ringbuffer);

  gboolean ok=gst_element_seek_simple(d_pipeline,
    GST_FORMAT_TIME,
    (GstSeekFlags)(GST_SEEK_FLAG_FLUSH|
                   GST_SEEK_FLAG_ACCURATE),
    pos);
  if(ok) {
    //
    // Wait for the seek to settle so the pipeline
    // is ready at the new position.
    //
    gst_element_get_state(d_pipeline,NULL,NULL,
                          2*GST_SECOND);
  }

  //
  // The flush-seek has completed and GStreamer is now
  // decoding from the new position.  Reset the
  // ringbuffer again to discard any stale data that
  // onNewSample may have slipped in before seeing
  // d_seeking, then allow new samples through.
  //
  jack_ringbuffer_reset(d_ringbuffer);
  d_seeking=false;

  //
  // Update start position and reset frame counter
  // so position() tracks from the new seek point.
  //
  d_start_pos_ms=pos_ms;
  if(d_frames_played_ptr!=NULL) {
    *d_frames_played_ptr=0;
  }

  if(was_playing&&ok) {
    //
    // Wait for the ringbuffer to refill with audio
    // from the new position before re-enabling JACK.
    // JACK is gated off, so the buffer fills without
    // contention (same pattern as play()).
    //
    size_t rb_capacity=
      jack_ringbuffer_read_space(d_ringbuffer)+
      jack_ringbuffer_write_space(d_ringbuffer);
    size_t fill_target=rb_capacity*3/4;
    int fill_wait=0;
    while(jack_ringbuffer_read_space(d_ringbuffer)<
          fill_target&&fill_wait<500) {
      g_usleep(1000);
      fill_wait++;
    }
    rda->syslog(LOG_DEBUG,
                "GstPlaybackStream: seek-refill "
                "card=%d stream=%d pos=%ums "
                "rb_avail=%zu fill_wait=%dms",
                d_card,d_stream,pos_ms,
                jack_ringbuffer_read_space(
                  d_ringbuffer),
                fill_wait);
    if(d_play_active_ptr!=NULL) {
      *d_play_active_ptr=true;
    }
  }
  return ok==TRUE;
}


void GstPlaybackStream::setOutputVolume(int level_cb)
{
  d_output_level=level_cb;
  ApplyVolume();
}


void GstPlaybackStream::fadeOutputVolume(int level_cb,
                                         int length_ms)
{
  if(d_volume==NULL||length_ms<=0) {
    setOutputVolume(level_cb);
    return;
  }

  double start_vol=CentibelsToLinear(d_output_level);
  double end_vol=CentibelsToLinear(level_cb);
  d_output_level=level_cb;

  gint64 pos=0;
  gst_element_query_position(d_pipeline,
                             GST_FORMAT_TIME,&pos);

  GstInterpolationControlSource *ics=
    GST_INTERPOLATION_CONTROL_SOURCE(
      gst_interpolation_control_source_new());
  GstControlSource *cs=GST_CONTROL_SOURCE(ics);
  g_object_set(ics,"mode",
               GST_INTERPOLATION_MODE_LINEAR,NULL);

  GstTimedValueControlSource *tvcs=
    GST_TIMED_VALUE_CONTROL_SOURCE(ics);
  gst_timed_value_control_source_set(tvcs,pos,start_vol);
  gst_timed_value_control_source_set(tvcs,
    pos+(gint64)length_ms*(gint64)GST_MSECOND,end_vol);

  GstControlBinding *binding=
    gst_direct_control_binding_new_absolute(
      GST_OBJECT(d_volume),"volume",cs);
  gst_object_add_control_binding(
    GST_OBJECT(d_volume),binding);
  gst_object_unref(ics);
}


unsigned GstPlaybackStream::position() const
{
  if(d_frames_played_ptr==NULL) {
    return 0;
  }

  //
  // Compute playout position from the JACK frame counter.
  // d_play_frames_out is incremented in the JACK RT callback
  // each time frames are actually read from the ringbuffer and
  // delivered to the output ports.  This is the authoritative
  // count of what has actually been played.
  //
  // For timescaled playback (rate != 1.0), scaletempo outputs
  // at the same sample rate but covers source material at
  // rate× speed, so each output frame represents rate× source
  // time.
  //
  uint64_t frames=*d_frames_played_ptr;
  double elapsed_ms=
    (double)frames*d_play_rate*1000.0/(double)d_jack_rate;
  return d_start_pos_ms+(unsigned)elapsed_ms;
}


bool GstPlaybackStream::getStreamOutputMeters(
  short levels[2])
{
  if(d_state!=Playing) {
    return false;
  }
  levels[0]=d_meter_levels[0];
  levels[1]=d_meter_levels[1];
  return true;
}


void GstPlaybackStream::busTimerData()
{
  //
  // Drain check: after EOS the pipeline is destroyed but
  // audio may still be buffered in the JACK ringbuffer.
  // Wait for the RT callback to consume all remaining
  // samples before signalling completion.
  //
  if(d_state==Draining) {
    //
    // Safety timeout: if draining has taken longer than
    // 5 seconds, force completion to prevent stuck streams.
    //
    if(d_drain_start_time.elapsed()>5000) {
      rda->syslog(LOG_WARNING,
                  "SEGUE_DEBUG: drain-timeout "
                  "card=%d stream=%d "
                  "elapsed=%dms",
                  d_card,d_stream,
                  d_drain_start_time.elapsed());
      d_bus_timer->stop();
      if(d_draining_ptr!=NULL) {
        *d_draining_ptr=false;
      }
      d_state=Finished;
      emit playStateChanged(d_card,d_stream,0);
      return;
    }
    size_t rb_remaining=
      jack_ringbuffer_read_space(d_ringbuffer);
    if(rb_remaining==0) {
      rda->syslog(LOG_DEBUG,
                  "SEGUE_DEBUG: drain-complete "
                  "card=%d stream=%d",
                  d_card,d_stream);
      d_bus_timer->stop();
      if(d_draining_ptr!=NULL) {
        *d_draining_ptr=false;
      }
      d_state=Finished;
      emit playStateChanged(d_card,d_stream,0);
    }
    return;
  }

  if(d_pipeline==NULL) {
    return;
  }

  //
  // Periodic ringbuffer fill-level diagnostic during
  // Playing state.  Log every ~1s (100 timer ticks).
  //
  if(d_state==Playing) {
    static int diag_counter=0;
    size_t rb_fill=
      jack_ringbuffer_read_space(d_ringbuffer);
    size_t rb_free=
      jack_ringbuffer_write_space(d_ringbuffer);
    if(rb_fill==0) {
      rda->syslog(LOG_WARNING,
                  "GstPlaybackStream: UNDERRUN "
                  "card=%d stream=%d pos=%u",
                  d_card,d_stream,position());
    }
    else if((diag_counter++%100)==0) {
      //
      // Peek at first few samples to check if
      // ringbuffer contains non-zero audio
      //
      float peek_buf[8];
      size_t peeked=jack_ringbuffer_peek(
        d_ringbuffer,(char *)peek_buf,
        sizeof(peek_buf));
      float peak_val=0.0f;
      for(size_t i=0;i<peeked/sizeof(float);i++) {
        float av=fabsf(peek_buf[i]);
        if(av>peak_val) {
          peak_val=av;
        }
      }
      rda->syslog(LOG_DEBUG,
                  "GstPlaybackStream: rb_fill "
                  "card=%d stream=%d fill=%zu "
                  "free=%zu pos=%u "
                  "onsample_calls=%zu "
                  "onsample_bytes=%zu "
                  "peek_peak=%.6f "
                  "meter_L=%d meter_R=%d",
                  d_card,d_stream,rb_fill,
                  rb_free,position(),
                  d_onsample_calls,
                  d_onsample_bytes,
                  peak_val,
                  (int)d_meter_levels[0],
                  (int)d_meter_levels[1]);
    }
  }

  //
  // Closed-loop stop check: compare the authoritative
  // JACK frame counter position against the target stop
  // position.  This replaces the wall-clock d_stop_timer
  // as the primary stop mechanism, ensuring audio is
  // never cut short or left running due to clock drift.
  //
  if(d_target_stop_ms>0&&d_state==Playing) {
    unsigned pos=position();
    if(pos>=d_target_stop_ms) {
      size_t rb_remaining=jack_ringbuffer_read_space(d_ringbuffer);
      rda->syslog(LOG_DEBUG,
                  "SEGUE_DEBUG: position-stop "
                  "card=%d stream=%d pos=%u "
                  "target=%u rb_remaining=%zu",
                  d_card,d_stream,pos,
                  d_target_stop_ms,
                  rb_remaining);
      d_target_stop_ms=0;
      //
      // Stop the GStreamer pipeline so it produces no more data,
      // but leave the ring buffer intact so JACK can drain the
      // remaining decoded audio naturally.  Calling stop() here
      // would reset the ring buffer mid-content, chopping off the
      // tail of short files (e.g. jingles) that were fully decoded
      // into the buffer ahead of JACK's consumption position.
      // An explicit operator stop() will still abort the drain
      // immediately via the stop-during-drain branch.
      //
      if(d_pipeline!=NULL) {
        gst_element_set_state(d_pipeline,GST_STATE_PAUSED);
      }
      d_state=Draining;
      d_drain_start_time.start();
      if(d_draining_ptr!=NULL) {
        *d_draining_ptr=true;
      }
      d_bus_timer->start(10);
      return;
    }
  }

  GstBus *bus=gst_element_get_bus(d_pipeline);
  GstMessage *msg=NULL;
  while((msg=gst_bus_pop(bus))!=NULL) {
    switch(GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      rda->syslog(LOG_DEBUG,
                  "SEGUE_DEBUG: EOS "
                  "card=%d stream=%d "
                  "state=%d "
                  "rb_remaining=%zu pos=%u "
                  "stop_timer_active=%d "
                  "stop_timer_remaining=%d",
                  d_card,d_stream,
                  d_state,
                  jack_ringbuffer_read_space(
                    d_ringbuffer),
                  position(),
                  d_stop_timer->isActive(),
                  d_stop_timer->remainingTime());
      d_target_stop_ms=0;
      d_stop_timer->stop();
      DestroyPipeline();

      //
      // If EOS arrives while still in Loaded state
      // (short file or seek near end decoded everything
      // during prefill), keep Loaded so play() and
      // seek() can still be called.  Clear the
      // ringbuffer since it contains data from the
      // wrong position — play() will rebuild the
      // pipeline from scratch.
      //
      if(d_state==Loaded) {
        jack_ringbuffer_reset(d_ringbuffer);
        d_bus_timer->start(10);
        break;
      }

      d_state=Draining;
      d_drain_start_time.start();
      if(d_draining_ptr!=NULL) {
        *d_draining_ptr=true;
      }
      //
      // Restart the bus timer so the drain check
      // at the top of busTimerData() continues to
      // fire.  DestroyPipeline() stopped it.
      //
      d_bus_timer->start(10);
      break;

    case GST_MESSAGE_ERROR: {
      GError *err=NULL;
      gchar *dbg=NULL;
      gst_message_parse_error(msg,&err,&dbg);
      rda->syslog(LOG_WARNING,
                  "GstPlaybackStream error "
                  "card=%d stream=%d: %s",
                  d_card,d_stream,err->message);
      g_error_free(err);
      g_free(dbg);
      d_stop_timer->stop();
      d_bus_timer->stop();
      DestroyPipeline();
      d_state=Error;
      emit playStateChanged(d_card,d_stream,0);
      break;
    }

    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s=
        gst_message_get_structure(msg);
      if(s!=NULL) {
        const gchar *sname=gst_structure_get_name(s);
        if(g_strcmp0(sname,"level")==0) {
          const GValue *peak=
            gst_structure_get_value(s,"peak");
          if(peak==NULL) {
            break;
          }
          //
          // Handle both GValueArray (GStreamer level
          // element uses this) and GstValueList
          //
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
          if(G_VALUE_HOLDS(peak,
               G_TYPE_VALUE_ARRAY)) {
            GValueArray *arr=
              (GValueArray *)
              g_value_get_boxed(peak);
            if(arr!=NULL) {
              unsigned nch=arr->n_values;
              for(unsigned i=0;i<nch&&i<2;i++) {
                double db=g_value_get_double(
                  g_value_array_get_nth(arr,i));
                if(db<-100.0) {
                  d_meter_levels[i]=-10000;
                }
                else {
                  d_meter_levels[i]=
                    (short)(db*100.0);
                }
              }
              if(nch==1) {
                d_meter_levels[1]=
                  d_meter_levels[0];
              }
            }
          }
          else if(GST_VALUE_HOLDS_LIST(peak)) {
#pragma GCC diagnostic pop
            unsigned nch=
              gst_value_list_get_size(peak);
            for(unsigned i=0;i<nch&&i<2;i++) {
              const GValue *v=
                gst_value_list_get_value(peak,i);
              double db=g_value_get_double(v);
              if(db<-100.0) {
                d_meter_levels[i]=-10000;
              }
              else {
                d_meter_levels[i]=
                  (short)(db*100.0);
              }
            }
            if(nch==1) {
              d_meter_levels[1]=
                d_meter_levels[0];
            }
          }
#pragma GCC diagnostic pop
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


void GstPlaybackStream::stopTimerData()
{
  rda->syslog(LOG_WARNING,
              "SEGUE_DEBUG: safety-timer-fired "
              "card=%d stream=%d state=%d",
              d_card,d_stream,d_state);
  stop();
}


void GstPlaybackStream::onPadAdded(GstElement *element,
                                   GstPad *pad,
                                   gpointer data)
{
  GstPlaybackStream *self=
    static_cast<GstPlaybackStream*>(data);
  (void)element;

  if(self->d_pad_linked) {
    return;
  }

  GstCaps *caps=gst_pad_get_current_caps(pad);
  if(!caps) {
    caps=gst_pad_query_caps(pad,NULL);
  }
  if(!caps) {
    return;
  }

  GstStructure *str=gst_caps_get_structure(caps,0);
  const gchar *name=gst_structure_get_name(str);

  if(g_str_has_prefix(name,"audio/")) {
    if(self->d_audioconvert==NULL) {
      gst_caps_unref(caps);
      return;
    }
    GstPad *sinkpad=
      gst_element_get_static_pad(
        self->d_audioconvert,"sink");
    if(sinkpad&&!gst_pad_is_linked(sinkpad)) {
      if(gst_pad_link(pad,sinkpad)==GST_PAD_LINK_OK) {
        self->d_pad_linked=true;
      }
      else {
        rda->syslog(LOG_WARNING,
                    "GstPlaybackStream: pad link "
                    "failed card=%d stream=%d",
                    self->d_card,self->d_stream);
      }
      gst_object_unref(sinkpad);
    }
  }
  gst_caps_unref(caps);
}


//
// Appsink callback — runs in GStreamer streaming thread.
// Writes decoded audio into the JACK ringbuffer.
//
GstFlowReturn GstPlaybackStream::onNewSample(
  GstAppSink *sink,gpointer data)
{
  GstPlaybackStream *self=
    static_cast<GstPlaybackStream*>(data);
  GstSample *sample=gst_app_sink_pull_sample(sink);
  if(!sample) {
    return GST_FLOW_ERROR;
  }
  GstBuffer *buf=gst_sample_get_buffer(sample);
  GstMapInfo map;
  if(gst_buffer_map(buf,&map,GST_MAP_READ)) {
    //
    // Write all decoded data to the ringbuffer.
    // Spin-wait for space to avoid dropping audio
    // data, which causes audible glitches.
    //
    size_t written=0;
    while(written<map.size) {
      if(self->d_shutting_down) {
        break;
      }
      if(self->d_seeking) {
        break;
      }
      size_t space=
        jack_ringbuffer_write_space(
          self->d_ringbuffer);
      size_t remaining=map.size-written;
      size_t chunk=
        (remaining<space)?remaining:space;
      if(chunk>0) {
        jack_ringbuffer_write(self->d_ringbuffer,
          (const char *)map.data+written,chunk);
        written+=chunk;
        self->d_onsample_bytes+=chunk;
      }
      else {
        g_usleep(100);
      }
    }
    gst_buffer_unmap(buf,&map);
  }
  self->d_onsample_calls++;
  gst_sample_unref(sample);
  return GST_FLOW_OK;
}


bool GstPlaybackStream::BuildPipeline(
  const QString &uri)
{
  d_pad_linked=false;
  d_shutting_down=false;
  d_seeking=false;

  d_pipeline=gst_pipeline_new(NULL);
  d_uridecodebin=
    gst_element_factory_make("uridecodebin",NULL);
  d_audioconvert=
    gst_element_factory_make("audioconvert",NULL);
  d_audioresample=
    gst_element_factory_make("audioresample",NULL);
  d_scaletempo=
    gst_element_factory_make("scaletempo",NULL);
  d_volume=gst_element_factory_make("volume",NULL);
  d_level=gst_element_factory_make("level",NULL);
  d_sink=gst_element_factory_make("appsink",NULL);

  bool have_scaletempo=(d_scaletempo!=NULL);

  if(!d_pipeline||!d_uridecodebin||
     !d_audioconvert||!d_audioresample||
     !d_volume||!d_level||!d_sink) {
    rda->syslog(LOG_ERR,
                "GstPlaybackStream: element creation "
                "failed card=%d stream=%d",
                d_card,d_stream);
    DestroyPipeline();
    return false;
  }

  g_object_set(d_uridecodebin,"uri",
               uri.toUtf8().constData(),NULL);

  //
  // Configure uridecodebin to buffer more compressed
  // data for network sources.  5MB covers ~5 minutes
  // of 128kbps MP3 while using minimal RAM.
  //
  g_object_set(d_uridecodebin,
               "buffer-size",(gint)(5*1024*1024),
               NULL);

  g_object_set(d_level,
               "post-messages",TRUE,
               "interval",(guint64)(50*GST_MSECOND),
               "peak-ttl",(guint64)(100*GST_MSECOND),
               "peak-falloff",10.0,
               NULL);

  g_object_set(d_volume,"volume",
               CentibelsToLinear(d_output_level),NULL);

  //
  // Configure appsink: F32LE interleaved stereo at JACK rate.
  //
  // sync=FALSE is critical: the JACK ringbuffer provides
  // real-time pacing (onNewSample spin-waits when the
  // ringbuffer is full).  GStreamer clock-based sync
  // would stall delivery, causing ringbuffer underruns.
  //
  GstCaps *caps=gst_caps_new_simple("audio/x-raw",
    "format",G_TYPE_STRING,"F32LE",
    "channels",G_TYPE_INT,2,
    "rate",G_TYPE_INT,(int)d_jack_rate,
    "layout",G_TYPE_STRING,"interleaved",
    NULL);
  g_object_set(d_sink,
               "caps",caps,
               "emit-signals",FALSE,
               "sync",FALSE,
               NULL);
  gst_caps_unref(caps);

  //
  // Set appsink callbacks for ringbuffer delivery
  //
  GstAppSinkCallbacks callbacks;
  memset(&callbacks,0,sizeof(callbacks));
  callbacks.new_sample=onNewSample;
  gst_app_sink_set_callbacks(GST_APP_SINK(d_sink),
                             &callbacks,this,NULL);

  if(have_scaletempo) {
    gst_bin_add_many(GST_BIN(d_pipeline),
      d_uridecodebin,d_audioconvert,
      d_audioresample,d_scaletempo,d_volume,
      d_level,d_sink,NULL);
    if(!gst_element_link_many(d_audioconvert,
       d_audioresample,d_scaletempo,
       d_volume,d_level,d_sink,NULL)) {
      rda->syslog(LOG_ERR,
                  "GstPlaybackStream: chain link "
                  "failed card=%d stream=%d",
                  d_card,d_stream);
      DestroyPipeline();
      return false;
    }
  }
  else {
    d_scaletempo=NULL;
    gst_bin_add_many(GST_BIN(d_pipeline),
      d_uridecodebin,d_audioconvert,
      d_audioresample,d_volume,d_level,d_sink,NULL);
    if(!gst_element_link_many(d_audioconvert,
       d_audioresample,d_volume,d_level,d_sink,NULL)) {
      rda->syslog(LOG_ERR,
                  "GstPlaybackStream: chain link "
                  "failed card=%d stream=%d",
                  d_card,d_stream);
      DestroyPipeline();
      return false;
    }
  }

  g_signal_connect(d_uridecodebin,"pad-added",
                   G_CALLBACK(onPadAdded),this);

  return true;
}


void GstPlaybackStream::DestroyPipeline()
{
  if(d_pipeline!=NULL) {
    d_shutting_down=true;
    d_bus_timer->stop();
    d_stop_timer->stop();
    gst_element_set_state(d_pipeline,GST_STATE_NULL);
    gst_object_unref(d_pipeline);
    d_pipeline=NULL;
    d_uridecodebin=NULL;
    d_audioconvert=NULL;
    d_audioresample=NULL;
    d_scaletempo=NULL;
    d_volume=NULL;
    d_level=NULL;
    d_sink=NULL;
  }
}


void GstPlaybackStream::ApplyVolume()
{
  if(d_volume==NULL) {
    return;
  }
  g_object_set(d_volume,"volume",
               CentibelsToLinear(d_output_level),NULL);
}


double GstPlaybackStream::CentibelsToLinear(int cb) const
{
  if(cb<=-10000) {
    return 0.0;
  }
  return pow(10.0,(double)cb/2000.0);
}
