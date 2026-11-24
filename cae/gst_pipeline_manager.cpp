// gst_pipeline_manager.cpp
//
// GStreamer pipeline manager for caed(8)
//
// Implements a persistent JACK mixer client with GStreamer-based
// file decode/encode.  Playback streams decode to appsink →
// ringbuffer → JACK output.  Record streams capture JACK input
// → ringbuffer → appsrc → encoder.
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
#include <unistd.h>

#include <QTimer>

#include <rdapplication.h>
#include <rdprofile.h>

#include "gst_pipeline_manager.h"


GstPipelineManager::GstPipelineManager(QObject *parent)
  :QObject(parent)
{
  d_card_number=0;
  d_input_ports=8;
  d_output_ports=8;
  d_jack_client=NULL;
  d_jack_rate=44100;
  d_prev_output_frames=0;
  memset(d_prev_output,0,sizeof(d_prev_output));
  memset(d_input_copy,0,sizeof(d_input_copy));
  memset(d_input_prev,0,sizeof(d_input_prev));
  memset(d_mm_config,0,sizeof(d_mm_config));
  memset(d_mm_gain_now,0,sizeof(d_mm_gain_now));
  memset(d_mm_excluded,0,sizeof(d_mm_excluded));
  d_self_loop=0;
  d_mm_current=0;
  d_mm_blocked=0;
  d_mm_rt_blocked=0;
  d_mm_logged=0;
  d_cycles=0;
  d_graph_changed=false;
  memset((void *)d_output_peak,0,sizeof(d_output_peak));
  memset((void *)d_input_peak,0,sizeof(d_input_peak));

  for(int i=0;i<RD_MAX_STREAMS;i++) {
    d_play_streams[i]=NULL;
    d_play_rb[i]=NULL;
    d_play_active[i]=false;
    d_play_draining[i]=false;
    d_play_frames_out[i]=0;
    d_fade_state[i].active=false;
    d_fade_state[i].port=0;
    d_fade_state[i].from_linear=0.0f;
    d_fade_state[i].to_linear=0.0f;
    d_fade_state[i].steps_total=1;
    d_fade_state[i].steps_done=0;
    for(int j=0;j<RD_MAX_PORTS;j++) {
      d_output_vol[i][j]=0.0f;
    }
  }
  for(int i=0;i<RD_MAX_PORTS;i++) {
    d_record_streams[i]=NULL;
    d_record_rb[i]=NULL;
    d_record_active[i]=false;
    for(int j=0;j<2;j++) {
      d_jack_out[i][j]=NULL;
      d_jack_in[i][j]=NULL;
    }
    for(int j=0;j<RD_MAX_PORTS;j++) {
      d_passthrough[i][j]=0.0f;
    }
  }

  d_fade_timer=new QTimer(this);
  d_fade_timer->setSingleShot(false);
  connect(d_fade_timer,SIGNAL(timeout()),this,SLOT(FadeTimerData()));

  d_graph_timer=new QTimer(this);
  d_graph_timer->setSingleShot(false);
  connect(d_graph_timer,SIGNAL(timeout()),this,SLOT(GraphTimerData()));
}


GstPipelineManager::MixMinus::MixMinus()
{
  output=-1;
  source=-1;
  level=0;
  enabled=true;
  state='R';
}


GstPipelineManager::~GstPipelineManager()
{
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    if(d_play_streams[i]!=NULL) {
      delete d_play_streams[i];
      d_play_streams[i]=NULL;
    }
    if(d_play_rb[i]!=NULL) {
      jack_ringbuffer_free(d_play_rb[i]);
      d_play_rb[i]=NULL;
    }
  }
  for(int i=0;i<RD_MAX_PORTS;i++) {
    if(d_record_streams[i]!=NULL) {
      delete d_record_streams[i];
      d_record_streams[i]=NULL;
    }
    if(d_record_rb[i]!=NULL) {
      jack_ringbuffer_free(d_record_rb[i]);
      d_record_rb[i]=NULL;
    }
  }
  if(d_jack_client!=NULL) {
    jack_deactivate(d_jack_client);
    jack_client_close(d_jack_client);
    d_jack_client=NULL;
  }
}


