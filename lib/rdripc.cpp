// rdripc.cpp
//
// Connection to the Rivendell Interprocess Communication Daemon
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

#include "rdapplication.h"
#include "rddatedecode.h"
#include "rddb.h"
#include "rdescape_string.h"
#include "rdripc.h"
#include <QRandomGenerator>

RDRipc::RDRipc(RDStation *station,RDConfig *config,QObject *parent)
  : QObject(parent)
{
  ripc_station=station;
  ripc_config=config;
  ripc_socket=NULL;
  ripc_onair_flag=false;
  ripc_ignore_mask=false;
  ripc_accum="";
  ripc_watchdog_pending=false;
  debug=false;

  ripc_connected=false;
  // Initialize reconnect lock (per-host) to avoid thundering herd
  ripc_reconnect_lock=new QLockFile(QString::asprintf("/tmp/rdripc-reconnect-%s.lock",
                                        ripc_station->name().toUtf8().constData()));
  ripc_reconnect_lock->setStaleLockTime(30000);

  //
  // Watchdog Timers
  //
  ripc_watchdog_timer=new QTimer(this);
  ripc_watchdog_timer->setSingleShot(true);
  connect(ripc_watchdog_timer,SIGNAL(timeout()),this,SLOT(watchdogRetryData()));

  ripc_heartbeat_timer=new QTimer(this);
  ripc_heartbeat_timer->setSingleShot(true);
  connect(ripc_heartbeat_timer,SIGNAL(timeout()),
	  this,SLOT(sendHeartbeatData()));

    // Retry backoff timer
    ripc_retry_timer=new QTimer(this);
    ripc_retry_timer->setSingleShot(true);
    connect(ripc_retry_timer,SIGNAL(timeout()),this,SLOT(retryTimerFired()));

  // Metrics initialization
  ripc_metrics_start_ts=time(NULL);
  ripc_metrics_timer=new QTimer(this);
  ripc_metrics_timer->setSingleShot(false);
  // Metrics timer -- Uncomment the two lines below to enable periodic logging
  // connect(ripc_metrics_timer,&QTimer::timeout,[this](){ if(rda!=nullptr) { LogMetrics(); } });
  // ripc_metrics_timer->start(10000); // 10s summary
}


RDRipc::~RDRipc()
{
  delete ripc_socket;
}


QString RDRipc::user() const
{
  return ripc_user;
}


QString RDRipc::station() const
{
  return ripc_station->name();
}


bool RDRipc::onairFlag() const
{
  return ripc_onair_flag;
}


void RDRipc::setUser(QString user)
{
  SendCommand(QString("SU ")+user+"!");
}


void RDRipc::setIgnoreMask(bool state)
{
  ripc_ignore_mask=state;
}


void RDRipc::connectHost(QString hostname,uint16_t hostport,QString password)
{
  ripc_hostname=hostname;
  ripc_hostport=hostport;
  ripc_password=password;

  //
  // TCP Connection
  //
  ripc_heartbeat_timer->stop();
  if(ripc_socket!=NULL) {
    ripc_socket->deleteLater();
  }
  ripc_socket=new QTcpSocket(this);
  connect(ripc_socket,SIGNAL(connected()),this,SLOT(connectedData()));
  connect(ripc_socket,SIGNAL(disconnected()),this,SLOT(disconnectedData()));
  connect(ripc_socket,SIGNAL(error(QAbstractSocket::SocketError)),
	  this,SLOT(errorData(QAbstractSocket::SocketError)));
  connect(ripc_socket,SIGNAL(readyRead()),this,SLOT(readyData()));

  ripc_socket->connectToHost(hostname,hostport);
  ripc_heartbeat_timer->start(RIPC_HEARTBEAT_POLL_INTERVAL);
}


void RDRipc::connectedData()
{
  // if(rda!=nullptr) {
  //   rda->syslog(LOG_INFO,"RDRipc: TCP connected to %s:%d - sending password",
  //               ripc_hostname.toUtf8().constData(), ripc_hostport);
  // }
  SendCommand(QString("PW ")+ripc_password+"!");
  if(ripc_watchdog_pending) {
    rda->syslog(LOG_WARNING,"connection to ripcd(8) restored");
    ripc_watchdog_pending=false;
  }
  // Reset backoff on successful connect
  ripc_retry_ms=1000;
  // Release reconnect lock if held
  if(ripc_reconnect_lock && ripc_reconnect_lock->isLocked()) {
    ripc_reconnect_lock->unlock();
  }
}


