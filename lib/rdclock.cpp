// rdclock.cpp
//
// Abstract a Rivendell Log Manager Clock.
//
//   (C) Copyright 2002-2021 Fred Gleason <fredg@paravelsystems.com>
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

#include "rddb.h"
#include "rdcart_cache.h"
#include "rdclock.h"
#include "rdclock_cache.h"
#include "rdevent_line.h"
#include "rdevent_line_cache.h"
#include "rdescape_string.h"
#include "rdloggenerationcache.h"

//
// Global Classes
//
RDClock::RDClock(RDStation *station)
{
  clock_station=station;
  clear();
}


QString RDClock::clockName() const
{
  return clock_name;
}


void RDClock::setClockName(const QString &name)
{
  clock_name=name;
}


QString RDClock::shortName() const
{
  return clock_short_name;
}


void RDClock::setShortName(const QString &name)
{
  clock_short_name=name;
}


QColor RDClock::color() const
{
  return clock_color;
}


void RDClock::setColor(const QColor &color)
{
  clock_color=color;
}


QString RDClock::remarks() const
{
  return clock_remarks;
}


void RDClock::setRemarks(const QString &str)
{
  clock_remarks=str;
}


unsigned RDClock::getArtistSep()
{
  return artistsep;
}

void RDClock::setArtistSep(unsigned artist_sep)
{
  artistsep=artist_sep;
}

bool RDClock::getRulesModified()
{
  return rules_modified;
}


void RDClock::setRulesModified(bool modified)
{
  rules_modified=modified;
}


void RDClock::clear()
{
   clock_name="";
   clock_short_name="";
   clock_color=QColor();
   clock_remarks="";
   clock_events.clear();
   artistsep=15;
   rules_modified=false;
}


RDEventLine *RDClock::eventLine(int line) const
{
  if((line<0)||(line>=clock_events.size())) {
    return NULL;
  }
  return clock_events.at(line);
}


int RDClock::size() const
{
  return clock_events.size();
}


bool RDClock::load()
{
  QString sql=QString("select ")+
    "`SHORT_NAME`,"+  // 00
    "`COLOR`,"+       // 01
    "`ARTISTSEP`,"+   // 02
    "`REMARKS` "+     // 03
    "from `CLOCKS` where "+
    "`NAME`='"+RDEscapeString(clock_name)+"'";
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(!q->first()) {
    delete q;
    return false;
  }
  clock_short_name=q->value(0).toString();
  if(q->value(1).isNull()) {
    clock_color=QColor();
  }
  else {
    clock_color=QColor(q->value(1).toString());
  }
  artistsep=q->value(2).toUInt();
  clock_remarks=q->value(3).toString();
  delete q;

  sql=QString("select ")+
    "`EVENT_NAME`,"+  // 00
    "`START_TIME`,"+  // 01
    "`LENGTH` "+      // 02
    "from `CLOCK_LINES` where "+
    "`CLOCK_NAME`='"+RDEscapeString(clock_name)+"' "+
    "order by `START_TIME`";
  q=new RDSqlQuery(sql);
  while(q->next()) {
    clock_events.push_back(new RDEventLine(clock_station));
    clock_events.back()->setName(q->value(0).toString());
    clock_events.back()->setStartTime(QTime(0,0,0).addMSecs(q->value(1).toInt()));
    clock_events.back()->setLength(q->value(2).toInt());
    clock_events.back()->load();
  }
  delete q;
  return true;
}


