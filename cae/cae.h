// cae.h
//
// The Core Audio Engine component of Rivendell
//
//   (C) Copyright 2002-2025 Fred Gleason <fredg@paravelsystems.com>
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

#ifndef CAE_H
#define CAE_H

#include <sys/types.h>
#include <pthread.h>
#include <stdint.h>

#include <QTimer>
#include <QObject>
#include <QProcess>
#include <QUdpSocket>

#include <rdwavefile.h>

#include <rd.h>
#include <rdconfig.h>
#include <rdstation.h>

#include "cae_server.h"
#include "gst_pipeline_manager.h"
#include "playsession.h"

#ifndef HAVE_SRC_CONV
void src_int_to_float_array (const int *in, float *out, int len);
void src_float_to_int_array (const float *in, int *out, int len);
#endif  // HAVE_SRC_CONV

//
// Global CAE Definitions
//
#define CAED_USAGE "[-d]\n\nSupplying the '-d' flag will set 'debug' mode, causing caed(8) to stay\nin the foreground and print debugging info on standard output.\n"

//
// Function Prototypes
//
void SigHandler(int signum);

class MainObject : public QObject
{
  Q_OBJECT
 public:
  MainObject(QObject *parent=0);

 private slots:
   void loadPlaybackData(uint64_t phandle,unsigned card,unsigned portnum,
			 const QString &name);
  void unloadPlaybackData(uint64_t phandle);
  void playPositionData(uint64_t phandle,unsigned pos);
  void playData(uint64_t phandle,unsigned length,unsigned speed,
		unsigned pitch_flag);
  void stopPlaybackData(uint64_t phandle);
  void timescalingSupportData(int id,unsigned card);
  void loadRecordingData(int id,unsigned card,unsigned port,unsigned coding,
			unsigned channels,unsigned samprate,unsigned bitrate,
			const QString &name);
  void unloadRecordingData(int id,unsigned card,unsigned stream);
  void recordData(int id,unsigned card,unsigned stream,unsigned len,
		 int threshold_level);
  void stopRecordingData(int id,unsigned card,unsigned stream);
  void setInputVolumeData(int id,unsigned card,unsigned stream,int level);
  void setOutputVolumeData(uint64_t phandle,int level);
  void fadeOutputVolumeData(uint64_t phandle,int level,unsigned length);
  void setInputLevelData(int id,unsigned card,unsigned stream,int level);
  void setOutputLevelData(int id,unsigned card,unsigned port,int level);
  void setInputModeData(int id,unsigned card,unsigned stream,unsigned mode);
  void setOutputModeData(int id,unsigned card,unsigned stream,unsigned mode);
  void setInputVoxLevelData(int id,unsigned card,unsigned stream,int level);
  void setInputTypeData(int id,unsigned card,unsigned port,unsigned type);
  void getInputStatusData(int id,unsigned card,unsigned port);
  void setAudioPassthroughLevelData(int id,unsigned card,unsigned input,
				    unsigned output,int level);
  void setClockSourceData(int id,unsigned card,int input);
  void meterEnableData(int id,uint16_t udp_port,const QList<unsigned> &cards);
  void statePlayUpdate(int card,int stream,int state);
  void stateRecordUpdate(int card,int stream,int state);
  void updateMeters();
  void connectionDroppedData(int id);

 private:
  void InitProvisioning() const;
  void InitMixers();
  void KillSocket(int);
  bool CheckDaemon(QString);
  pid_t GetPid(QString pidfile);
  PlaySession *GetPlaySession(unsigned card,unsigned stream) const;
  uint64_t GetPlayHandle(unsigned cardnum,unsigned streamnum) const;
  void ProbeCaps(RDStation *station);
  void ClearDriverEntries() const;
  void SendMeterLevelUpdate(const QString &type,int cardnum,int portnum,
			    short levels[]);
  void SendStreamMeterLevelUpdate(PlaySession *psess,short levels[]);
  void SendMeterPositionUpdate(int cardnum,unsigned pos[]);
  void SendMeterUpdate(const QString &msg,int conn_id);
  bool CheckMp4Decode();
  GstPipelineManager *d_gst_mgr;
  bool debug;
  unsigned system_sample_rate;
  CaeServer *cae_server;
  int16_t tcp_port;
  QUdpSocket *meter_socket;
  int record_owner[RD_MAX_CARDS][RD_MAX_STREAMS];
  int record_length[RD_MAX_CARDS][RD_MAX_STREAMS];
  int record_threshold[RD_MAX_CARDS][RD_MAX_STREAMS];
  int play_length[RD_MAX_CARDS][RD_MAX_STREAMS];
  int play_speed[RD_MAX_CARDS][RD_MAX_STREAMS];
  bool play_pitch[RD_MAX_CARDS][RD_MAX_STREAMS];
  bool port_status[RD_MAX_CARDS][RD_MAX_PORTS];
  bool output_status_flag[RD_MAX_CARDS][RD_MAX_PORTS][RD_MAX_STREAMS];
  unsigned cae_play_serial[RD_MAX_CARDS][RD_MAX_STREAMS];
  QMap<uint64_t,PlaySession *> play_sessions;
};


#endif  // CAE_H