bool GstPipelineManager::initialize()
{
  GError *err=NULL;

  //
  // Initialize GStreamer
  //
  if(!gst_init_check(NULL,NULL,&err)) {
    rda->syslog(LOG_ERR,
                "GstPipelineManager: gst_init failed: %s",
                err->message);
    g_error_free(err);
    return false;
  }

  //
  // Read configuration from rd.conf [Cae] section
  //
  RDProfile *profile=new RDProfile();
  profile->setSource(rda->config()->filename());
  d_card_number=profile->intValue("Cae","CardNumber",0);
  d_input_ports=profile->intValue("Cae","InputPorts",8);
  d_output_ports=profile->intValue("Cae","OutputPorts",8);
  delete profile;

  //
  // Open JACK client
  //
  QString client_name=
    QString::asprintf("rivendell_%d",d_card_number);
  jack_status_t status;
  d_jack_client=
    jack_client_open(client_name.toUtf8().constData(),
                     JackNoStartServer,&status);
  if(d_jack_client==NULL) {
    rda->syslog(LOG_ERR,
                "GstPipelineManager: jack_client_open "
                "failed (status=0x%x)",status);
    return false;
  }
  d_jack_rate=jack_get_sample_rate(d_jack_client);

  //
  // Register output ports (playout_NL, playout_NR)
  //
  for(int i=0;i<d_output_ports;i++) {
    QString name_l=QString::asprintf("playout_%dL",i);
    QString name_r=QString::asprintf("playout_%dR",i);
    d_jack_out[i][0]=
      jack_port_register(d_jack_client,
                         name_l.toUtf8().constData(),
                         JACK_DEFAULT_AUDIO_TYPE,
                         JackPortIsOutput,0);
    d_jack_out[i][1]=
      jack_port_register(d_jack_client,
                         name_r.toUtf8().constData(),
                         JACK_DEFAULT_AUDIO_TYPE,
                         JackPortIsOutput,0);
    if(d_jack_out[i][0]==NULL||d_jack_out[i][1]==NULL) {
      rda->syslog(LOG_ERR,
                  "GstPipelineManager: failed to register "
                  "output port %d",i);
      jack_client_close(d_jack_client);
      d_jack_client=NULL;
      return false;
    }
  }

  //
  // Register input ports (record_NL, record_NR)
  //
  for(int i=0;i<d_input_ports;i++) {
    QString name_l=QString::asprintf("record_%dL",i);
    QString name_r=QString::asprintf("record_%dR",i);
    d_jack_in[i][0]=
      jack_port_register(d_jack_client,
                         name_l.toUtf8().constData(),
                         JACK_DEFAULT_AUDIO_TYPE,
                         JackPortIsInput,0);
    d_jack_in[i][1]=
      jack_port_register(d_jack_client,
                         name_r.toUtf8().constData(),
                         JACK_DEFAULT_AUDIO_TYPE,
                         JackPortIsInput,0);
    if(d_jack_in[i][0]==NULL||d_jack_in[i][1]==NULL) {
      rda->syslog(LOG_ERR,
                  "GstPipelineManager: failed to register "
                  "input port %d",i);
      jack_client_close(d_jack_client);
      d_jack_client=NULL;
      return false;
    }
  }

  //
  // Allocate ringbuffers for all stream/port slots
  //
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    d_play_rb[i]=jack_ringbuffer_create(GST_JACK_RB_SIZE);
    jack_ringbuffer_mlock(d_play_rb[i]);
  }
  for(int i=0;i<RD_MAX_PORTS;i++) {
    d_record_rb[i]=jack_ringbuffer_create(GST_JACK_RB_SIZE);
    jack_ringbuffer_mlock(d_record_rb[i]);
  }

  //
  // Set JACK process callback and activate
  //
  jack_set_process_callback(d_jack_client,JackProcess,this);
  jack_set_port_connect_callback(d_jack_client,JackPortConnect,this);
  if(jack_activate(d_jack_client)!=0) {
    rda->syslog(LOG_ERR,
                "GstPipelineManager: jack_activate failed");
    jack_client_close(d_jack_client);
    d_jack_client=NULL;
    return false;
  }

  rda->syslog(LOG_INFO,
              "GstPipelineManager: JACK client \"%s\" "
              "rate=%u in=%d out=%d",
              client_name.toUtf8().constData(),
              d_jack_rate,d_input_ports,d_output_ports);
  d_graph_timer->start(50);
  CheckGraph();
  return true;
}


