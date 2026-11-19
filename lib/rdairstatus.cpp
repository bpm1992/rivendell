// rdairstatus.cpp
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

#include <unistd.h>

#include "rdairstatus.h"
#include "rdapplication.h"
#include "rdcart.h"

RDAirStatus::RDAirStatus(const QString &app,QObject *parent)
  : QObject(parent)
{
  air_app=app;
  air_started=QDateTime::currentMSecsSinceEpoch();
  air_seq=0;
  air_pending_move_id=-1;
  air_pending_move_machine=NULL;

  connect(rda->ripc(),SIGNAL(statusReady()),this,SLOT(readyData()));
  connect(rda->ripc(),SIGNAL(statusRequested()),this,SLOT(readyData()));
  connect(rda->ripc(),SIGNAL(onairFlagChanged(bool)),
	  this,SLOT(beatNowData()));
  connect(rda->cae(),SIGNAL(isConnected(bool)),
	  this,SLOT(caeConnectedData(bool)));

  air_heartbeat_timer=new QTimer(this);
  connect(air_heartbeat_timer,SIGNAL(timeout()),this,SLOT(heartbeatData()));
  air_heartbeat_timer->start(RDAIRSTATUS_HEARTBEAT_INTERVAL);

  //
  // The connection to ripcd is usually made before this exists, so its
  // statusReady() has gone: say hello once the program is set up.  If it
  // isn't connected yet, nothing is sent and statusReady() follows.
  //
  QTimer::singleShot(0,this,SLOT(readyData()));
}


void RDAirStatus::addMachine(int machine,RDLogPlay *log)
{
  Machine *m=new Machine();
  m->machine=machine;
  m->log=log;
  air_machines.push_back(m);

  connect(log,&RDLogPlay::played,this,[this,m](int line){Played(m,line);});
  connect(log,&RDLogPlay::paused,this,[this,m](int line){Paused(m,line);});
  connect(log,&RDLogPlay::stopped,this,[this,m](int line){Stopped(m,line);});
  connect(log,&RDLogPlay::position,this,
	  [this,m](int line,int point){Position(m,line,point);});
  connect(log,&RDLogPlay::inserted,this,
	  [this,m](int line){Inserted(m,line);});
  connect(log,&RDLogPlay::removed,this,
	  [this,m](int line,int num,bool moving){Removed(m,line,num,moving);});
  connect(log,&RDLogPlay::modified,this,
	  [this,m](int line){Modified(m,line);});
  connect(log,&RDLogPlay::reloaded,this,[this,m](){Reloaded(m);});
  connect(log,&RDLogPlay::renamed,this,[this,m](){Reloaded(m);});
  connect(log,&RDLogPlay::channelStarted,this,
	  [this,m](int id,int mport,int card,int port){
	    Send("channel",QString::asprintf("%d %d %d %d 1",
					     m->machine,mport,card,port));
	  });
  connect(log,&RDLogPlay::channelStopped,this,
	  [this,m](int id,int mport,int card,int port){
	    Send("channel",QString::asprintf("%d %d %d %d 0",
					     m->machine,mport,card,port));
	  });
  connect(log,&RDLogPlay::activeEventChanged,this,
	  [this,m](int line,RDLogLine::TransType trans){
	    Send("segue",QString::asprintf("%d ",m->machine)+LineId(m,line)+
		 QString::asprintf(" %d",trans));
	  });
  connect(log,&RDLogPlay::nextEventChanged,this,
	  [this,m](int line){
	    Send("next",QString::asprintf("%d ",m->machine)+LineId(m,line));
	  });
  connect(log,&RDLogPlay::nextStopChanged,this,
	  [this,m](QTime time){
	    Send("nextstop",QString::asprintf("%d %d",m->machine,
					      timeOfDay(time)));
	  });
  connect(log,&RDLogPlay::runStatusChanged,this,
	  [this,m](bool running){
	    Send("running",QString::asprintf("%d %d",m->machine,running));
	    SendBeat();
	  });
}


void RDAirStatus::modeChanged(int machine)
{
  for(int i=0;i<air_machines.size();i++) {
    Machine *m=air_machines.at(i);
    if(m->machine==machine) {
      Send("mode",QString::asprintf("%d %d",m->machine,m->log->mode()));
    }
  }
  SendBeat();
}


