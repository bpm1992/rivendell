// gst_playback_stream.h
//
// GStreamer-based playback pipeline for caed(8)
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

#ifndef GST_PLAYBACK_STREAM_H
#define GST_PLAYBACK_STREAM_H

#include <stdint.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <jack/ringbuffer.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QTime>

class GstPlaybackStream : public QObject
{
  Q_OBJECT
 public:
  enum State {
    Stopped=0,
    Loading=1,
    Loaded=2,
    Playing=3,
    Stopping=4,
    Finished=5,
    Error=6,
    Draining=7
  };
  GstPlaybackStream(int card,int stream,
                    jack_ringbuffer_t *ringbuffer,
                    unsigned jack_rate,
                    volatile uint64_t *frames_played,
                    volatile bool *draining_flag,
                    volatile bool *play_active,
                    QObject *parent=0);
  ~GstPlaybackStream();
  int card() const;
  int stream() const;
  State state() const;
  bool load(const QString &uri);
  bool play(int length,int speed,bool pitch);
  bool stop();
  bool seek(unsigned pos_ms);
  void setOutputVolume(int level_cb);
  void fadeOutputVolume(int level_cb,int length_ms);
  unsigned position() const;
  bool getStreamOutputMeters(short levels[2]);

 signals:
  void playStateChanged(int card,int stream,int state);

 private slots:
  void busTimerData();
  void stopTimerData();

 private:
  static void onPadAdded(GstElement *element,GstPad *pad,
                         gpointer data);
  static GstFlowReturn onNewSample(GstAppSink *sink,
                                   gpointer data);
  bool BuildPipeline(const QString &uri);
  void DestroyPipeline();
  void ApplyVolume();
  double CentibelsToLinear(int cb) const;

  int d_card;
  int d_stream;
  jack_ringbuffer_t *d_ringbuffer;
  unsigned d_jack_rate;
  State d_state;
  QString d_uri;
  GstElement *d_pipeline;
  GstElement *d_uridecodebin;
  GstElement *d_audioconvert;
  GstElement *d_audioresample;
  GstElement *d_scaletempo;
  GstElement *d_volume;
  GstElement *d_level;
  GstElement *d_sink;
  QTimer *d_bus_timer;
  QTimer *d_stop_timer;
  int d_output_level;
  short d_meter_levels[2];
  bool d_pad_linked;
  volatile uint64_t *d_frames_played_ptr;
  volatile bool *d_draining_ptr;
  volatile bool *d_play_active_ptr;
  unsigned d_start_pos_ms;
  double d_play_rate;
  unsigned d_target_stop_ms;
  QTime d_drain_start_time;
  volatile bool d_shutting_down;
  volatile bool d_seeking;
  volatile size_t d_onsample_bytes;
  volatile size_t d_onsample_calls;
};

#endif  // GST_PLAYBACK_STREAM_H