//
// JACK real-time process callback.
//
// Mixes playback stream ringbuffers into output ports,
// applies passthrough routing, and feeds record ringbuffers
// from input ports.
//
int GstPipelineManager::JackProcess(jack_nframes_t nframes,
                                    void *arg)
{
  GstPipelineManager *mgr=
    static_cast<GstPipelineManager*>(arg);

  if(nframes>GST_JACK_MAX_FRAMES) {
    nframes=GST_JACK_MAX_FRAMES;
  }
  const MixMinusConfig *mm=&mgr->d_mm_config[mgr->d_mm_current.load()];
  uint32_t mm_blocked=mgr->d_mm_blocked.load();
  uint32_t self_loop=mgr->d_self_loop.load();

  //
  // Copy input port buffers before touching any output: an input fed
  // by one of our own outputs may share that output's buffer
  //
  float *in_l[RD_MAX_PORTS];
  float *in_r[RD_MAX_PORTS];
  const void *in_raw[RD_MAX_PORTS][2];
  for(int p=0;p<mgr->d_input_ports;p++) {
    in_l[p]=mgr->d_input_copy[p][0];
    in_r[p]=mgr->d_input_copy[p][1];
    in_raw[p][0]=jack_port_get_buffer(mgr->d_jack_in[p][0],nframes);
    in_raw[p][1]=jack_port_get_buffer(mgr->d_jack_in[p][1],nframes);
    memcpy(in_l[p],in_raw[p][0],nframes*sizeof(float));
    memcpy(in_r[p],in_raw[p][1],nframes*sizeof(float));
  }

  //
  // Get and zero output port buffers
  //
  float *out_l[RD_MAX_PORTS];
  float *out_r[RD_MAX_PORTS];
  for(int p=0;p<mgr->d_output_ports;p++) {
    out_l[p]=(float *)jack_port_get_buffer(
      mgr->d_jack_out[p][0],nframes);
    out_r[p]=(float *)jack_port_get_buffer(
      mgr->d_jack_out[p][1],nframes);
    memset(out_l[p],0,nframes*sizeof(float));
    memset(out_r[p],0,nframes*sizeof(float));
  }

  //
  // Mix-minus exclusion accumulators
  //
  for(int m=0;m<mm->count;m++) {
    memset(mgr->d_mm_excluded[m][0],0,nframes*sizeof(float));
    memset(mgr->d_mm_excluded[m][1],0,nframes*sizeof(float));
  }

  //
  // Feedback, caught in the cycle it arrives: an input that reaches a
  // mix-minus's source, isn't excluded, and carries that mix-minus's
  // output (JACK delivers a loop within this client one cycle late, as
  // an exact copy, or as the very same buffer) blocks the mix-minus, and
  // is left out of the mix so nothing of it reaches the source, also in
  // the cycles while the blocked mix-minus fades.  A signal that repeats
  // exactly every cycle (a tone whose period divides the buffer) can
  // match without any loop, so an input equal to its own previous cycle
  // is left to the graph check.
  //
  uint32_t loop_inputs=0;
  if(mgr->d_prev_output_frames==nframes) {
    for(int m=0;m<mm->count;m++) {
      int o=mm->output[m];
      for(int ip=0;ip<mgr->d_input_ports;ip++) {
        if(((mm->excluded[m]&(1u<<ip))!=0)||
           (mgr->d_passthrough[ip][mm->source[m]]<=0.0f)) {
          continue;
        }
        bool loop=(in_raw[ip][0]==(const void *)out_l[o])||
          (in_raw[ip][1]==(const void *)out_r[o]);
        for(int c=0;(c<2)&&(!loop);c++) {
          const float *in=c?in_r[ip]:in_l[ip];
          const float *prev=mgr->d_prev_output[o][c];
          const float *prev_in=mgr->d_input_prev[ip][c];
          bool same=true;
          bool sound=false;
          bool repeats=true;
          for(jack_nframes_t i=0;(i<nframes)&&same;i++) {
            same=(in[i]==prev[i]);
            sound=sound||(prev[i]!=0.0f);
            repeats=repeats&&(in[i]==prev_in[i]);
          }
          loop=same&&sound&&(!repeats);
        }
        if(loop) {
          loop_inputs|=(1u<<ip);
          if((mm_blocked&(1u<<o))==0) {
            mm_blocked|=(1u<<o);
            mgr->d_mm_blocked.fetch_or(1u<<o);
            mgr->d_mm_rt_blocked.fetch_or(1u<<o);
            mgr->d_graph_changed.store(true);   // The Qt thread logs why
          }
        }
      }
    }
  }

  //
  // Mix active playback streams into output ports
  //
  size_t frame_bytes=2*sizeof(float);
  size_t needed=nframes*frame_bytes;
  float local[GST_JACK_MAX_FRAMES*2];

  for(int s=0;s<RD_MAX_STREAMS;s++) {
    if(!mgr->d_play_active[s]) {
      continue;
    }
    jack_ringbuffer_t *rb=mgr->d_play_rb[s];
    if(rb==NULL) {
      continue;
    }

    //
    // Read interleaved stereo from ringbuffer
    //
    size_t avail=jack_ringbuffer_read_space(rb);
    size_t to_read=(avail<needed)?avail:needed;
    to_read=(to_read/frame_bytes)*frame_bytes;
    if(to_read>0) {
      jack_ringbuffer_read(rb,(char *)local,to_read);
    }
    size_t frames_read=to_read/frame_bytes;

    //
    // Increment per-stream frame counter.  This is the
    // authoritative count of frames actually delivered
    // to JACK outputs.  GstPlaybackStream::position()
    // uses this for precise playout position tracking.
    //
    mgr->d_play_frames_out[s]+=frames_read;

    //
    // Micro-fade on final partial read during drain.
    // When draining, the ringbuffer is being consumed
    // after GStreamer EOS.  A partial read means these
    // are the last samples.  Apply a short linear
    // ramp-down to avoid a click from the abrupt
    // transition to silence.
    //
    if(mgr->d_play_draining[s]&&
       frames_read>0&&frames_read<(size_t)nframes) {
      size_t fade_len=
        (frames_read<32)?frames_read:32;
      size_t fade_start=frames_read-fade_len;
      for(size_t i=0;i<fade_len;i++) {
        float gain=
          1.0f-(float)(i+1)/(float)fade_len;
        local[(fade_start+i)*2]*=gain;
        local[(fade_start+i)*2+1]*=gain;
      }
    }

    //
    // Zero-pad if underrun
    //
    for(size_t i=frames_read*2;i<(size_t)nframes*2;i++) {
      local[i]=0.0f;
    }

    //
    // Mix into each output port using volume matrix
    //
    for(int p=0;p<mgr->d_output_ports;p++) {
      float vol=mgr->d_output_vol[s][p];
      if((vol<=0.0f)||mm->dedicated[p]) {
        continue;
      }
      for(jack_nframes_t i=0;i<nframes;i++) {
        out_l[p][i]+=local[i*2]*vol;
        out_r[p][i]+=local[i*2+1]*vol;
      }
    }
  }

  //
  // Passthrough: mix JACK input ports to output ports.
  //
  // The ST RML macro sets d_passthrough[in][out] to route
  // audio arriving on record_N (JACK input) to playout_M
  // (JACK output).  Passthroughs to a mix-minus output are
  // ignored: that port carries only its mix-minus.
  //
  // Self-loopback workaround: when an output is connected to
  // the input of the same number on this client, PipeWire may
  // deliver an empty input buffer.  For such an input only, a
  // silent buffer is replaced by the previous cycle's output.
  //
  for(int ip=0;ip<mgr->d_input_ports;ip++) {
    for(int op=0;op<mgr->d_output_ports;op++) {
      float level=mgr->d_passthrough[ip][op];
      if((level<=0.0f)||mm->dedicated[op]||((loop_inputs&(1u<<ip))!=0)) {
        continue;
      }
      //
      // Check whether a self-looped input buffer is silent
      //
      bool port_connected=
        (jack_port_connected(mgr->d_jack_in[ip][0])>0);
      float in_max=0.0f;
      if(port_connected&&((self_loop&(1u<<ip))!=0)) {
        unsigned check=nframes<100?nframes:100;
        for(unsigned i=0;i<check;i++) {
          float a=fabsf(in_l[ip][i]);
          if(a>in_max) {
            in_max=a;
          }
        }
      }
      const float *src_l=NULL;
      const float *src_r=NULL;
      // Never for an input whose same-numbered output is a mix-minus: that
      // previous cycle is the mix-minus itself, and adding it is feedback
      if(port_connected&&((self_loop&(1u<<ip))!=0)&&in_max<0.00001f&&
         (!mm->dedicated[ip])&&
         mgr->d_prev_output_frames==nframes) {
        src_l=mgr->d_prev_output[ip][0];   // Self-loopback fallback
        src_r=mgr->d_prev_output[ip][1];
      }
      else if(port_connected) {
        src_l=in_l[ip];
        src_r=in_r[ip];
      }
      if(src_l==NULL) {
        continue;
      }
      for(jack_nframes_t i=0;i<nframes;i++) {
        out_l[op][i]+=src_l[i]*level;
        out_r[op][i]+=src_r[i]*level;
      }
      //
      // The very samples added from an excluded input, for each
      // mix-minus taken from this output
      //
      for(int m=0;m<mm->count;m++) {
        if((mm->source[m]==op)&&((mm->excluded[m]&(1u<<ip))!=0)) {
          float *ex_l=mgr->d_mm_excluded[m][0];
          float *ex_r=mgr->d_mm_excluded[m][1];
          for(jack_nframes_t i=0;i<nframes;i++) {
            ex_l[i]+=src_l[i]*level;
            ex_r[i]+=src_r[i]*level;
          }
        }
      }
    }
  }

  //
  // Mix-minus outputs: the source's full sum less the excluded
  // inputs, at the mix-minus level.  Gain changes (and blocking)
  // ramp across one cycle.  A source is never itself a mix-minus.
  //
  for(int p=0;p<mgr->d_output_ports;p++) {
    if(!mm->dedicated[p]) {
      mgr->d_mm_gain_now[p]=0.0f;
    }
  }
  for(int m=0;m<mm->count;m++) {
    int o=mm->output[m];
    int src=mm->source[m];
    float target=((mm_blocked&(1u<<o))!=0)?0.0f:mm->gain[m];
    float g0=mgr->d_mm_gain_now[o];
    float step=(target-g0)/(float)nframes;
    const float *ex_l=mgr->d_mm_excluded[m][0];
    const float *ex_r=mgr->d_mm_excluded[m][1];
    if(step==0.0f) {
      for(jack_nframes_t i=0;i<nframes;i++) {
        out_l[o][i]=(out_l[src][i]-ex_l[i])*target;
        out_r[o][i]=(out_r[src][i]-ex_r[i])*target;
      }
    }
    else {
      for(jack_nframes_t i=0;i<nframes;i++) {
        float g=g0+step*(float)(i+1);
        out_l[o][i]=(out_l[src][i]-ex_l[i])*g;
        out_r[o][i]=(out_r[src][i]-ex_r[i])*g;
      }
    }
    mgr->d_mm_gain_now[o]=target;
  }

  //
  // Feed record ringbuffers from input ports
  //
  float rec_buf[GST_JACK_MAX_FRAMES*2];
  for(int p=0;p<mgr->d_input_ports;p++) {
    if(!mgr->d_record_active[p]) {
      continue;
    }
    jack_ringbuffer_t *rb=mgr->d_record_rb[p];
    if(rb==NULL) {
      continue;
    }

    //
    // Interleave L/R into temp buffer
    //
    for(jack_nframes_t i=0;i<nframes;i++) {
      rec_buf[i*2]=in_l[p][i];
      rec_buf[i*2+1]=in_r[p][i];
    }

    size_t bytes=nframes*frame_bytes;
    if(jack_ringbuffer_write_space(rb)>=bytes) {
      jack_ringbuffer_write(rb,(const char *)rec_buf,bytes);
    }
  }

  //
  // Compute output port peak meters
  //
  for(int p=0;p<mgr->d_output_ports;p++) {
    float peak_l=0.0f;
    float peak_r=0.0f;
    for(jack_nframes_t i=0;i<nframes;i++) {
      float al=fabsf(out_l[p][i]);
      float ar=fabsf(out_r[p][i]);
      if(al>peak_l) {
        peak_l=al;
      }
      if(ar>peak_r) {
        peak_r=ar;
      }
    }
    mgr->d_output_peak[p][0]=peak_l;
    mgr->d_output_peak[p][1]=peak_r;
  }

  //
  // Compute input port peak meters
  //
  for(int p=0;p<mgr->d_input_ports;p++) {
    float peak_l=0.0f;
    float peak_r=0.0f;
    for(jack_nframes_t i=0;i<nframes;i++) {
      float al=fabsf(in_l[p][i]);
      float ar=fabsf(in_r[p][i]);
      if(al>peak_l) {
        peak_l=al;
      }
      if(ar>peak_r) {
        peak_r=ar;
      }
    }
    mgr->d_input_peak[p][0]=peak_l;
    mgr->d_input_peak[p][1]=peak_r;
  }

  //
  // Cache current output buffers for PipeWire self-loopback
  //
  for(int p=0;p<mgr->d_output_ports;p++) {
    memcpy(mgr->d_prev_output[p][0],out_l[p],
           nframes*sizeof(float));
    memcpy(mgr->d_prev_output[p][1],out_r[p],
           nframes*sizeof(float));
  }
  mgr->d_prev_output_frames=nframes;
  for(int p=0;p<mgr->d_input_ports;p++) {
    memcpy(mgr->d_input_prev[p][0],in_l[p],nframes*sizeof(float));
    memcpy(mgr->d_input_prev[p][1],in_r[p],nframes*sizeof(float));
  }
  mgr->d_cycles.fetch_add(1);

  return 0;
}


