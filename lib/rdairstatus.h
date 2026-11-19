// rdairstatus.h
//
// Real-time status of log machines through ripcd(8), and the line-ID
// log commands (fork addition; see docs/apis/air_status.md)
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

#ifndef RDAIRSTATUS_H
#define RDAIRSTATUS_H

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <rdlogplay.h>
#include <rdmacro.h>

#define RDAIRSTATUS_VERSION 1
#define RDAIRSTATUS_HEARTBEAT_INTERVAL 10000
#define RDAIRSTATUS_POSITION_INTERVAL 1000
#define RDAIRSTATUS_CAPS "status,cmdid,lineid,pause,edit"

class RDAirStatus : public QObject
{
  Q_OBJECT
 public:
  RDAirStatus(const QString &app,QObject *parent=0);
  void addMachine(int machine,RDLogPlay *log);
  void modeChanged(int machine);
  void sendFullState();
  void sendCommandResult(const RDMacro *rml,bool ok,const QString &error);
  QString lineCommand(RDMacro *rml,RDLogPlay *log,bool pause_enabled);
  static QString guardCheck(RDLogPlay *log,int line,int guard_secs);
  static int timeOfDay(const QTime &time);

 private slots:
  void readyData();
  void heartbeatData();
  void beatNowData();
  void caeConnectedData(bool state);

 private:
  class Machine {
   public:
    Machine() {machine=0; log=NULL; version=0;}
    int machine;
    RDLogPlay *log;
    unsigned version;
    QMap<int,int> positions;      // line ID -> position (ms)
    QMap<int,qint64> sent_at;     // line ID -> when its last 'pos' went
  };
  void Send(const QString &type,const QString &fields);
  void SendHello();
  void SendBeat();
  QString MachineSummary(Machine *m) const;
  QString LineId(Machine *m,int line) const;
  Machine *FindMachine(RDLogPlay *log);
  void Played(Machine *m,int line);
  void Paused(Machine *m,int line);
  void Stopped(Machine *m,int line);
  void Position(Machine *m,int line,int point);
  void Inserted(Machine *m,int line);
  void Removed(Machine *m,int line,int num,bool moving);
  void Modified(Machine *m,int line);
  void Reloaded(Machine *m);
  QList<Machine *> air_machines;
  QString air_app;
  qint64 air_started;
  unsigned air_seq;
  QTimer *air_heartbeat_timer;
  int air_pending_move_id;
  Machine *air_pending_move_machine;
};


#endif  // RDAIRSTATUS_H
