// gst_pipeline_manager.h
//
// GStreamer pipeline manager for caed(8)
//
// Replaces the legacy Driver abstraction with GStreamer-based
// audio I/O.  Exposes the same interface as Driver so that
// cae.cpp requires minimal changes.
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

#ifndef GST_PIPELINE_MANAGER_H
#define GST_PIPELINE_MANAGER_H

#include <stdint.h>

#include <atomic>

#include <gst/gst.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <rd.h>

#include "gst_playback_stream.h"
#include "gst_record_stream.h"

//
// Ringbuffer size in bytes.  1MB per buffer ≈ 3 seconds
// at 44.1kHz stereo float.  Must be large enough to
// absorb scheduling jitter between GStreamer decode and
// JACK consume threads, and to pre-fill enough audio
// for glitch-free playback start over network sources.
// 48 streams × 1MB = 48MB total (pre-allocated).
//
#define GST_JACK_RB_SIZE 1048576

//
// Maximum JACK frames per process callback (for stack buffers)
//
#define GST_JACK_MAX_FRAMES 8192

class GstPipelineManager : public QObject
{
  Q_OBJECT
 public:
  GstPipelineManager(QObject *parent=0);
  ~GstPipelineManager();

  //
  // Configuration / initialization
  //
  bool initialize();
  bool hasCard(int card) const;
  int cardNumber() const;
  int inputPortQuantity(int card) const;
  int outputPortQuantity(int card) const;
  bool timescaleSupported(int card) const;
  QString version() const;

  //
  // Playback
  //
  bool loadPlayback(int card,const QString &wavename,int *stream);
  bool unloadPlayback(int card,int stream);
  bool playbackPosition(int card,int stream,unsigned pos);
  bool play(int card,int stream,int length,int speed,
            bool pitch,bool rates);
  bool stopPlayback(int card,int stream);

  //
  // Recording
  //
  bool loadRecord(int card,int port,int coding,int chans,
                  int samprate,int bitrate,
                  const QString &wavename);
  bool unloadRecord(int card,int port,unsigned *len_frames,
                    RDTempDirectory **take=NULL,QString *take_wav=NULL);
  bool record(int card,int port,int length,int thres);
  bool stopRecord(int card,int port);

  //
  // Mixer / routing
  //
  bool setClockSource(int card,int src);
  bool setInputVolume(int card,int stream,int level);
  bool setOutputVolume(int card,int stream,int port,int level);
  bool fadeOutputVolume(int card,int stream,int port,
                        int level,int length);
  bool setInputLevel(int card,int port,int level);
  bool setOutputLevel(int card,int port,int level);
  bool setInputMode(int card,int stream,int mode);
  bool setOutputMode(int card,int stream,int mode);
  bool setInputVoxLevel(int card,int stream,int level);
  bool setInputType(int card,int port,int type);
  bool getInputStatus(int card,int port);
  bool setPassthroughLevel(int card,int in_port,
                           int out_port,int level);

  //
  // Metering
  //
  bool getInputMeters(int card,int port,short levels[2]);
  bool getOutputMeters(int card,int port,short levels[2]);
  bool getStreamOutputMeters(int card,int stream,
                             short levels[2]);
  void getOutputPosition(int card,unsigned *pos);

  //
  // Mix-minus: an output port that carries the full sum of another
  // output port (every stream and passthrough mixed into it, before
  // that port's output level) less the contributions of chosen input
  // ports, at its own level.  Ports here count from 0.
  //
  class MixMinus {
   public:
    MixMinus();
    int output;
    int source;
    QList<int> excluded;
    int level;              // hundredths of a dB, -10000 = mute
    bool enabled;
    char state;             // 'A' active, 'D' disabled, 'B' blocked, 'R' refused
    QString reason;         // why it was refused or is blocked
  };
  int setMixMinus(int card,QList<MixMinus> *mms);
  QList<MixMinus> mixMinus(int card) const;

 signals:
  void playStateChanged(int card,int stream,int state);
  void recordStateChanged(int card,int stream,int state);

 private slots:
  void streamPlayStateChanged(int card,int stream,int state);
  void streamRecordStateChanged(int card,int port,int state);
  void FadeTimerData();
  void GraphTimerData();

 private:
  static int JackProcess(jack_nframes_t nframes,void *arg);
  static void JackPortConnect(jack_port_id_t a,jack_port_id_t b,
                              int connect,void *arg);

  bool Connected(jack_port_t *out_port,jack_port_t *in_port) const;
  static double CentibelsToLinear(int cb);
  GstPlaybackStream *FindPlayStream(int card,int stream) const;
  GstRecordStream *FindRecordStream(int card,int port) const;