bool GstPipelineManager::hasCard(int card) const
{
  return card==d_card_number;
}


int GstPipelineManager::cardNumber() const
{
  return d_card_number;
}


int GstPipelineManager::inputPortQuantity(int card) const
{
  if(card!=d_card_number) {
    return 0;
  }
  return d_input_ports;
}


int GstPipelineManager::outputPortQuantity(int card) const
{
  if(card!=d_card_number) {
    return 0;
  }
  return d_output_ports;
}


bool GstPipelineManager::timescaleSupported(int card) const
{
  if(card!=d_card_number) {
    return false;
  }
  return gst_element_factory_find("scaletempo")!=NULL;
}


QString GstPipelineManager::version() const
{
  return QString::asprintf("GStreamer %s",
                           gst_version_string());
}


bool GstPipelineManager::loadPlayback(int card,
                                      const QString &wavename,
                                      int *stream)
{
  if(card!=d_card_number) {
    return false;
  }

  //
  // Find a free stream slot
  //
  int slot=-1;
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    if(d_play_streams[i]==NULL) {
      slot=i;
      break;
    }
    GstPlaybackStream::State s=d_play_streams[i]->state();
    if(s==GstPlaybackStream::Finished||
       s==GstPlaybackStream::Error||
       s==GstPlaybackStream::Stopped) {
      d_fade_state[i].active=false;
      delete d_play_streams[i];
      d_play_streams[i]=NULL;
      d_play_active[i]=false;
      slot=i;
      break;
    }
  }
  if(slot<0) {
    rda->syslog(LOG_WARNING,
                "GstPipelineManager: no free stream "
                "slots card=%d",card);
    return false;
  }

  //
  // Reset ringbuffer for this slot
  //
  jack_ringbuffer_reset(d_play_rb[slot]);
  d_play_frames_out[slot]=0;

  GstPlaybackStream *ps=
    new GstPlaybackStream(card,slot,d_play_rb[slot],
                          d_jack_rate,
                          &d_play_frames_out[slot],
                          &d_play_draining[slot],
                          &d_play_active[slot],
                          this);
  connect(ps,SIGNAL(playStateChanged(int,int,int)),
          this,
          SLOT(streamPlayStateChanged(int,int,int)));

  if(!ps->load(wavename)) {
    delete ps;
    return false;
  }

  d_play_streams[slot]=ps;
  *stream=slot;
  rda->syslog(LOG_DEBUG,
              "GstPipelineManager: loadPlayback "
              "card=%d slot=%d uri=%s",
              card,slot,
              wavename.toUtf8().constData());
  return true;
}