void RDAirStatus::sendFullState()
{
  QString fields;

  for(int i=0;i<air_machines.size();i++) {
    Machine *m=air_machines.at(i);
    RDLogPlay *log=m->log;
    int lines[TRANSPORT_QUANTITY];
    RDLogLine *ll;

    if(!fields.isEmpty()) {
      fields+=" ";
    }
    fields+=QString::asprintf("M %d %u %d %d ",m->machine,m->version,
			      log->mode(),log->running(true))+
      LineId(m,log->nextLine());
    int running=log->runningEvents(lines,true);
    for(int j=0;j<running;j++) {
      if((ll=log->logLine(lines[j]))!=NULL) {
	fields+=QString::asprintf(" D %d:%u:%d:%d:%d",ll->id(),
				  ll->cartNumber(),ll->cutNumber(),
				  m->positions.value(ll->id(),0),
				  ll->effectiveLength());
      }
    }
    for(int j=0;j<log->lineCount();j++) {
      if((ll=log->logLine(j))!=NULL) {
	fields+=QString::asprintf(" L %d:%u:%d:%d:%d:%d:%d:%d",ll->id(),
				  ll->cartNumber(),ll->cutNumber(),
				  ll->status(),ll->transType(),ll->timeType(),
			  timeOfDay(ll->startTime(RDLogLine::Predicted)),
				  ll->effectiveLength());
      }
    }
  }
  Send("state",fields);
}


void RDAirStatus::sendCommandResult(const RDMacro *rml,bool ok,
				    const QString &error)
{
  if(rml->commandId().isEmpty()) {
    return;
  }
  Send("cmd",rml->commandId()+QString::asprintf(" %d ",ok)+
       (error.isEmpty()?QString("-"):error));
}


QString RDAirStatus::lineCommand(RDMacro *rml,RDLogPlay *log,
				 bool pause_enabled)
{
  //
  // Runs one of LP, LN, LI, LV, LD, PA or PR on this machine's log.
  // Returns an empty string when done, otherwise a short code word.  The
  // caller has checked the machine number (argument 0).
  //
  int guard=rda->config()->editGuardSeconds();
  int lines[TRANSPORT_QUANTITY];
  int line=-1;
  int pos=-1;
  bool ok=false;
  QString err;
  RDLogLine::TransType trans=RDLogLine::Play;

  switch(rml->command()) {
  case RDMacro::LP:   // Start this line
    if(rml->argQuantity()!=2) {
      return "args";
    }
    if((line=log->lineById(rml->arg(1).toInt(&ok)))<0||(!ok)) {
      return "noline";
    }
    if(log->running(true)) {
      return "running";
    }
    if(log->logLine(line)->status()!=RDLogLine::Scheduled) {
      return "played";
    }
    if(!log->play(line,RDLogLine::StartMacro)) {
      return "failed";
    }
    return QString();

  case RDMacro::LN:   // Make this line next
    if(rml->argQuantity()!=2) {
      return "args";
    }
    if((line=log->lineById(rml->arg(1).toInt(&ok)))<0||(!ok)) {
      return "noline";
    }
    if(log->logLine(line)->status()!=RDLogLine::Scheduled) {
      return "played";
    }
    log->makeNext(line);
    return QString();

  case RDMacro::LI:   // Insert a cart after a line
    if((rml->argQuantity()<3)||(rml->argQuantity()>4)) {
      return "args";
    }
    if(rml->argQuantity()==4) {
      trans=RDLogLine::NoTrans;
      if(rml->arg(3).toLower()=="play") {
	trans=RDLogLine::Play;
      }
      if(rml->arg(3).toLower()=="segue") {
	trans=RDLogLine::Segue;
      }
      if(rml->arg(3).toLower()=="stop") {
	trans=RDLogLine::Stop;
      }
      if(trans==RDLogLine::NoTrans) {
	return "args";
      }
    }
    {
      unsigned cartnum=rml->arg(2).toUInt(&ok);
      if((!ok)||(cartnum==0)||(cartnum>RD_MAX_CART_NUMBER)) {
	return "args";
      }
      RDCart *cart=new RDCart(cartnum);
      bool exists=cart->exists();
      delete cart;
      if(!exists) {
	return "nocart";
      }
      int after=rml->arg(1).toInt(&ok);
      if(!ok) {
	return "args";
      }
      if(after<0) {
	if((pos=log->nextLine())<0) {
	  pos=log->lineCount();
	}
      }
      else {
	if((line=log->lineById(after))<0) {
	  return "noline";
	}
	pos=line+1;
      }
      if(pos<log->lineCount()) {
	if(!(err=guardCheck(log,pos,guard)).isEmpty()) {
	  return err;
	}
      }
      bool had_next=log->nextLine()>=0;
      log->insert(pos,cartnum,RDLogLine::NoTrans,trans);
      if(!had_next) {
	log->makeNext(pos);
      }
    }
    return QString();

  case RDMacro::LV:   // Move a line after another
    if(rml->argQuantity()!=3) {
      return "args";
    }
    if((line=log->lineById(rml->arg(1).toInt(&ok)))<0||(!ok)) {
      return "noline";
    }
    if(!(err=guardCheck(log,line,guard)).isEmpty()) {
      return err;
    }
    {
      int after=rml->arg(2).toInt(&ok);
      if(!ok) {
	return "args";
      }
      if(after<0) {
	if((pos=log->nextLine())<0) {
	  pos=log->lineCount();
	}
      }
      else {
	int a=log->lineById(after);
	if(a<0) {
	  return "noline";
	}
	if(a==line) {
	  return "args";
	}
	pos=a+1;
      }
      if((pos<log->lineCount())&&(pos!=line)) {
	if(!(err=guardCheck(log,pos,guard)).isEmpty()) {
	  return err;
	}
      }
      if(pos>line) {   // As RDAirPlay's own Move does
	pos--;
      }
      if(pos!=line) {
	log->move(line,pos);
	log->update(pos);
      }
    }
    return QString();

  case RDMacro::LD:   // Delete a line
    if(rml->argQuantity()!=2) {
      return "args";
    }
    if((line=log->lineById(rml->arg(1).toInt(&ok)))<0||(!ok)) {
      return "noline";
    }
    if(!(err=guardCheck(log,line,guard)).isEmpty()) {
      return err;
    }
    log->remove(line,1);
    return QString();

  case RDMacro::PA:   // Pause
    if(rml->argQuantity()!=1) {
      return "args";
    }
    if(!pause_enabled) {
      return "disabled";
    }
    {
      int running=log->runningEvents(lines,false);
      for(int i=0;i<running;i++) {
	if((log->logLine(lines[i])!=NULL)&&
	   (log->logLine(lines[i])->status()==RDLogLine::Playing)) {
	  return log->pause(lines[i])?QString():QString("failed");
	}
      }
    }
    return "idle";

  case RDMacro::PR:   // Resume
    if(rml->argQuantity()!=1) {
      return "args";
    }
    {
      int running=log->runningEvents(lines,true);
      for(int i=0;i<running;i++) {
	if((log->logLine(lines[i])!=NULL)&&
	   (log->logLine(lines[i])->status()==RDLogLine::Paused)) {
	  return log->play(lines[i],RDLogLine::StartMacro)?
	    QString():QString("failed");
	}
      }
    }
    return "idle";

  default:
    break;
  }
  return "args";
}