bool RDClock::save()
{
  if(clock_short_name.isEmpty()) {
    clock_short_name=clock_name.left(3);
  }
  QString sql=QString("select `NAME` from `CLOCKS` where ")+
    "`NAME`='"+RDEscapeString(clock_name)+"'";
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(q->first()) {
    delete q;
    sql=QString("update `CLOCKS` set ")+
      "`SHORT_NAME`='"+RDEscapeString(clock_short_name)+"',"+
      "`COLOR`='"+RDEscapeString(clock_color.name())+"',"+
      QString::asprintf("`ARTISTSEP`=%d,",artistsep)+
      "`REMARKS`='"+RDEscapeString(clock_remarks)+"' "+
      "where `NAME`='"+RDEscapeString(clock_name)+"'";
    RDSqlQuery::apply(sql);

    sql=QString("delete from `CLOCK_LINES` where ")+
      "`CLOCK_NAME`='"+RDEscapeString(clock_name)+"'";
    RDSqlQuery::apply(sql);
  }
  else {
    delete q;
    sql=QString("insert into `CLOCKS` set ")+
      "`NAME`='"+RDEscapeString(clock_name)+"',"+
      "`SHORT_NAME`='"+RDEscapeString(clock_short_name)+"',"+
      "`COLOR`='"+RDEscapeString(clock_color.name())+"',"+
      QString::asprintf("`ARTISTSEP`=%d,",artistsep)+
      "`REMARKS`='"+RDEscapeString(clock_remarks)+"'";
    RDSqlQuery::apply(sql);
  }
  sql=QString("delete from `CLOCK_LINES` where ")+
    "`CLOCK_NAME`='"+RDEscapeString(clock_name)+"'";
  RDSqlQuery::apply(sql);
  for(int i=0;i<clock_events.size();i++) {
    sql=QString("insert into `CLOCK_LINES` set ")+
      "`CLOCK_NAME`='"+RDEscapeString(clock_name)+"',"+
      "`EVENT_NAME`='"+RDEscapeString(clock_events.at(i)->name())+"',"+
      QString::asprintf("`START_TIME`=%d,",
			QTime(0,0,0).msecsTo(clock_events.at(i)->startTime()))+
      QString::asprintf("`LENGTH`=%d",clock_events.at(i)->length());
    RDSqlQuery::apply(sql);
  }
  return true;
}


int RDClock::insert(const QString &event_name,const QTime &time,int len)
{
  int line=preInsert(event_name,time);

  if(line<0) {
    return -1;
  }
  execInsert(line,event_name,time,len);

  return line;
}


void RDClock::remove(int line)
{
  delete clock_events[line];
  clock_events.removeAt(line);
}


bool RDClock::validate(const QTime &start_time,int length,int except_line)
{
  QTime end_time=start_time.addMSecs(length);
  QTime end;
  for(int i=0;i<clock_events.size();i++) {
    if(i!=except_line) {
      end=clock_events.at(i)->startTime().
	addMSecs(clock_events.at(i)->length());
      if((start_time>=clock_events.at(i)->startTime())&&(start_time<end)) {
	return false;
      }
      if(((end_time>clock_events.at(i)->startTime())&&
	  (end_time<end))||
	 ((start_time<end)&&(end_time>end))) {
	return false;
      }
    }
  }   
  return true;
}


bool RDClock::generateLog(int hour,const QString &logname,
			  const QString &svc_name,QString *errors)
{
  RDEventLine eventline(clock_station);
  int event_count=0;

  // Check if we have an active generation cache
  RDLogGenerationCache *cache = RDLogGenerationCache::instance();
  bool use_cache = cache->isInitialized();

  if (use_cache && cache->hasClockLines(clock_name)) {
    // Use cached clock lines - NO database queries
    QList<RDLogGenerationCache::ClockLineEntry> lines = cache->getClockLines(clock_name);
    for (int i = 0; i < lines.size(); i++) {
      const RDLogGenerationCache::ClockLineEntry &entry = lines[i];
      eventline.setName(entry.event_name);
      eventline.loadFromGenerationCache();
      eventline.setStartTime(QTime(0,0,0).addMSecs(entry.start_time).
			     addSecs(3600*hour));
      eventline.setLength(entry.length);
      eventline.generateLogCached(logname,svc_name,clock_name,errors);
      eventline.clear();
      event_count++;
    }
  } else {
    // Fallback to database queries (non-cached path)
    QString sql=QString("select ")+
      "`EVENT_NAME`,"+  // 00
      "`START_TIME`,"+  // 01
      "`LENGTH` "+      // 02
      "from `CLOCK_LINES` where "+
      "`CLOCK_NAME`='"+RDEscapeString(clock_name)+"' "+
      "order by `START_TIME`";
    RDSqlQuery *q=new RDSqlQuery(sql);
    while(q->next()) {
      QString event_name=q->value(0).toString();
      eventline.setName(event_name);
      eventline.load();
      eventline.setStartTime(QTime(0,0,0).addMSecs(q->value(1).toInt()).
			     addSecs(3600*hour));
      eventline.setLength(q->value(2).toInt());
      eventline.generateLog(logname,svc_name,clock_name,errors);
      eventline.clear();
      event_count++;
    }
    delete q;
  }
  
  return true;
}