bool GstPipelineManager::unloadPlayback(int card,int stream)
{
  GstPlaybackStream *ps=FindPlayStream(card,stream);
  if(ps==NULL) {
    return false;
  }
  if(ps->state()==GstPlaybackStream::Playing||
     ps->state()==GstPlaybackStream::Draining) {
    ps->stop();
  }
  d_play_active[stream]=false;
  d_play_frames_out[stream]=0;
  d_fade_state[stream].active=false;
  delete ps;
  d_play_streams[stream]=NULL;
  jack_ringbuffer_reset(d_play_rb[stream]);
  rda->syslog(LOG_DEBUG,
              "GstPipelineManager: unloadPlayback "
              "card=%d stream=%d",card,stream);
  return true;
}


bool GstPipelineManager::playbackPosition(int card,int stream,
                                          unsigned pos)
{
  GstPlaybackStream *ps=FindPlayStream(card,stream);
  if(ps==NULL) {
    return false;
  }
  return ps->seek(pos);
}


bool GstPipelineManager::play(int card,int stream,int length,
                              int speed,bool pitch,bool rates)
{
  (void)rates;
  GstPlaybackStream *ps=FindPlayStream(card,stream);
  if(ps==NULL) {
    return false;
  }
  if(ps->play(length,speed,pitch)) {
    d_play_active[stream]=true;
    rda->syslog(LOG_DEBUG,
                "GstPipelineManager: play "
                "card=%d stream=%d length=%d "
                "speed=%d active=true",
                card,stream,length,speed);
    return true;
  }
  return false;
}


bool GstPipelineManager::stopPlayback(int card,int stream)
{
  GstPlaybackStream *ps=FindPlayStream(card,stream);
  if(ps==NULL) {
    return false;
  }
  return ps->stop();
}


bool GstPipelineManager::loadRecord(int card,int port,
                                    int coding,int chans,
                                    int samprate,int bitrate,
                                    const QString &wavename)
{
  if(card!=d_card_number||port<0||port>=RD_MAX_PORTS) {
    return false;
  }
  if(d_record_streams[port]!=NULL) {
    d_record_active[port]=false;
    delete d_record_streams[port];
    d_record_streams[port]=NULL;
  }

  jack_ringbuffer_reset(d_record_rb[port]);

  GstRecordStream *rs=
    new GstRecordStream(card,port,d_record_rb[port],
                        d_jack_rate,this);
  connect(rs,SIGNAL(recordStateChanged(int,int,int)),
          this,
          SLOT(streamRecordStateChanged(int,int,int)));

  if(!rs->loadRecord(coding,chans,samprate,bitrate,
                     wavename)) {
    delete rs;
    return false;
  }
  d_record_streams[port]=rs;
  rda->syslog(LOG_DEBUG,
              "GstPipelineManager: loadRecord "
              "card=%d port=%d coding=%d "
              "chans=%d rate=%d bitrate=%d",
              card,port,coding,chans,
              samprate,bitrate);
  return true;
}


bool GstPipelineManager::unloadRecord(int card,int port,
                                      unsigned *len_frames,
                                      RDTempDirectory **take,
                                      QString *take_wav)
{
  if(take!=NULL) {
    *take=NULL;
  }
  rda->syslog(LOG_DEBUG,
    "DBG GstPipelineManager::unloadRecord: ENTER "
    "card=%d port=%d",card,port);
  GstRecordStream *rs=FindRecordStream(card,port);
  if(rs==NULL) {
    rda->syslog(LOG_DEBUG,
      "DBG GstPipelineManager::unloadRecord: "
      "rs==NULL, returning false");
    if(len_frames) {
      *len_frames=0;
    }
    return false;
  }
  d_record_active[port]=false;
  bool ok=rs->unloadRecord(len_frames);
  unsigned frames_val=len_frames?*len_frames:0;
  rda->syslog(LOG_DEBUG,
    "DBG GstPipelineManager::unloadRecord: "
    "rs->unloadRecord returned ok=%d frames=%u",
    ok,frames_val);
  rda->syslog(LOG_DEBUG,
    "DBG GstPipelineManager::unloadRecord: "
    "about to delete rs");
  if((take!=NULL)&&(take_wav!=NULL)) {
    *take=rs->takeTake(take_wav);
  }
  delete rs;
  rda->syslog(LOG_DEBUG,
    "DBG GstPipelineManager::unloadRecord: "
    "delete rs done");
  d_record_streams[port]=NULL;
  jack_ringbuffer_reset(d_record_rb[port]);
  rda->syslog(LOG_DEBUG,
    "DBG GstPipelineManager::unloadRecord: "
    "returning ok=%d",ok);
  return ok;
}