void RDRipc::disconnectedData()
{
  ripc_heartbeat_timer->stop();
  ripc_watchdog_timer->stop();
  ripc_watchdog_timer->start(RIPC_HEARTBEAT_POLL_INTERVAL);
  ripc_metric_disconnects++;
  
  // Reset connection state
  if(ripc_connected) {
    ripc_connected=false;
    emit connected(false);
  }
  ripc_user="";  // Clear username so RU command triggers connected(true) on reconnect
  
  // Start exponential backoff reconnect
  if(!ripc_retry_timer->isActive()) {
    if(rda!=nullptr) {
      rda->syslog(LOG_INFO,
                  "RDRipc: disconnected - scheduling reconnect in %d ms",
                  ripc_retry_ms);
    }
    // Apply jitter +/-20% to avoid thundering herd
    int jitter = (int)(ripc_retry_ms * (0.2 * (QRandomGenerator::global()->bounded(2001) - 1000) / 1000.0));
    int next_delay = qBound(500, ripc_retry_ms + jitter, 30000);
    ripc_retry_timer->start(next_delay);
    ripc_retry_ms=qMin(ripc_retry_ms*2,30000); // base backoff growth
  }
}


void RDRipc::sendHeartbeatData()
{
  ripc_watchdog_timer->stop();
  SendCommand("HB!");
  ripc_watchdog_timer->start(RIPC_HEARTBEAT_POLL_INTERVAL);
}


void RDRipc::watchdogRetryData()
{
  if(!ripc_watchdog_pending) {
    rda->syslog(LOG_WARNING,
		"connection to ripcd(8) timed out, attempting reconnect");
  }
  ripc_watchdog_pending=true;
  connectHost(ripc_hostname,ripc_hostport,ripc_password);
  ripc_watchdog_timer->start(RIPC_HEARTBEAT_POLL_INTERVAL);
}


void RDRipc::sendGpiStatus(int matrix)
{
  SendCommand(QString::asprintf("GI %d!",matrix));
}


void RDRipc::sendGpoStatus(int matrix)
{
  SendCommand(QString::asprintf("GO %d!",matrix));
}


void RDRipc::sendGpiMask(int matrix)
{
  SendCommand(QString::asprintf("GM %d!",matrix));
}


void RDRipc::sendGpoMask(int matrix)
{
  SendCommand(QString::asprintf("GN %d!",matrix));
}


void RDRipc::sendGpiCart(int matrix)
{
  SendCommand(QString::asprintf("GC %d!",matrix));
}


void RDRipc::sendGpoCart(int matrix)
{
  SendCommand(QString::asprintf("GD %d!",matrix));
}


void RDRipc::sendNotification(RDNotification::Type type,
			      RDNotification::Action action,const QVariant &id)
{
  RDNotification *notify=new RDNotification(type,action,id);
  sendNotification(*notify);
  delete notify;
}


void RDRipc::sendNotification(const RDNotification &notify)
{
  SendCommand("ON "+notify.write()+"!");
}


void RDRipc::sendCatchEvent(RDCatchEvent *evt)
{
  SendCommand("ON "+evt->write()+"!");
}


void RDRipc::sendOnairFlag()
{
  SendCommand(QString::asprintf("TA %d!",ripc_onair_flag));
}


void RDRipc::sendRml(RDMacro *macro)
{
  QString cmd;
  uint16_t port=RD_RML_NOECHO_PORT;
  QDateTime now=QDateTime::currentDateTime();

  if(macro->echoRequested()) {
    port=RD_RML_ECHO_PORT;
  }
  if(macro->port()>0) {
    port=macro->port();
  }
  QString rmlline=macro->toString();
  QString sql=QString("select ")+
    "`NAME`,"+      // 00
    "`VARVALUE` "+  // 01
    "from `HOSTVARS` where "+
    "`STATION_NAME`='"+RDEscapeString(ripc_station->name())+"'";
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    rmlline.replace(q->value(0).toString(),q->value(1).toString());
  }
  delete q;
  rmlline=RDDateTimeDecode(rmlline,now,ripc_station,ripc_config);
  switch(macro->role()) {
  case RDMacro::Cmd:
    cmd=QString("MS ")+macro->address().toString()+
      QString::asprintf(" %d ",port)+rmlline;
    break;
	
  case RDMacro::Reply:
    cmd=QString("ME ")+macro->address().toString()+
      QString::asprintf(" %d ",port)+rmlline;
    break;

  default:
    break;
  }
  SendCommand(cmd);
}


