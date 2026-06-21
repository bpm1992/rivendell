// gst_record_stream.h
//
// GStreamer-based recording pipeline for caed(8)
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

#ifndef GST_RECORD_STREAM_H
#define GST_RECORD_STREAM_H

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <jack/ringbuffer.h>

#include <QObject>
#include <QString>
#include <QTimer>

#include <rdwavefile.h>

class GstRecordStream : public QObject
{
  Q_OBJECT
 public:
  enum State {
    Idle=0,
    Armed=1,
    Recording=2,
    Stopping=3
  };
  GstRecordStream(int card,int port,
                  jack_ringbuffer_t *ringbuffer,
                  unsigned jack_rate,
                  QObject *parent=0);
  ~GstRecordStream();
  int card() const;
  int port() const;
  State state() const;
  bool loadRecord(int coding,int chans,int samprate,
                  int bitrate,const QString &filename);
  bool record(int length_ms,int threshold_cb);
  bool stopRecord();
  bool unloadRecord(unsigned *frames);
  bool getInputMeters(short levels[2]);

 signals:
  void recordStateChanged(int card,int port,int state);

 private slots:
  void busTimerData();
  void stopTimerData();

 private:
  bool BuildPipeline(int coding,int chans,int samprate,
                     int bitrate,const QString &filename);
  void DestroyPipeline();
  void FeedFromRingbuffer();
  void WrapMpegToWav();
  void CopyToDestination();
  const char *EncoderName(int coding) const;

  int d_card;
  int d_port;
  jack_ringbuffer_t *d_ringbuffer;
  unsigned d_jack_rate;
  State d_state;
  GstElement *d_pipeline;
  GstElement *d_src;
  GstElement *d_audioconvert;
  GstElement *d_audioresample;
  GstElement *d_capsfilter;
  GstElement *d_level;
  GstElement *d_encoder;
  GstElement *d_filesink;
  QTimer *d_bus_timer;
  QTimer *d_stop_timer;
  unsigned d_sample_rate;
  short d_meter_levels[2];
  int d_coding;
  int d_channels;
  int d_bitrate;
  unsigned d_recorded_frames;
  QString d_dest_filename;
  QString d_temp_mpeg_path;
  QString d_temp_wav_path;
};

#endif  // GST_RECORD_STREAM_H