bool GstPipelineManager::record(int card,int port,
                                int length,int thres)
{
  GstRecordStream *rs=FindRecordStream(card,port);
  if(rs==NULL) {
    return false;
  }
  if(rs->record(length,thres)) {
    d_record_active[port]=true;
    return true;
  }
  return false;
}


bool GstPipelineManager::stopRecord(int card,int port)
{
  GstRecordStream *rs=FindRecordStream(card,port);
  if(rs==NULL) {
    return false;
  }
  d_record_active[port]=false;
  return rs->stopRecord();
}


bool GstPipelineManager::setClockSource(int card,int src)
{
  (void)card;
  (void)src;
  return true;
}


bool GstPipelineManager::setInputVolume(int card,int stream,
                                        int level)
{
  (void)card;
  (void)stream;
  (void)level;
  return true;
}


bool GstPipelineManager::setOutputVolume(int card,int stream,
                                         int port,int level)
{
  if(card!=d_card_number||stream<0||
     stream>=RD_MAX_STREAMS||
     port<0||port>=RD_MAX_PORTS) {
    return false;
  }
  if(d_fade_state[stream].active&&d_fade_state[stream].port==port) {
    d_fade_state[stream].active=false;
  }
  d_output_vol[stream][port]=(float)CentibelsToLinear(level);
  rda->syslog(LOG_DEBUG,
              "GstPipelineManager: setOutputVolume "
              "stream=%d port=%d level=%dcB "
              "linear=%.4f",
              stream,port,level,
              d_output_vol[stream][port]);
  return true;
}


bool GstPipelineManager::fadeOutputVolume(int card,int stream,
                                          int port,int level,
                                          int length)
{
  if(card!=d_card_number||stream<0||
     stream>=RD_MAX_STREAMS||
     port<0||port>=RD_MAX_PORTS) {
    return false;
  }
  int steps=length/10;
  if(steps<=0) {
    return setOutputVolume(card,stream,port,level);
  }
  d_fade_state[stream].active=true;
  d_fade_state[stream].port=port;
  d_fade_state[stream].from_linear=(float)d_output_vol[stream][port];
  d_fade_state[stream].to_linear=(float)CentibelsToLinear(level);
  d_fade_state[stream].steps_total=steps;
  d_fade_state[stream].steps_done=0;
  if(!d_fade_timer->isActive()) {
    d_fade_timer->start(10);
  }
  rda->syslog(LOG_DEBUG,
              "GstPipelineManager: fadeOutputVolume "
              "stream=%d port=%d level=%dcB "
              "from=%.4f to=%.4f steps=%d",
              stream,port,level,
              d_fade_state[stream].from_linear,
              d_fade_state[stream].to_linear,steps);
  return true;
}


void GstPipelineManager::FadeTimerData()
{
  bool any_active=false;
  for(int s=0;s<RD_MAX_STREAMS;s++) {
    if(!d_fade_state[s].active) {
      continue;
    }
    any_active=true;
    d_fade_state[s].steps_done++;
    float t=(float)d_fade_state[s].steps_done/
             (float)d_fade_state[s].steps_total;
    if(t>=1.0f) {
      t=1.0f;
      d_fade_state[s].active=false;
    }
    int p=d_fade_state[s].port;
    d_output_vol[s][p]=d_fade_state[s].from_linear+
                        t*(d_fade_state[s].to_linear-
                           d_fade_state[s].from_linear);
  }
  if(!any_active) {
    d_fade_timer->stop();
  }
}


bool GstPipelineManager::setInputLevel(int card,int port,
                                       int level)
{
  (void)card;
  (void)port;
  (void)level;
  return true;
}


bool GstPipelineManager::setOutputLevel(int card,int port,
                                        int level)
{
  (void)card;
  (void)port;
  (void)level;
  return true;
}


bool GstPipelineManager::setInputMode(int card,int stream,
                                      int mode)
{
  (void)card;
  (void)stream;
  (void)mode;
  return true;
}


bool GstPipelineManager::setOutputMode(int card,int stream,
                                       int mode)
{
  (void)card;
  (void)stream;
  (void)mode;
  return true;
}


bool GstPipelineManager::setInputVoxLevel(int card,int stream,
                                          int level)
{
  (void)card;
  (void)stream;
  (void)level;
  return true;
}


bool GstPipelineManager::setInputType(int card,int port,
                                      int type)
{
  (void)card;
  (void)port;
  (void)type;
  return true;
}


bool GstPipelineManager::getInputStatus(int card,int port)
{
  (void)card;
  (void)port;
  return false;
}


bool GstPipelineManager::setPassthroughLevel(int card,
                                              int in_port,
                                              int out_port,
                                              int level)
{
  if(card!=d_card_number||in_port<0||
     in_port>=RD_MAX_PORTS||
     out_port<0||out_port>=RD_MAX_PORTS) {
    return false;
  }
  float linear=(float)CentibelsToLinear(level);
  if(linear>0.0f) {
    //
    // Block feedback before it starts: a mix-minus this path would feed
    // back into is muted, and has fallen silent, before the path opens
    //
    uint32_t was=d_mm_blocked.load();
    CheckGraph(in_port,out_port,linear);
    if(((d_mm_blocked.load()&~was)!=0)&&(d_jack_client!=NULL)) {
      unsigned start=d_cycles.load();
      for(int i=0;(i<500)&&((d_cycles.load()-start)<3);i++) {
        usleep(1000);
      }
    }
  }
  d_passthrough[in_port][out_port]=linear;
  if(linear<=0.0f) {
    d_mm_rt_blocked.store(0);
    CheckGraph();
  }
  rda->syslog(LOG_DEBUG,
              "GstPipelineManager: passthrough "
              "in=%d out=%d level=%dcB",
              in_port,out_port,level);
  return true;
}


bool GstPipelineManager::getInputMeters(int card,int port,
                                        short levels[2])
{
  if(card!=d_card_number||port<0||
     port>=d_input_ports) {
    return false;
  }
  for(int i=0;i<2;i++) {
    float peak=d_input_peak[port][i];
    if(peak<0.000001f) {
      levels[i]=-10000;
    }
    else {
      double db=20.0*log10((double)peak);
      if(db<-100.0) {
        levels[i]=-10000;
      }
      else {
        levels[i]=(short)(db*100.0);
      }
    }
  }
  return true;
}