void RDRipc::errorData(QAbstractSocket::SocketError err)
{
  rda->syslog(LOG_WARNING,"RDRipc: socket error %d [%s]",
              (int)err, ripc_socket->errorString().toUtf8().constData());
  // Trigger backoff if not already pending
  if(!ripc_retry_timer->isActive()) {
    rda->syslog(LOG_INFO,
                "RDRipc: scheduling reconnect in %d ms due to error",
                ripc_retry_ms);
    int jitter = (int)(ripc_retry_ms * (0.2 * (QRandomGenerator::global()->bounded(2001) - 1000) / 1000.0));
    int next_delay = qBound(500, ripc_retry_ms + jitter, 30000);
    ripc_retry_timer->start(next_delay);
    ripc_retry_ms=qMin(ripc_retry_ms*2,30000);
  }
}


void RDRipc::readyData()
{
  char data[1501];
  int n;

  // Try to read data
  while((n=ripc_socket->read(data,1500))>0) {
    ripc_metric_reads++;
    data[n]=0;
    // if(rda!=nullptr && n>0) {
    //   rda->syslog(LOG_DEBUG,"RDRipc: READ %d bytes: [%s]",
    //               n, QString::fromUtf8(data,qMin(n,50)).toUtf8().constData());
    // }
    QString line=QString::fromUtf8(data);
    for(int i=0;i<line.length();i++) {
      QChar c=line.at(i);
      if(c==QChar('!')) {
	DispatchCommand();
	ripc_accum="";
      }
      else {
	if((c!=QChar('\r'))&&(c!=QChar('\n'))) {
	  ripc_accum+=c;
	  // Protect against unbounded accumulator growth
	  if(ripc_accum.length() > RIPC_MAX_LENGTH) {
	    if(rda!=nullptr) {
	      rda->syslog(LOG_ERR,
	                  "RDRipc: command buffer overflow (%d bytes), disconnecting",
	                  ripc_accum.length());
	    }
	    ripc_accum="";
	    ripc_socket->disconnectFromHost();
	    return;
	  }
	}
      }
    }
  }
  
  // Only check for spurious readiness if the entire read loop found nothing
  // AND there's nothing left to read
  if(n<=0 && ripc_socket->bytesAvailable()==0) {
    ripc_empty_ready_count++;
    ripc_metric_empty_ready++;
    // Rate-limited logging: first and every 100
    if(ripc_empty_ready_count==1 || (ripc_empty_ready_count%100)==0) {
      if(rda!=nullptr) {
        rda->syslog(LOG_WARNING,
                    "RDRipc: empty readyRead events count=%d",ripc_empty_ready_count);
      }
    }
    // Force reconnect if we hit excessive empty events (spin loop detection)
    if(ripc_empty_ready_count >= 100) {
      if(rda!=nullptr) {
        rda->syslog(LOG_WARNING,
                    "RDRipc: excessive empty readyRead events (%d), forcing reconnect to break spin loop",
                    ripc_empty_ready_count);
      }
      ripc_empty_ready_count=0;
      ripc_socket->disconnectFromHost();
      // disconnectedData() will handle reconnection via retry timer
    }
  }
  else {
    // Reset counter when we successfully read data
    ripc_empty_ready_count=0;
  }
}

void RDRipc::retryTimerFired()
{
  if(ripc_connected) {
    return;
  }
  // Attempt reconnect
  if(rda!=nullptr) {
    rda->syslog(LOG_INFO,"RDRipc: attempting reconnect now");
  }
  ripc_metric_reconnect_attempts++;
  // Acquire lock to limit concurrent reconnects across daemons
  if(ripc_reconnect_lock && !ripc_reconnect_lock->tryLock(30000)) {
    // Couldn't acquire lock, reschedule with jittered delay
    int jitter = (int)(ripc_retry_ms * (0.2 * (QRandomGenerator::global()->bounded(2001) - 1000) / 1000.0));
    int next_delay = qBound(500, ripc_retry_ms + jitter, 30000);
    rda->syslog(LOG_INFO,"RDRipc: reconnect lock busy - rescheduling in %d ms",next_delay);
    ripc_retry_timer->start(next_delay);
    return;
  }
  connectHost(ripc_hostname,ripc_hostport,ripc_password);
}