QString RDAirStatus::guardCheck(RDLogPlay *log,int line,int guard_secs)
{
  //
  // Whether a remote edit may touch this line (move or delete it, or insert
  // before it): not once it has played or is playing, nor while its
  // predicted start is within the guard window
  //
  RDLogLine *ll=log->logLine(line);
  if(ll==NULL) {
    return "noline";
  }
  if(ll->status()!=RDLogLine::Scheduled) {
    return "played";
  }
  if(guard_secs>0) {
    QTime start=ll->startTime(RDLogLine::Predicted);
    if(start.isValid()) {
      int ahead=QTime::currentTime().msecsTo(start);
      if(ahead<0) {
	ahead+=86400000;  // Past midnight
      }
      if(ahead<(1000*guard_secs)) {
	return "guard";
      }
    }
  }
  return QString();
}


int RDAirStatus::timeOfDay(const QTime &time)
{
  if(!time.isValid()) {
    return -1;
  }
  return time.msecsSinceStartOfDay();
}


void RDAirStatus::readyData()
{
  //
  // Connected (again) to ripcd, asked by it for a full state, or caught up
  // after skipping messages
  //
  SendHello();
  sendFullState();
  SendBeat();
}


void RDAirStatus::heartbeatData()
{
  SendBeat();
}


void RDAirStatus::beatNowData()
{
  SendBeat();
}


void RDAirStatus::caeConnectedData(bool state)
{
  SendBeat();
}


void RDAirStatus::Send(const QString &type,const QString &fields)
{
  rda->ripc()->sendStatus(QString::asprintf("ST %d %u %lld ",
					    RDAIRSTATUS_VERSION,++air_seq,
				    QDateTime::currentMSecsSinceEpoch())+
			  type+(fields.isEmpty()?QString():(" "+fields))+"!");
}


void RDAirStatus::SendHello()
{
  Send("hello",air_app+QString::asprintf(" %d %lld ",getpid(),air_started)+
       RDAIRSTATUS_CAPS);
}


void RDAirStatus::SendBeat()
{
  QString fields=QString::asprintf("%d %d %u",rda->ripc()->onairFlag(),
				   rda->cae()->isCaeConnected(),
				   rda->ripc()->statusSkipped());
  for(int i=0;i<air_machines.size();i++) {
    fields+=" "+MachineSummary(air_machines.at(i));
  }
  air_heartbeat_timer->start(RDAIRSTATUS_HEARTBEAT_INTERVAL);
  Send("beat",fields);
}