bool GstPipelineManager::getOutputMeters(int card,int port,
                                         short levels[2])
{
  if(card!=d_card_number||port<0||
     port>=d_output_ports) {
    return false;
  }
  for(int i=0;i<2;i++) {
    float peak=d_output_peak[port][i];
    if(peak<0.000001f) {
      levels[i]=-10000;
    }
    else {
      double db=20.0*log10((double)peak);
      if(db<-100.0) {
        levels[i]=-10000;
      }
      else {
        levels[i]=(short)(db*100.0);
      }
    }
  }
  return true;
}


bool GstPipelineManager::getStreamOutputMeters(
  int card,int stream,short levels[2])
{
  GstPlaybackStream *ps=FindPlayStream(card,stream);
  if(ps==NULL) {
    return false;
  }
  return ps->getStreamOutputMeters(levels);
}


void GstPipelineManager::getOutputPosition(int card,
                                           unsigned *pos)
{
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    pos[i]=0;
    if(d_play_streams[i]!=NULL&&
       d_play_streams[i]->card()==card) {
      pos[i]=d_play_streams[i]->position();
    }
  }
}


void GstPipelineManager::streamPlayStateChanged(
  int card,int stream,int state)
{
  if(state==0) {
    d_play_active[stream]=false;
  }
  rda->syslog(LOG_DEBUG,
              "GstPipelineManager: playStateChanged "
              "card=%d stream=%d state=%d "
              "active=%d",
              card,stream,state,
              d_play_active[stream]);
  emit playStateChanged(card,stream,state);
}


void GstPipelineManager::streamRecordStateChanged(
  int card,int port,int state)
{
  if(state==3) {
    d_record_active[port]=false;
  }
  emit recordStateChanged(card,port,state);
}


int GstPipelineManager::setMixMinus(int card,QList<MixMinus> *mms)
{
  //
  // Validate.  Ports count from 0 here; messages count from 1, as
  // RDAdmin shows them.
  //
  uint32_t outputs=0;
  for(int i=0;i<mms->size();i++) {
    MixMinus *m=&(*mms)[i];
    m->reason="";
    m->state='R';
    if(card!=d_card_number) {
      m->reason=QString::asprintf("card %d is not this audio engine's card",
                                  card);
      continue;
    }
    if(!m->enabled) {
      m->state='D';
      continue;
    }
    if((m->output<0)||(m->output>=d_output_ports)) {
      m->reason=QString::asprintf("Output %d doesn't exist (the card has %d)",
                                  m->output+1,d_output_ports);
      continue;
    }
    if((m->source<0)||(m->source>=d_output_ports)) {
      m->reason=QString::asprintf("source Output %d doesn't exist "
                                  "(the card has %d)",
                                  m->source+1,d_output_ports);
      continue;
    }
    if(m->output==m->source) {
      m->reason=QString::asprintf("Output %d can't take its own sum",
                                  m->output+1);
      continue;
    }
    QList<int> excluded;
    for(int j=0;j<m->excluded.size();j++) {
      int in=m->excluded.at(j);
      if((in>=0)&&(in<d_input_ports)&&(!excluded.contains(in))) {
        excluded.push_back(in);
      }
    }
    m->excluded=excluded;
    if(excluded.size()==0) {
      m->reason="it excludes no input";
      continue;
    }
    if((m->level<-10000)||(m->level>1000)) {
      m->reason=QString::asprintf("level %.1f dB is outside -100 to +10 dB",
                                  (double)m->level/100.0);
      continue;
    }
    if((outputs&(1u<<m->output))!=0) {
      m->reason=QString::asprintf("Output %d already has a mix-minus",
                                  m->output+1);
      continue;
    }
    outputs|=(1u<<m->output);
    m->state='A';
  }
  for(int i=0;i<mms->size();i++) {   // No mix-minus of a mix-minus
    MixMinus *m=&(*mms)[i];
    if((m->state=='A')&&((outputs&(1u<<m->source))!=0)) {
      m->state='R';
      m->reason=QString::asprintf("its source Output %d is a mix-minus",
                                  m->source+1);
    }
  }

  //
  // Build the new configuration in the copy the RT thread isn't using
  //
  int next=1-d_mm_current.load();
  MixMinusConfig *cfg=&d_mm_config[next];
  memset(cfg,0,sizeof(MixMinusConfig));
  for(int i=0;i<mms->size();i++) {
    const MixMinus &m=mms->at(i);
    if(m.state!='A') {
      continue;
    }
    cfg->output[cfg->count]=m.output;
    cfg->source[cfg->count]=m.source;
    for(int j=0;j<m.excluded.size();j++) {
      cfg->excluded[cfg->count]|=(1u<<m.excluded.at(j));
    }
    cfg->gain[cfg->count]=(float)CentibelsToLinear(m.level);
    cfg->dedicated[m.output]=true;
    cfg->count++;
  }
  d_mm_rows=*mms;
  d_mm_rt_blocked.store(0);
  CheckGraph(-1,-1,0.0f,cfg);   // Blocked before the new one is heard
  d_mm_current.store(next);

  //
  // Wait until the RT thread has run two cycles on it, so the copy it
  // left is free for the next change
  //
  if(d_jack_client!=NULL) {
    unsigned start=d_cycles.load();
    for(int i=0;(i<500)&&((d_cycles.load()-start)<2);i++) {
      usleep(1000);
    }
  }
  return cfg->count;
}


QList<GstPipelineManager::MixMinus> GstPipelineManager::mixMinus(int card) const
{
  QList<MixMinus> ret;
  if(card!=d_card_number) {
    return ret;
  }
  uint32_t blocked=d_mm_blocked.load();
  for(int i=0;i<d_mm_rows.size();i++) {
    MixMinus m=d_mm_rows.at(i);
    if((m.state=='A')&&((blocked&(1u<<m.output))!=0)) {
      m.state='B';
      m.reason=d_mm_block_reason[m.output];
    }
    ret.push_back(m);
  }
  return ret;
}