void RDRipc::SendCommand(const QString &cmd)
{
  //  printf("RDRipc::SendCommand(%s)\n",(const char *)cmd.toUtf8());
  
  // Check connection state before writing
  if(ripc_socket->state() != QAbstractSocket::ConnectedState) {
    if(rda!=nullptr) {
      rda->syslog(LOG_WARNING,
                  "RDRipc: attempted write while disconnected (state=%d), triggering reconnect",
                  ripc_socket->state());
    }
    // Trigger reconnection if not already pending
    if(!ripc_retry_timer->isActive() && !ripc_watchdog_timer->isActive()) {
      ripc_retry_timer->start(100);
    }
    return;
  }
  
  // Write to socket and check for errors
  qint64 written = ripc_socket->write(cmd.toUtf8());
  if(written == -1) {
    if(rda!=nullptr) {
      rda->syslog(LOG_ERR,
                  "RDRipc: write failed: %s",
                  ripc_socket->errorString().toUtf8().constData());
    }
    // Trigger reconnection on write error
    if(!ripc_retry_timer->isActive()) {
      ripc_retry_timer->start(100);
    }
  }
}


void RDRipc::DispatchCommand()
{
  RDMacro macro;
  QString str;

  //  printf("RDRipc::DispatchCommand: %s\n",ripc_accum.toUtf8().constData());
  QStringList cmds=ripc_accum.split(" ",QString::SkipEmptyParts);

  if(cmds.size()==0) {
    return;
  }
  ripc_metric_dispatches++;
  
  // Debug: log all commands for troubleshooting
  // if(rda!=nullptr && cmds.size()>0) {
  //   rda->syslog(LOG_DEBUG,"RDRipc: DISPATCH cmd='%s' args=%d",
  //               cmds[0].toUtf8().constData(), cmds.size()-1);
  // }
  
  if(cmds[0]=="PW") {  // Password Response
    // if(rda!=nullptr) {
    //   rda->syslog(LOG_INFO,"RDRipc: received PW response");
    // }
    // Request user identity after successful authentication
    SendCommand("RU!");
  }

  if((cmds[0]=="RU")&&(cmds.size()==2)) {  // User Identity
    // if(rda!=nullptr) {
    //   rda->syslog(LOG_INFO,"RDRipc: received RU user='%s'",
    //               cmds[1].toUtf8().constData());
    // }
    if(cmds[1]!=ripc_user) {
      ripc_user=cmds[1];
      if(!ripc_connected) {
	ripc_connected=true;
	emit connected(true);
        // if(rda!=nullptr) {
        //   rda->syslog(LOG_INFO,"RDRipc: NOW CONNECTED - emitted connected(true)");
        // }
      }
      emit userChanged();
    }
  }

  if(cmds[0]=="HB") {  // Heartbeat
    if(cmds.size()!=1) {
      return;
    }
    ripc_watchdog_timer->stop();
    ripc_heartbeat_timer->start(RIPC_HEARTBEAT_POLL_INTERVAL);
  }

  if(cmds[0]=="MS") {  // Macro Sent
    if(cmds.size()<4) {
      return;
    }
    str=cmds[3];
    for(int i=4;i<cmds.size();i++) {
      str+=" "+cmds[i];
    }
    str+="!";
    macro=RDMacro::fromString(str,RDMacro::Cmd);
    if(!macro.isNull()) {
      QHostAddress addr;
      addr.setAddress(cmds[1]);
      if(cmds[2].left(1)=="1") {
	macro.setEchoRequested(true);
      }
      macro.setAddress(addr);
      emit rmlReceived(&macro);
    }
    return;
  }

  if(cmds[0]=="ME") {  // Macro Echoed
    if(cmds.size()<4) {
      return;
    }
    str=cmds[3];
    for(int i=4;i<cmds.size();i++) {
      str+=" "+cmds[i];
    }
    str+="!";
    macro=RDMacro::fromString(str,RDMacro::Reply);
    if(!macro.isNull()) {
      macro.setAddress(QHostAddress(cmds[1]));
      macro.setRole(RDMacro::Reply);
      emit rmlReceived(&macro);
    }
    return;
  }

  if(cmds[0]=="GI") {   // GPI State Changed
    if(cmds.size()<4) {
      return;
    }
    int matrix=cmds[1].toInt();
    int line=cmds[2].toInt();
    int mask=cmds[4].toInt();
    if((mask>0)||ripc_ignore_mask) {
      if(cmds[3].left(1)=="0") {
	emit gpiStateChanged(matrix,line,false);
      }
      else {
	emit gpiStateChanged(matrix,line,true);
      }
    }
  }

  if(cmds[0]=="GO") {   // GPO State Changed
    if(cmds.size()<4) {
      return;
    }
    int matrix=cmds[1].toInt();
    int line=cmds[2].toInt();
    int mask=cmds[4].toInt();
    if((mask>0)||ripc_ignore_mask) {
      if(cmds[3].left(1)=="0") {
	emit gpoStateChanged(matrix,line,false);
      }
      else {
	emit gpoStateChanged(matrix,line,true);
      }
    }
  }

  if(cmds[0]=="GM") {   // GPI Mask Changed
    if(cmds.size()<4) {
      return;
    }
    int matrix=cmds[1].toInt();
    int line=cmds[2].toInt();
    if(cmds[3].left(1)=="0") {
      emit gpiMaskChanged(matrix,line,false);
    }
    else {
      emit gpiMaskChanged(matrix,line,true);
    }
  }

  if(cmds[0]=="GN") {   // GPO Mask Changed
    if(cmds.size()<4) {
      return;
    }
    int matrix=cmds[1].toInt();
    int line=cmds[2].toInt();
    if(cmds[3].left(1)=="0") {
      emit gpoMaskChanged(matrix,line,false);
    }
    else {
      emit gpoMaskChanged(matrix,line,true);
    }
  }

  if(cmds[0]=="GC") {   // GPI Cart Changed
    if(cmds.size()<5) {
      return;
    }
    int matrix=cmds[1].toInt();
    int line=cmds[2].toInt();
    unsigned off_cartnum=cmds[3].toUInt();
    unsigned on_cartnum=cmds[4].toUInt();
    emit gpiCartChanged(matrix,line,off_cartnum,on_cartnum);
  }

  if(cmds[0]=="GD") {   // GPO Cart Changed
    if(cmds.size()<5) {
      return;
    }
    int matrix=cmds[1].toInt();
    int line=cmds[2].toInt();
    unsigned off_cartnum=cmds[3].toUInt();
    unsigned on_cartnum=cmds[4].toUInt();
    emit gpoCartChanged(matrix,line,off_cartnum,on_cartnum);
  }

  if(cmds[0]=="TA") {   // On Air Flag Changed
    if(cmds.size()!=2) {
      return;
    }
    ripc_onair_flag=cmds[1].left(1)=="1";
    emit onairFlagChanged(ripc_onair_flag);
  }

  if(cmds[0]=="ON") {   // Notification Received
    if(cmds.size()<4) {
      return;
    }
    QString msg;
    for(int i=1;i<cmds.size();i++) {
      msg+=QString(cmds[i])+" ";
    }
    msg=msg.left(msg.length()-1);
    QStringList f0=msg.split(" ",QString::SkipEmptyParts);
    if(f0.at(0)=="NOTIFY") {
      RDNotification *notify=new RDNotification();
      if(!notify->read(msg)) {
	delete notify;
	return;
      }
      emit notificationReceived(notify);
      delete notify;
    }
    if(f0.at(0)=="CATCH") {
      RDCatchEvent *evt=new RDCatchEvent();
      if(!evt->read(msg)) {
	delete evt;
	return;
      }
      emit catchEventReceived(evt);
      delete evt;
    }
  }
}

void RDRipc::LogMetrics()
{
  time_t now=time(NULL);
  unsigned uptime=now-ripc_metrics_start_ts;
  rda->syslog(LOG_INFO,
              "METRICS rdripc: up=%us reads=%u empty_ready=%u dispatches=%u disconnects=%u reconnect_attempts=%u connected=%d",
              uptime,
              ripc_metric_reads,
              ripc_metric_empty_ready,
              ripc_metric_dispatches,
              ripc_metric_disconnects,
              ripc_metric_reconnect_attempts,
              ripc_connected?1:0);
}