QString RDAirStatus::MachineSummary(Machine *m) const
{
  int lines[TRANSPORT_QUANTITY];
  RDLogPlay *log=m->log;
  int next=log->nextLine();
  int next_trans=-1;
  if((next>=0)&&(log->logLine(next)!=NULL)) {
    next_trans=log->logLine(next)->transType();
  }
  return QString::asprintf("%d:%d:%d:%d:",m->machine,log->mode(),
			   log->running(true),log->runningEvents(lines,false))+
    LineId(m,next)+QString::asprintf(":%d",next_trans);
}


QString RDAirStatus::LineId(Machine *m,int line) const
{
  RDLogLine *ll=NULL;
  if((line<0)||((ll=m->log->logLine(line))==NULL)) {
    return QString("-1");
  }
  return QString::asprintf("%d",ll->id());
}


void RDAirStatus::Played(Machine *m,int line)
{
  RDLogLine *ll=m->log->logLine(line);
  if(ll==NULL) {
    return;
  }
  m->positions[ll->id()]=0;
  Send("played",QString::asprintf("%d %d %u %d %d %d",m->machine,ll->id(),
				  ll->cartNumber(),ll->cutNumber(),
				  ll->effectiveLength(),
			  timeOfDay(ll->startTime(RDLogLine::Actual))));
}


void RDAirStatus::Paused(Machine *m,int line)
{
  RDLogLine *ll=m->log->logLine(line);
  if(ll==NULL) {
    return;
  }
  Send("paused",QString::asprintf("%d %d %d",m->machine,ll->id(),
				  m->positions.value(ll->id(),0)));
}


void RDAirStatus::Stopped(Machine *m,int line)
{
  RDLogLine *ll=m->log->logLine(line);
  if(ll==NULL) {
    return;
  }
  Send("stopped",QString::asprintf("%d %d %d",m->machine,ll->id(),
				   m->positions.value(ll->id(),0)));
  m->positions.remove(ll->id());
  m->sent_at.remove(ll->id());
}


void RDAirStatus::Position(Machine *m,int line,int point)
{
  RDLogLine *ll=m->log->logLine(line);
  if(ll==NULL) {
    return;
  }
  m->positions[ll->id()]=point;
  qint64 now=QDateTime::currentMSecsSinceEpoch();
  if((now-m->sent_at.value(ll->id(),0))>=RDAIRSTATUS_POSITION_INTERVAL) {
    m->sent_at[ll->id()]=now;
    Send("pos",QString::asprintf("%d %d %d",m->machine,ll->id(),point));
  }
}


void RDAirStatus::Inserted(Machine *m,int line)
{
  RDLogLine *ll=m->log->logLine(line);
  if(ll==NULL) {
    return;
  }
  m->version++;
  QString after=LineId(m,line-1);
  if((air_pending_move_machine==m)&&(air_pending_move_id==ll->id())) {
    air_pending_move_machine=NULL;
    air_pending_move_id=-1;
    Send("move",QString::asprintf("%d %u %d ",m->machine,m->version,ll->id())+
	 after);
    return;
  }
  Send("insert",QString::asprintf("%d %u %d ",m->machine,m->version,ll->id())+
       after+QString::asprintf(" %u %d %d",ll->cartNumber(),ll->cutNumber(),
			       ll->transType()));
}


void RDAirStatus::Removed(Machine *m,int line,int num,bool moving)
{
  RDLogLine *ll=NULL;

  if(moving&&(num==1)&&((ll=m->log->logLine(line))!=NULL)) {
    air_pending_move_machine=m;   // 'move' goes when it is inserted again
    air_pending_move_id=ll->id();
    return;
  }
  m->version++;
  QString fields=QString::asprintf("%d %u",m->machine,m->version);
  for(int i=line;i<(line+num);i++) {
    if((ll=m->log->logLine(i))!=NULL) {
      fields+=QString::asprintf(" %d",ll->id());
      m->positions.remove(ll->id());
      m->sent_at.remove(ll->id());
    }
  }
  Send("remove",fields);
}


void RDAirStatus::Modified(Machine *m,int line)
{
  RDLogLine *ll=m->log->logLine(line);
  if(ll==NULL) {
    return;
  }
  m->version++;
  Send("modify",QString::asprintf("%d %u %d %d %d",m->machine,m->version,
				  ll->id(),ll->cutNumber(),ll->status()));
}


void RDAirStatus::Reloaded(Machine *m)
{
  m->version++;
  m->positions.clear();
  m->sent_at.clear();
  Send("loaded",QString::asprintf("%d %u",m->machine,m->version));
  sendFullState();
}