int RDClock::preInsert(const QString &event_name,const QTime &time) const
{
  int line=-1;

  QString sql=QString("select `NAME` from `EVENTS` where ")+
    "`NAME`='"+RDEscapeString(event_name)+"'";
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(!q->first()) {
    delete q;
    return -1;
  }
  delete q;
  if((clock_events.size()==0)||(time<clock_events.at(0)->startTime())) {
    return 0;
  }
  else {
    for(int i=0;i<clock_events.size()-1;i++) {
      if((time>clock_events.at(i)->startTime())&&
	 (time<clock_events.at(i+1)->startTime())) {
	return i+1;
      }
    }
    if(line<0) {
      return clock_events.size();
    }
  }
  return -1;
}


void RDClock::execInsert(int line,const QString &event_name,const QTime &time,
			 int len)
{
  if(line>=clock_events.size()) {
    clock_events.push_back(new RDEventLine(clock_station));
  }
  else {
    clock_events.insert(line,new RDEventLine(clock_station));
  }
  clock_events.at(line)->setName(event_name);
  clock_events.at(line)->setStartTime(time);
  clock_events.at(line)->setLength(len);
  clock_events.at(line)->load();
}


bool RDClock::generateLog(int hour,const QString &logname,
			  const QString &svc_name,QString *errors,
			  RDClockCache *clock_cache,
			  RDEventLineCache *event_cache,
			  RDCartCache *cart_cache)
{
  // If no caches provided, fall back to original implementation
  if(clock_cache==NULL || event_cache==NULL || cart_cache==NULL) {
    return generateLog(hour,logname,svc_name,errors);
  }

  RDEventLine eventline(clock_station);

  // Get clock data from cache instead of querying CLOCK_LINES
  const RDClockCache::ClockData* clock_data=clock_cache->getClock(clock_name);
  if(clock_data==NULL) {
    // Clock not in cache, fall back to original
    return generateLog(hour,logname,svc_name,errors);
  }
  
  for(int i=0;i<clock_data->events.size();i++) {
    const RDClockCache::ClockEvent &evt=clock_data->events.at(i);
    
    // Load event data from cache
    const RDEventLineCache::EventData* event_data=event_cache->getEvent(evt.event_name);
    if(event_data!=NULL) {
      eventline.setName(evt.event_name);
      // Load event properties from cache
      eventline.loadFromCache(*event_data);
      eventline.setStartTime(QTime(0,0,0).addMSecs(evt.start_time_ms).
			     addSecs(3600*hour));
      eventline.setLength(evt.length_ms);
      // Use cached generateLog
      eventline.generateLog(logname,svc_name,clock_name,errors,
			    event_cache,cart_cache,clock_cache);
      eventline.clear();
    }
    else {
      // Fall back to database load if event not in cache
      eventline.setName(evt.event_name);
      eventline.load();
      eventline.setStartTime(QTime(0,0,0).addMSecs(evt.start_time_ms).
			     addSecs(3600*hour));
      eventline.setLength(evt.length_ms);
      eventline.generateLog(logname,svc_name,clock_name,errors);
      eventline.clear();
    }
  }
  return true;
}