void GstPipelineManager::JackPortConnect(jack_port_id_t a,jack_port_id_t b,
                                         int connect,void *arg)
{
  (void)a;
  (void)b;
  GstPipelineManager *mgr=static_cast<GstPipelineManager *>(arg);
  if(!connect) {
    mgr->d_mm_rt_blocked.store(0);
  }
  if(connect) {
    //
    // Block a feedback path at once; unblocking (and logging) waits for
    // the Qt thread's check
    //
    uint32_t add=mgr->Blocked(-1,-1,0.0f,NULL,NULL);
    if(add!=0) {
      mgr->d_mm_blocked.fetch_or(add);
    }
  }
  mgr->d_graph_changed.store(true);
}


void GstPipelineManager::GraphTimerData()
{
  if(d_graph_changed.exchange(false)) {
    CheckGraph();
  }
}


bool GstPipelineManager::Connected(jack_port_t *out_port,
                                   jack_port_t *in_port) const
{
  if((out_port==NULL)||(in_port==NULL)) {
    return false;
  }
  return jack_port_connected_to(in_port,jack_port_name(out_port))!=0;
}


uint32_t GstPipelineManager::Blocked(int in_port,int out_port,float level,
                                     const MixMinusConfig *cfg,
                                     QString *reasons) const
{
  //
  // The mix-minus outputs to block: a mix-minus is blocked while its
  // output is connected straight back into an input that reaches its
  // source and isn't excluded, since the source would hear itself again,
  // cycle after cycle.  With in_port/out_port, as though that
  // passthrough were already at level.  Reads only; safe from JACK's
  // notification thread.
  //
  uint32_t blocked=0;
  if(d_jack_client==NULL) {
    return 0;
  }
  const MixMinusConfig *mm=
    (cfg!=NULL)?cfg:&d_mm_config[d_mm_current.load()];
  for(int m=0;m<mm->count;m++) {
    int o=mm->output[m];
    int src=mm->source[m];
    for(int ip=0;ip<d_input_ports;ip++) {
      if((mm->excluded[m]&(1u<<ip))!=0) {
        continue;
      }
      float lv=((ip==in_port)&&(src==out_port))?level:d_passthrough[ip][src];
      if(lv<=0.0f) {
        continue;
      }
      bool fed=false;
      for(int a=0;a<2;a++) {
        for(int b=0;b<2;b++) {
          fed=fed||Connected(d_jack_out[o][a],d_jack_in[ip][b]);
        }
      }
      if(fed) {
        if(((blocked&(1u<<o))==0)&&(reasons!=NULL)) {
          reasons[o]=
            QString::asprintf("Output %d is connected to Input %d, which "
                              "reaches its source Output %d",
                              o+1,ip+1,src+1);
        }
        blocked|=(1u<<o);
      }
    }
  }
  return blocked;
}


void GstPipelineManager::CheckGraph(int in_port,int out_port,float level,
                                    const MixMinusConfig *cfg)
{
  //
  // Called from the Qt thread whenever the JACK graph, a passthrough
  // or the mix-minus configuration changes.  With in_port/out_port,
  // checks as though that passthrough were already at level.
  //
  if(d_jack_client==NULL) {
    return;
  }
  uint32_t self=0;
  for(int ip=0;(ip<d_input_ports)&&(ip<d_output_ports);ip++) {
    if(Connected(d_jack_out[ip][0],d_jack_in[ip][0])||
       Connected(d_jack_out[ip][1],d_jack_in[ip][1])) {
      self|=(1u<<ip);
    }
  }
  d_self_loop.store(self);

  QString reasons[RD_MAX_PORTS];
  uint32_t blocked=Blocked(in_port,out_port,level,cfg,reasons);
  for(int o=0;o<d_output_ports;o++) {
    if((blocked&(1u<<o))!=0) {
      d_mm_block_reason[o]=reasons[o];
    }
  }
  //
  // Blocks the RT thread found by the audio itself hold until something
  // changes (a disconnection, a passthrough closed, a reload): a loop
  // through another client isn't in the graph, and releasing it would
  // only let it feed back again
  //
  uint32_t rt=d_mm_rt_blocked.load()&~blocked;
  for(int o=0;o<d_output_ports;o++) {
    if((rt&(1u<<o))!=0) {
      d_mm_block_reason[o]=QString::asprintf("its own audio came back on an "
                                             "input that reaches its source");
    }
  }
  blocked|=rt;
  d_mm_blocked.store(blocked);
  uint32_t was=d_mm_logged;
  d_mm_logged=blocked;
  for(int o=0;o<d_output_ports;o++) {
    uint32_t bit=1u<<o;
    if(((blocked&bit)!=0)&&((was&bit)==0)) {
      rda->syslog(LOG_WARNING,"GstPipelineManager: mix-minus on Output %d "
                  "muted: %s",o+1,d_mm_block_reason[o].toUtf8().constData());
    }
    if(((blocked&bit)==0)&&((was&bit)!=0)) {
      rda->syslog(LOG_INFO,"GstPipelineManager: mix-minus on Output %d "
                  "no longer muted",o+1);
    }
  }
}


double GstPipelineManager::CentibelsToLinear(int cb)
{
  if(cb<=-10000) {
    return 0.0;
  }
  return pow(10.0,(double)cb/2000.0);
}


GstPlaybackStream *GstPipelineManager::FindPlayStream(
  int card,int stream) const
{
  if(card!=d_card_number||stream<0||
     stream>=RD_MAX_STREAMS) {
    return NULL;
  }
  return d_play_streams[stream];
}


GstRecordStream *GstPipelineManager::FindRecordStream(
  int card,int port) const
{
  if(card!=d_card_number||port<0||port>=RD_MAX_PORTS) {
    return NULL;
  }
  return d_record_streams[port];
}