  //
  // Per-stream volume fade state (one active fade per stream).
  // Written/read from Qt thread only (FadeTimerData, fadeOutputVolume,
  // setOutputVolume, unloadPlayback).  d_output_vol is volatile so
  // the JACK RT callback always sees the latest value.
  //
  struct FadeState {
    bool active;
    int port;
    float from_linear;
    float to_linear;
    int steps_total;
    int steps_done;
  };
  FadeState d_fade_state[RD_MAX_STREAMS];
  QTimer *d_fade_timer;

  //
  // Configuration
  //
  int d_card_number;
  int d_input_ports;
  int d_output_ports;

  //
  // JACK client and ports
  //
  jack_client_t *d_jack_client;
  unsigned d_jack_rate;
  jack_port_t *d_jack_out[RD_MAX_PORTS][2];
  jack_port_t *d_jack_in[RD_MAX_PORTS][2];

  //
  // Ringbuffers: playback (stream→JACK) and record (JACK→stream)
  //
  jack_ringbuffer_t *d_play_rb[RD_MAX_STREAMS];
  jack_ringbuffer_t *d_record_rb[RD_MAX_PORTS];

  //
  // Mixer state (read by JACK RT thread, written by Qt thread)
  // Volume in linear gain.  0.0 = muted.
  //
  volatile float d_output_vol[RD_MAX_STREAMS][RD_MAX_PORTS];
  volatile float d_passthrough[RD_MAX_PORTS][RD_MAX_PORTS];
  volatile bool d_play_active[RD_MAX_STREAMS];
  volatile bool d_play_draining[RD_MAX_STREAMS];
  volatile bool d_record_active[RD_MAX_PORTS];

  //
  // Previous output buffer cache for PipeWire self-loopback.
  // When PipeWire connects an output to an input on the same
  // client, the input buffer may be empty.  We cache the
  // previous cycle's output as a fallback source.
  //
  float d_prev_output[RD_MAX_PORTS][2][GST_JACK_MAX_FRAMES];
  unsigned d_prev_output_frames;

  //
  // Input buffers, copied at the start of each cycle before the
  // outputs are zeroed: JACK may hand an input connected to one of
  // our own outputs that output's buffer, so a loop through our own
  // ports carries the previous cycle as JACK intends.
  //
  float d_input_copy[RD_MAX_PORTS][2][GST_JACK_MAX_FRAMES];
  float d_input_prev[RD_MAX_PORTS][2][GST_JACK_MAX_FRAMES];   // last cycle's copy

  //
  // Input ports connected to our own output port of the same number
  // (bit per input port).  Only those may use the previous-cycle
  // fallback above; any other silent input is just silent.  Set from
  // the Qt thread when the JACK graph changes.
  //
  std::atomic<uint32_t> d_self_loop;

  //
  // Mix-minus configuration for the RT thread: two copies, swapped by
  // index so the RT thread never sees one being written.
  //
  struct MixMinusConfig {
    int count;
    int output[RD_MAX_PORTS];
    int source[RD_MAX_PORTS];
    uint32_t excluded[RD_MAX_PORTS];
    float gain[RD_MAX_PORTS];
    bool dedicated[RD_MAX_PORTS];    // by output port
  };
  MixMinusConfig d_mm_config[2];
  void CheckGraph(int in_port=-1,int out_port=-1,float level=0.0f,
                  const MixMinusConfig *cfg=NULL);
  uint32_t Blocked(int in_port,int out_port,float level,
                   const MixMinusConfig *cfg,QString *reasons) const;
  std::atomic<int> d_mm_current;
  std::atomic<uint32_t> d_mm_blocked;   // bit per mix-minus output port
  std::atomic<uint32_t> d_mm_rt_blocked;  // found by the RT thread, held
  std::atomic<unsigned> d_cycles;
  std::atomic<bool> d_graph_changed;
  float d_mm_gain_now[RD_MAX_PORTS];    // RT thread only, by output port
  float d_mm_excluded[RD_MAX_PORTS][2][GST_JACK_MAX_FRAMES];
  QList<MixMinus> d_mm_rows;
  QString d_mm_block_reason[RD_MAX_PORTS];
  uint32_t d_mm_logged;                 // Blocks logged, Qt thread only
  QTimer *d_graph_timer;

  //
  // Output port peak meters (written by JACK RT thread,
  // read by Qt thread via getOutputMeters).
  // Stored as linear peak amplitude (0.0 .. 1.0+).
  //
  volatile float d_output_peak[RD_MAX_PORTS][2];
  volatile float d_input_peak[RD_MAX_PORTS][2];

  //
  // Per-stream frame counter: incremented in JACK RT
  // callback after reading frames from the ringbuffer.
  // Read by GstPlaybackStream::position() for precise
  // playout position tracking.
  //
  volatile uint64_t d_play_frames_out[RD_MAX_STREAMS];

  //
  // Streams
  //
  GstPlaybackStream *d_play_streams[RD_MAX_STREAMS];
  GstRecordStream *d_record_streams[RD_MAX_PORTS];
};

#endif  // GST_PIPELINE_MANAGER_H
