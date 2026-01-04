// rdevent_line.cpp
//
// Abstract a Rivendell Log Manager Event
//
//   (C) Copyright 2002-2022 Fred Gleason <fredg@paravelsystems.com>
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

#include <QObject>

#include "rdapplication.h"
#include "rdconf.h"
#include "rdcart.h"
#include "rdcart_cache.h"
#include "rdclock_cache.h"
#include "rddb.h"
#include "rdescape_string.h"
#include "rdevent.h"
#include "rdevent_line.h"
#include "rdevent_line_cache.h"
#include "rdeventimportlist.h"
#include "rdimporter_cache.h"
#include "rdloggenerationcache.h"
#include "rdschedcartlist.h"

RDEventLine::RDEventLine(RDStation *station)
{
  event_station=station;
  event_preimport_list=new RDEventImportList();
  event_postimport_list=new RDEventImportList();
  clear();
}


QString RDEventLine::name() const
{
  return event_name;
}


void RDEventLine::setName(const QString &name)
{
  event_name=name;
  event_preimport_list->setEventName(event_name);
  event_preimport_list->setType(RDEventImportList::PreImport);
  event_postimport_list->setEventName(event_name);
  event_postimport_list->setType(RDEventImportList::PostImport);
}


int RDEventLine::preposition() const
{
  return event_preposition;
}


void RDEventLine::setPreposition(int offset)
{
  event_preposition=offset;
}


RDLogLine::TimeType RDEventLine::timeType() const
{
  return event_time_type;
}


void RDEventLine::setTimeType(RDLogLine::TimeType type)
{
  event_time_type=type;
}


int RDEventLine::graceTime() const
{
  return event_grace_time;
}


void RDEventLine::setGraceTime(int offset)
{
  event_grace_time=offset;
}


bool RDEventLine::useAutofill() const
{
  return event_use_autofill;
}


void RDEventLine::setUseAutofill(bool state)
{
  event_use_autofill=state;
}


int RDEventLine::autofillSlop() const
{
  return event_autofill_slop;
}


void RDEventLine::setAutofillSlop(int slop)
{
  event_autofill_slop=slop;
}


bool RDEventLine::useTimescale() const
{
  return event_use_timescale;
}


void RDEventLine::setUseTimescale(bool state)
{
  event_use_timescale=state;
}


RDEventLine::ImportSource RDEventLine::importSource() const
{
  return event_import_source;
}


void RDEventLine::setImportSource(RDEventLine::ImportSource src)
{
  event_import_source=src;
}


int RDEventLine::startSlop() const
{
  return event_start_slop;
}


void RDEventLine::setStartSlop(int slop)
{
  event_start_slop=slop;
}


int RDEventLine::endSlop() const
{
  return event_end_slop;
}


void RDEventLine::setEndSlop(int slop)
{
  event_end_slop=slop;
}


RDLogLine::TransType RDEventLine::firstTransType() const
{
  return event_first_transtype;
}


void RDEventLine::setFirstTransType(RDLogLine::TransType trans)
{
  event_first_transtype=trans;
}


RDLogLine::TransType RDEventLine::defaultTransType() const
{
  return event_default_transtype;
}


void RDEventLine::setDefaultTransType(RDLogLine::TransType trans)
{
  event_default_transtype=trans;
}


QColor RDEventLine::color() const
{
  return event_color;
}


void RDEventLine::setColor(const QColor &color)
{
  event_color=color;
}


QString RDEventLine::schedGroup() const
{
  return event_sched_group;
}


void RDEventLine::setSchedGroup(QString str)
{
  event_sched_group=str;
}


QString RDEventLine::HaveCode() const
{
  return event_have_code;
}


void RDEventLine::setHaveCode(QString str)
{
  event_have_code=str;
}

QString RDEventLine::HaveCode2() const
{
  return event_have_code2;
}


void RDEventLine::setHaveCode2(QString str)
{
  event_have_code2=str;
}

unsigned RDEventLine::titleSep() const
{
  return event_title_sep;
}


void RDEventLine::setTitleSep(unsigned titlesep)
{
  event_title_sep=titlesep;
}


QTime RDEventLine::startTime() const
{
  return event_start_time;
}


void RDEventLine::setStartTime(const QTime &time)
{
  event_start_time=time;
}


int RDEventLine::length() const
{
  return event_length;
}


void RDEventLine::setLength(int msecs)
{
  event_length=msecs;
}


void RDEventLine::clear()
{
   event_name="";
   event_preposition=0;
   event_time_type=RDLogLine::Relative;
   event_grace_time=0;
   event_use_autofill=false;
   event_use_timescale=false;
   event_import_source=RDEventLine::None;
   event_start_slop=0;
   event_end_slop=0;
   event_first_transtype=RDLogLine::Segue;
   event_default_transtype=RDLogLine::Segue;
   event_color=QColor();
   event_preimport_list->clear();
   event_postimport_list->clear();
   event_start_time=QTime(0,0,0,0);
   event_length=0;
   event_autofill_slop=-1;
   event_sched_group="";
   event_have_code="";
   event_have_code2="";
   event_artist_sep=15;
   event_title_sep=100;
   event_nested_event="";
   event_using_bypass=false;
}


bool RDEventLine::load()
{
  QString sql=QString("select ")+
    "`PREPOSITION`,"+         // 00
    "`TIME_TYPE`,"+           // 01
    "`GRACE_TIME`,"+          // 02
    "`USE_AUTOFILL`,"+        // 03
    "`USE_TIMESCALE`,"+       // 04
    "`IMPORT_SOURCE`,"+       // 05
    "`START_SLOP`,"+          // 06
    "`END_SLOP`,"+            // 07
    "`FIRST_TRANS_TYPE`,"+    // 08
    "`DEFAULT_TRANS_TYPE`,"+  // 09
    "`COLOR`,"+               // 10
    "`AUTOFILL_SLOP`,"+       // 11
    "`NESTED_EVENT`,"+        // 12
    "`SCHED_GROUP`,"+         // 13
    "`ARTIST_SEP`,"+          // 14
    "`TITLE_SEP`,"+           // 15
    "`HAVE_CODE`,"+           // 16
    "`HAVE_CODE2` "+          // 17
    "from `EVENTS` where "+
    "`NAME`='"+RDEscapeString(event_name)+"'";
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(!q->first()) {
    fprintf(stderr,"RDEventLine::load() EVENT NOT FOUND: %s\n",
	    event_name.toUtf8().constData());
    delete q;
    return false;
  }
  event_preposition=q->value(0).toInt();
  event_time_type=(RDLogLine::TimeType)q->value(1).toInt();
  event_grace_time=q->value(2).toInt();
  event_use_autofill=RDBool(q->value(3).toString());
  event_use_timescale=RDBool(q->value(4).toString());
  event_import_source=(RDEventLine::ImportSource)q->value(5).toInt();
  event_start_slop=q->value(6).toInt();
  event_end_slop=q->value(7).toInt();
  event_first_transtype=(RDLogLine::TransType)q->value(8).toInt();
  event_default_transtype=(RDLogLine::TransType)q->value(9).toInt();
  if(q->value(10).isNull()) {
    event_color=QColor();
  }
  else {
    event_color=QColor(q->value(10).toString());
  }
  event_autofill_slop=q->value(11).toInt();
  event_nested_event=q->value(12).toString();
  event_sched_group=q->value(13).toString();
  event_artist_sep=q->value(14).toInt();
  event_title_sep=q->value(15).toInt();
  event_have_code=q->value(16).toString();
  event_have_code2=q->value(17).toString();
  delete q;

  event_preimport_list->load();
  event_postimport_list->load();

  event_using_bypass=false;

  return true;
}


bool RDEventLine::loadBypass()
{
  //
  // Internal read-only event for processing inline traffic imports
  //
  event_preposition=0;
  event_time_type=RDLogLine::Relative;
  event_grace_time=0;
  event_use_autofill=false;
  event_use_timescale=false;
  event_import_source=RDEventLine::Traffic;
  event_start_slop=0;
  event_end_slop=0;
  event_first_transtype=RDLogLine::Play;
  event_default_transtype=RDLogLine::Play;
  event_color=QColor();
  event_preimport_list->clear();
  event_postimport_list->clear();
  event_start_time=QTime(0,0,0,0);
  event_length=0;
  event_autofill_slop=-1;
  event_sched_group="";
  event_have_code="";
  event_have_code2="";
  event_artist_sep=15;
  event_title_sep=100;
  event_nested_event="";

  event_using_bypass=true;

  return true;
}


bool RDEventLine::loadFromCache(const RDEventLineCache::EventData &data)
{
  event_preposition=data.preposition;
  event_time_type=(RDLogLine::TimeType)data.time_type;
  event_grace_time=data.grace_time;
  event_use_autofill=data.use_autofill;
  event_use_timescale=data.use_timescale;
  event_import_source=(RDEventLine::ImportSource)data.import_source;
  event_start_slop=data.start_slop;
  event_end_slop=data.end_slop;
  event_first_transtype=(RDLogLine::TransType)data.first_trans_type;
  event_default_transtype=(RDLogLine::TransType)data.default_trans_type;
  event_color=data.color;
  event_autofill_slop=data.autofill_slop;
  event_nested_event=data.nested_event;
  event_sched_group=data.sched_group;
  event_artist_sep=data.artist_sep;
  event_title_sep=data.title_sep;
  event_have_code=data.have_code;
  event_have_code2=data.have_code2;

  // Note: Pre/post import lists are NOT loaded from cache
  // They would need to be loaded from the EVENT_LINES table
  // For now, load them from database for correctness
  event_preimport_list->load();
  event_postimport_list->load();

  event_using_bypass=false;

  return true;
}


bool RDEventLine::loadFromGenerationCache()
{
  RDLogGenerationCache *cache = RDLogGenerationCache::instance();
  const RDCachedEvent *evt = cache->getEvent(event_name);
  
  if (evt == nullptr) {
    // Fall back to database load
    return load();
  }
  
  event_preposition = evt->preposition;
  event_time_type = (RDLogLine::TimeType)evt->time_type;
  event_grace_time = evt->grace_time;
  event_use_autofill = evt->use_autofill;
  event_use_timescale = evt->use_timescale;
  event_import_source = (RDEventLine::ImportSource)evt->import_source;
  event_start_slop = evt->start_slop;
  event_end_slop = evt->end_slop;
  event_first_transtype = (RDLogLine::TransType)evt->first_trans_type;
  event_default_transtype = (RDLogLine::TransType)evt->default_trans_type;
  if (!evt->color.isEmpty()) {
    event_color = QColor(evt->color);
  } else {
    event_color = QColor();
  }
  event_autofill_slop = evt->autofill_slop;
  event_nested_event = evt->nested_event;
  event_sched_group = evt->sched_group;
  event_artist_sep = evt->artist_sep;
  event_title_sep = evt->title_sep;
  event_have_code = evt->have_code;
  event_have_code2 = evt->have_code2;
  
  // Load import lists from cache
  event_preimport_list->clear();
  for (int i = 0; i < evt->preimport_list.size(); i++) {
    const RDCachedImportItem &item = evt->preimport_list[i];
    RDEventImportItem *importItem = new RDEventImportItem();
    importItem->setEventType((RDLogLine::Type)item.event_type);
    importItem->setCartNumber(item.cart_number);
    importItem->setTransType((RDLogLine::TransType)item.trans_type);
    importItem->setMarkerComment(item.marker_comment);
    event_preimport_list->takeItem(i, importItem);
  }
  // Add sentinel item at end (required - loops use size()-1)
  event_preimport_list->takeItem(evt->preimport_list.size(), new RDEventImportItem(true));
  
  event_postimport_list->clear();
  for (int i = 0; i < evt->postimport_list.size(); i++) {
    const RDCachedImportItem &item = evt->postimport_list[i];
    RDEventImportItem *importItem = new RDEventImportItem();
    importItem->setEventType((RDLogLine::Type)item.event_type);
    importItem->setCartNumber(item.cart_number);
    importItem->setTransType((RDLogLine::TransType)item.trans_type);
    importItem->setMarkerComment(item.marker_comment);
    event_postimport_list->takeItem(i, importItem);
  }
  // Add sentinel item at end (required - loops use size()-1)
  event_postimport_list->takeItem(evt->postimport_list.size(), new RDEventImportItem(true));
  
  event_using_bypass = false;
  
  return true;
}


bool RDEventLine::save(RDConfig *config)
{
  if(event_using_bypass) {
    return false;
  }

  QString sql=QString("select `NAME` from `EVENTS` where ")+
    "`NAME`='"+RDEscapeString(event_name)+"'";
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(q->first()) {
    sql=QString("update `EVENTS` set ")+
      QString::asprintf("`PREPOSITION`=%d,",event_preposition)+
      QString::asprintf("`TIME_TYPE`=%d,",event_time_type)+
      QString::asprintf("`GRACE_TIME`=%d,",event_grace_time)+
      "`USE_AUTOFILL`='"+RDYesNo(event_use_autofill)+"`,"+
      "`USE_TIMESCALE`='"+RDYesNo(event_use_timescale)+"',"+
      QString::asprintf("`IMPORT_SOURCE`=%d,",event_import_source)+
      QString::asprintf("`START_SLOP`=%d,",event_start_slop)+
      QString::asprintf("`END_SLOP'=%d,",event_end_slop)+
      QString::asprintf("`FIRST_TRANS_TYPE`=%d,",event_first_transtype)+
      QString::asprintf("`DEFAULT_TRANS_TYPE`=%d,",event_default_transtype)+
      "`COLOR`='"+RDEscapeString(event_color.name())+"'"+
      QString::asprintf("`AUTOFILL_SLOP`=%d,",event_autofill_slop)+
      "`NESTED_EVENT`='"+RDEscapeString(event_nested_event)+"',"+
      "`SCHED_GROUP`='"+RDEscapeString(event_sched_group)+"',"+
      QString::asprintf("`ARTIST_SEP`=%d,",event_artist_sep)+
      QString::asprintf("`TITLE_SEP`=%d,",event_title_sep)+
      "`HAVE_CODE`='"+RDEscapeString(event_have_code)+"',"+
      "`HAVE_CODE2`='"+RDEscapeString(event_have_code2)+"' "+
      "where `NAME`='"+RDEscapeString(event_name)+"'";
  }
  else {
    sql=QString("insert into `EVENTS` set ")+
      "`NAME`='"+RDEscapeString(event_name)+"',"+
      QString::asprintf("`PREPOSITION`=%d,",event_preposition)+
      QString::asprintf("`TIME_TYPE`=%d,",event_time_type)+
      QString::asprintf("`GRACE_TIME`=%d,",event_grace_time)+
      "`USE_AUTOFILL`='"+RDYesNo(event_use_autofill)+"',"+
      "`USE_TIMESCALE`='"+RDYesNo(event_use_timescale)+"',"+
      QString::asprintf("`IMPORT_SOURCE`=%d,",event_import_source)+
      QString::asprintf("`START_SLOP`=%d,",event_start_slop)+
      QString::asprintf("`END_SLOP`=%d,",event_end_slop)+
      QString::asprintf("`FIRST_TRANS_TYPE`=%d,",event_first_transtype)+
      QString::asprintf("`DEFAULT_TRANS_TYPE`=%d,",event_default_transtype)+
      "`COLOR`='"+RDEscapeString(event_color.name())+"',"+
      QString::asprintf("`AUTOFILL_SLOP`=%d,",event_autofill_slop)+
      "`SCHED_GROUP`='"+RDEscapeString(event_sched_group)+"' where "+
      "`NAME`='"+RDEscapeString(event_name)+"'";
  }
  delete q;
  q=new RDSqlQuery(sql);
  if(!q->isActive()) {
    delete q;
    return false;
  }
  delete q;
  event_preimport_list->save();
  event_postimport_list->save();
  return true;
}


bool RDEventLine::generateLog(const QString &logname,const QString &svcname,
			      const QString &clockname,QString *report)
{
  QString sql;
  RDSqlQuery *q;
  QTime fill_start_time;
  QString import_table;
  __RDEventLine_GeneratorState *state=new __RDEventLine_GeneratorState();
  state->start_time=event_start_time;
  state->length=0;
  state->count=0;
  state->trans_type=event_first_transtype;
  state->time_type=event_time_type;
  state->link_type=RDLogLine::MusicLink;
  state->grace_time=event_grace_time;
  state->link_id=0;

  //
  // Get Current Count and Link ID
  //
  sql=QString("select `COUNT` from `LOG_LINES` where ")+
    "`LOG_NAME`='"+RDEscapeString(logname)+"' "+
    "order by `COUNT` desc";
  q=new RDSqlQuery(sql);
  if(q->first()) {
    state->count=q->value(0).toInt()+1;
  }
  delete q;

  sql=QString("select `LINK_ID` from `LOG_LINES` where ")+
    "`LOG_NAME`='"+RDEscapeString(logname)+"' && "+
    "`LINK_ID`>=0 "+
    "order by `LINK_ID` desc";
  q=new RDSqlQuery(sql);
  if(q->first()) {
    state->link_id=q->value(0).toInt()+1;
  }
  delete q;

  //
  // Override Default Parameters if Preposition Set
  //
  if(event_preposition>=0) {
    state->time_type=RDLogLine::Hard;
    state->grace_time=-1;
    if(QTime(0,0,0).msecsTo(state->start_time)>event_preposition) {
      state->start_time=state->start_time.addMSecs(-event_preposition);
    }
    else {
      state->start_time=QTime();
    }
  }

  //
  // Pre-Import Carts
  //
  state->length=0;
  for(int i=0;i<event_preimport_list->size()-1;i++) {
    RDEventImportItem *i_item=event_preimport_list->item(i);
    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`='"+RDEscapeString(logname)+"',"+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",i_item->eventType())+
      QString::asprintf("`SOURCE`=%d,",RDLogLine::Template)+
      QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(state->start_time))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`CART_NUMBER`=%u,",i_item->cartNumber())+
      QString::asprintf("`TIME_TYPE`=%d,",state->time_type)+
      QString::asprintf("`TRANS_TYPE`=%d,",i_item->transType())+
      "`COMMENT`='"+RDEscapeString(i_item->markerComment())+"',"+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    RDSqlQuery::apply(sql);
    state->count++;
    state->trans_type=event_default_transtype;
    state->time_type=RDLogLine::Relative;
    state->grace_time=-1;

    state->length+=GetLength(i_item->cartNumber());
  }

  //
  // Import Links
  //
  if(event_import_source==RDEventLine::Traffic || event_import_source==RDEventLine::Music) {
    switch(event_import_source) {
    case RDEventLine::Traffic:
      state->link_type=RDLogLine::TrafficLink;
      break;
	  
    case RDEventLine::Music:
      state->link_type=RDLogLine::MusicLink;
      break;
	  
    default:
      break;
    }
    QTime end_start_time=event_start_time.addMSecs(event_length);

    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`='"+RDEscapeString(logname)+"',"+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",state->link_type)+
      QString::asprintf("`SOURCE`=%d,",RDLogLine::Template)+
      QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(state->start_time))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`TIME_TYPE`=%d,",state->time_type)+
      QString::asprintf("`TRANS_TYPE`=%d,",state->trans_type)+
      "`LINK_EVENT_NAME`='"+RDEscapeString(event_name)+"',"+
      QString::asprintf("`LINK_START_TIME`=%d,",
			QTime(0,0,0).msecsTo(event_start_time))+
      QString::asprintf("`LINK_LENGTH`=%d,",
			event_start_time.msecsTo(end_start_time))+
      QString::asprintf("`LINK_ID`=%d,",state->link_id)+
      QString::asprintf("`LINK_START_SLOP`=%d,",event_start_slop)+
      QString::asprintf("`LINK_END_SLOP`=%d,",event_end_slop)+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    q=new RDSqlQuery(sql);
    delete q;
    state->count++;
    state->start_time=state->start_time.addMSecs(event_length);
    state->trans_type=event_default_transtype;
    state->time_type=RDLogLine::Relative;
    state->grace_time=-1;
  }

  //
  // Music Scheduler 
  //
  if(event_import_source == RDEventLine::Scheduler) {
    GenerateMusicSchedEvent(state,logname,svcname,clockname,report);
  }

  //
  // Post-Import Carts
  //
  for(int i=0;i<event_postimport_list->size()-1;i++) {
    RDEventImportItem *i_item=event_postimport_list->item(i);
    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`='"+RDEscapeString(logname)+"',"+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",i_item->eventType())+
      QString::asprintf("`SOURCE`=%d,",RDLogLine::Template)+
      QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(state->start_time))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`CART_NUMBER`=%u,",i_item->cartNumber())+
      QString::asprintf("`TIME_TYPE`=%d,",state->time_type)+
      QString::asprintf("`TRANS_TYPE`=%d,",i_item->transType())+
      "`COMMENT`='"+RDEscapeString(i_item->markerComment())+"',"+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    RDSqlQuery::apply(sql);
    state->count++;
    state->start_time=state->start_time.addMSecs(GetLength(i_item->cartNumber()));
    state->time_type=RDLogLine::Relative;
    state->trans_type=event_default_transtype;
    state->grace_time=-1;
  }

  delete state;

  return true;
}


//
// Fully cached version of generateLog - uses singleton RDLogGenerationCache
// NO DATABASE QUERIES during execution - all data from cache
// Log lines are buffered in cache and flushed at end of log generation
//
bool RDEventLine::generateLogCached(const QString &logname,const QString &svcname,
			      const QString &clockname,QString *report)
{
  RDLogGenerationCache *cache = RDLogGenerationCache::instance();
  
  __RDEventLine_GeneratorState *state=new __RDEventLine_GeneratorState();
  state->start_time=event_start_time;
  state->length=0;
  state->count=-1;  // Will be set when first line is added
  state->trans_type=event_first_transtype;
  state->time_type=event_time_type;
  state->link_type=RDLogLine::MusicLink;
  state->grace_time=event_grace_time;
  state->link_id=cache->currentLinkId();

  //
  // Override Default Parameters if Preposition Set
  //
  if(event_preposition>=0) {
    state->time_type=RDLogLine::Hard;
    state->grace_time=-1;
    if(QTime(0,0,0).msecsTo(state->start_time)>event_preposition) {
      state->start_time=state->start_time.addMSecs(-event_preposition);
    }
    else {
      state->start_time=QTime();
    }
  }

  //
  // Pre-Import Carts - buffer to cache instead of INSERT
  //
  state->length=0;
  for(int i=0;i<event_preimport_list->size()-1;i++) {
    RDEventImportItem *i_item=event_preimport_list->item(i);
    
    int count = cache->getNextCount();  // Get count when actually adding a line
    RDPendingLogLine line;
    line.line_id = count;
    line.count = count;
    line.type = i_item->eventType();
    line.source = RDLogLine::Template;
    line.start_time = QTime(0,0,0).msecsTo(state->start_time);
    line.grace_time = state->grace_time;
    line.cart_number = i_item->cartNumber();
    line.time_type = state->time_type;
    line.trans_type = i_item->transType();
    line.comment = i_item->markerComment();
    line.event_length = event_length;
    line.link_event_name = QString();
    line.link_start_time = 0;
    line.link_length = 0;
    line.link_id = -1;
    line.link_start_slop = 0;
    line.link_end_slop = 0;
    
    cache->addLogLine(line);
    
    state->trans_type=event_default_transtype;
    state->time_type=RDLogLine::Relative;
    state->grace_time=-1;

    state->length += cache->getCartLength(i_item->cartNumber());
  }

  //
  // Import Links
  //
  if(event_import_source==RDEventLine::Traffic || event_import_source==RDEventLine::Music) {
    switch(event_import_source) {
    case RDEventLine::Traffic:
      state->link_type=RDLogLine::TrafficLink;
      break;
	  
    case RDEventLine::Music:
      state->link_type=RDLogLine::MusicLink;
      break;
	  
    default:
      break;
    }
    QTime end_start_time=event_start_time.addMSecs(event_length);

    int count = cache->getNextCount();  // Get count when actually adding a line
    RDPendingLogLine line;
    line.line_id = count;
    line.count = count;
    line.type = state->link_type;
    line.source = RDLogLine::Template;
    line.start_time = QTime(0,0,0).msecsTo(state->start_time);
    line.grace_time = state->grace_time;
    line.cart_number = 0;
    line.time_type = state->time_type;
    line.trans_type = state->trans_type;
    line.comment = QString();
    line.event_length = event_length;
    line.link_event_name = event_name;
    line.link_start_time = QTime(0,0,0).msecsTo(event_start_time);
    line.link_length = event_start_time.msecsTo(end_start_time);
    line.link_id = cache->getNextLinkId();
    line.link_start_slop = event_start_slop;
    line.link_end_slop = event_end_slop;
    
    cache->addLogLine(line);
    
    state->start_time=state->start_time.addMSecs(event_length);
    state->trans_type=event_default_transtype;
    state->time_type=RDLogLine::Relative;
    state->grace_time=-1;
  }

  //
  // Music Scheduler 
  //
  if(event_import_source == RDEventLine::Scheduler) {
    GenerateMusicSchedEventCached(state,logname,svcname,clockname,report);
  }

  //
  // Post-Import Carts
  //
  for(int i=0;i<event_postimport_list->size()-1;i++) {
    RDEventImportItem *i_item=event_postimport_list->item(i);
    
    int count = cache->getNextCount();  // Get count when actually adding a line
    RDPendingLogLine line;
    line.line_id = count;
    line.count = count;
    line.type = i_item->eventType();
    line.source = RDLogLine::Template;
    line.start_time = QTime(0,0,0).msecsTo(state->start_time);
    line.grace_time = state->grace_time;
    line.cart_number = i_item->cartNumber();
    line.time_type = state->time_type;
    line.trans_type = i_item->transType();
    line.comment = i_item->markerComment();
    line.event_length = event_length;
    line.link_event_name = QString();
    line.link_start_time = 0;
    line.link_length = 0;
    line.link_id = -1;
    line.link_start_slop = 0;
    line.link_end_slop = 0;
    
    cache->addLogLine(line);
    
    state->start_time=state->start_time.addMSecs(cache->getCartLength(i_item->cartNumber()));
    state->time_type=RDLogLine::Relative;
    state->trans_type=event_default_transtype;
    state->grace_time=-1;
  }

  delete state;

  return true;
}

void RDEventLine::linkLogCached(RDLogModel *e,RDLog *log,const QString &svcname,
			  RDLogLine *link_logline,const QString &track_str,
			  const QString &label_cart,const QString &track_cart,
			  QString *errors,bool include_markers)
{
  QString sql;
  RDSqlQuery *q;
  RDLogLine *logline=NULL;
  RDLog::Source log_src=RDLog::SourceMusic;

  // Get the cache instance for cart length lookups
  RDLogGenerationCache *cache = RDLogGenerationCache::instance();

  //
  // Initial Import Parameters
  //
  RDLogLine::Source event_src=RDLogLine::Manual;
  switch(event_import_source) {
  case RDEventLine::Music:
    event_src=RDLogLine::Music;
    log_src=RDLog::SourceMusic;
    break;

  case RDEventLine::Traffic:
    event_src=RDLogLine::Traffic;
    log_src=RDLog::SourceTraffic;
    break;

  case RDEventLine::Scheduler:
  case RDEventLine::None:
    break;
  }
  RDLogLine::TimeType time_type=link_logline->timeType();
  RDLogLine::TransType trans_type=link_logline->transType();
  int grace_time=link_logline->graceTime();
  QTime time=link_logline->startTime(RDLogLine::Logged);

  //
  // Get slop factors for inline traffic breaks (using cache if available)
  //
  int inline_start_slop=0;
  int inline_end_slop=0;
  if(event_import_source==RDEventLine::Music && !event_nested_event.isEmpty()) {
    // Use cache for nested event lookup
    if(cache->isInitialized() && cache->hasEvent(event_nested_event)) {
      const RDCachedEvent *nested_data = cache->getEvent(event_nested_event);
      inline_start_slop=nested_data->start_slop;
      inline_end_slop=nested_data->end_slop;
    } else {
      // Fall back to database query
      sql=QString("select ")+
        "`START_SLOP`,"+  // 00
        "`END_SLOP` "+    // 01
        "from `EVENTS` where "+
        "`NAME`='"+RDEscapeString(event_nested_event)+"'";
      RDSqlQuery *q1=new RDSqlQuery(sql);
      if(q1->first()) {
        inline_start_slop=q1->value(0).toInt();
        inline_end_slop=q1->value(1).toInt();
      }
      delete q1;
    }
  }

  //
  // Insert Parent Link (using cached include_markers value)
  //
  if(include_markers) {
    e->insert(e->lineCount(),1);
    logline=new RDLogLine();
    *logline=*link_logline;
    logline->setId(e->nextId());
    if(!label_cart.isEmpty()) {
      logline->setMarkerComment(label_cart);
    }
    *(e->logLine(e->lineCount()-1))=*logline;
    delete logline;
    logline=NULL;

    //
    // Clear Leading Event Values
    //
    time_type=RDLogLine::Relative;
    trans_type=event_default_transtype;
    grace_time=-1;
  }
  else {
    //
    // Propagate Leading Event Values to Next Event
    //
    time_type=link_logline->timeType();
    trans_type=link_logline->transType();
    grace_time=link_logline->graceTime();
  }

  //
  // Calculate Event Time Boundaries
  //
  int start_start_hour=link_logline->linkStartTime().hour();
  int start_start_secs=60000*link_logline->linkStartTime().minute()+
    1000*link_logline->linkStartTime().second();
  int end_start_secs=start_start_secs+link_logline->linkLength();

  //
  // Apply Slop Factors
  //
  if(start_start_hour==link_logline->linkStartTime().
     addMSecs(-link_logline->linkStartSlop()).hour()) {
    start_start_secs-=link_logline->linkStartSlop();
  }
  else {
    start_start_secs=0;  // So we don't slop over into the previous hour
  }
  end_start_secs+=link_logline->linkEndSlop();

  //
  // Load Matching Events and Insert into Log (using in-memory cache)
  //
  RDImporterCache *importerCache=RDImporterCache::instance();
  QList<RDImporterLine*> matchingLines=
    importerCache->getMatchingLines(start_start_hour,
				    start_start_secs/1000,
				    end_start_secs/1000);
  QList<int> usedLineIds;

  for(int i=0;i<matchingLines.size();i++) {
    const RDImporterLine *line=matchingLines.at(i);
    usedLineIds.append(line->id);

    // Use cached cart length if available
    int length;
    if(cache->isInitialized()) {
      int cached_len = cache->getCartLength(line->cart_number);
      length = (cached_len >= 0) ? cached_len : line->length;
    } else {
      length = GetLength(line->cart_number, line->length);
    }

    //
    // Inline Traffic Break
    //
    if(line->type==RDLogLine::TrafficLink) {
      if((!event_nested_event.isEmpty()&&(event_nested_event!=event_name))) {
	e->insert(e->lineCount(),1);
	logline=e->logLine(e->lineCount()-1);
	logline->setId(e->nextId());
	logline->setStartTime(RDLogLine::Logged,time);
	logline->setType(RDLogLine::TrafficLink);
	logline->setSource(event_src);
	logline->setEventLength(event_length);
	logline->setLinkEventName(event_nested_event);
	logline->setLinkStartTime(line->link_start_time);
	logline->setLinkLength(line->link_length);
	logline->setLinkStartSlop(inline_start_slop);
	logline->setLinkEndSlop(inline_end_slop);
	logline->setLinkId(link_logline->linkId());
	logline->setLinkEmbedded(true);
      }
    }

    //
    // Voicetrack Marker
    //
    if(line->type==RDLogLine::Track) {
      e->insert(e->lineCount(),1);
      logline=e->logLine(e->lineCount()-1);
      logline->setId(e->nextId());
      logline->setStartTime(RDLogLine::Logged,time);
      logline->setType(RDLogLine::Track);
      logline->setSource(event_src);
      logline->setMarkerComment(line->title);
      logline->setEventLength(event_length);
      logline->setLinkEventName(event_name);
      logline->setLinkStartTime(link_logline->linkStartTime());
      logline->setLinkLength(link_logline->linkLength());
      logline->setLinkStartSlop(link_logline->linkStartSlop());
      logline->setLinkEndSlop(link_logline->linkEndSlop());
      logline->setLinkId(link_logline->linkId());
      logline->setLinkEmbedded(true);
    }

    //
    // Label/Note Cart
    //
    if(line->type==RDLogLine::Marker) {
      e->insert(e->lineCount(),1);
      logline=e->logLine(e->lineCount()-1);
      logline->setId(e->nextId());
      logline->setStartTime(RDLogLine::Logged,time);
      logline->setType(RDLogLine::Marker);
      logline->setSource(event_src);
      logline->setMarkerComment(line->title);
      logline->setEventLength(event_length);
      logline->setLinkEventName(event_name);
      logline->setLinkStartTime(link_logline->linkStartTime());
      logline->setLinkLength(link_logline->linkLength());
      logline->setLinkStartSlop(link_logline->linkStartSlop());
      logline->setLinkEndSlop(link_logline->linkEndSlop());
      logline->setLinkId(link_logline->linkId());
      logline->setLinkEmbedded(true);
    }

    //
    // Cart
    //
    if(line->type==RDLogLine::Cart) {
      e->insert(e->lineCount(),1);
      logline=e->logLine(e->lineCount()-1);
      logline->setId(e->nextId());
      logline->setSource(event_src);
      logline->
	setStartTime(RDLogLine::Logged,
		     QTime(start_start_hour,0,0).addSecs(line->start_secs));
      logline->setType(RDLogLine::Cart);
      logline->setCartNumber(line->cart_number);
      logline->setExtStartTime(QTime(0,0,0).addSecs(3600*start_start_hour+
					       line->start_secs));
      logline->setExtLength(line->length);
      logline->setExtData(line->ext_data.trimmed());
      logline->setExtEventId(line->ext_event_id.trimmed());
      logline->setExtAnncType(line->ext_annc_type.trimmed());
      logline->setExtCartName(line->ext_cart_name.trimmed());
      logline->setEventLength(event_length);
      logline->setLinkEventName(event_name);
      logline->setLinkStartTime(link_logline->linkStartTime());
      logline->setLinkLength(link_logline->linkLength());
      logline->setLinkStartSlop(link_logline->linkStartSlop());
      logline->setLinkEndSlop(link_logline->linkEndSlop());
      logline->setLinkId(link_logline->linkId());
      logline->setLinkEmbedded(link_logline->linkEmbedded());
      time=time.addMSecs(length);
    }

    //
    // Apply Leading Event Values
    //
    if(logline!=NULL) {
      logline->setGraceTime(grace_time);
      logline->setTimeType(time_type);
      logline->setTransType(trans_type);
    }

    //
    // Clear Leading Event Values
    //
    time_type=RDLogLine::Relative;
    trans_type=event_default_transtype;
    grace_time=-1;
  }

  //
  // Mark Events as Used (in-memory cache)
  //
  importerCache->markLinesAsUsed(usedLineIds);

  //
  // Autofill (using cached cart lengths)
  //
  QTime end_time=link_logline->startTime(RDLogLine::Logged).
    addMSecs(link_logline->linkLength());
  if(event_use_autofill&&(event_start_time<=time)) {
    QTime fill_start_time=time;
    sql=QString("select ")+
      "`AUTOFILLS`.`CART_NUMBER`,"+  // 00
      "`CART`.`FORCED_LENGTH` "+     // 01
      "from `AUTOFILLS` left join `CART` "+
      "on `AUTOFILLS`.`CART_NUMBER`=`CART`.`NUMBER` where "+
      "(`AUTOFILLS`.`SERVICE`='"+RDEscapeString(svcname)+"')&&"+
      QString::asprintf("(`CART`.`FORCED_LENGTH`<=%d)&&",time.msecsTo(end_time))+
      "(`CART`.`FORCED_LENGTH`>0) "+
      "order by `CART`.`FORCED_LENGTH` desc";
    q=new RDSqlQuery(sql);
    bool fit=true;
    while(fit) {
      fit=false;
      while(q->next()&&(fill_start_time<=time)) {
	if((time.addMSecs(q->value(1).toInt())<=end_time)&&
	   (time.addMSecs(q->value(1).toInt())>time)) {
	  e->insert(e->lineCount(),1);
	  logline=e->logLine(e->lineCount()-1);
	  logline->setId(e->nextId());
	  logline->setStartTime(RDLogLine::Logged,time);
	  logline->setType(RDLogLine::Cart);
	  logline->setSource(event_src);
	  logline->setTransType(trans_type);
	  logline->setGraceTime(grace_time);
	  logline->setCartNumber(q->value(0).toUInt());
	  logline->setTimeType(time_type);
	  logline->setEventLength(event_length);
	  logline->setLinkEventName(event_name);
	  logline->setLinkStartTime(link_logline->linkStartTime());
	  logline->setLinkLength(link_logline->linkLength());
	  logline->setLinkStartSlop(link_logline->linkStartSlop());
	  logline->setLinkEndSlop(link_logline->linkEndSlop());
	  logline->setLinkId(link_logline->linkId());
	  logline->setLinkEmbedded(false);
	  time=time.addMSecs(q->value(1).toInt());
	  time_type=RDLogLine::Relative;
	  trans_type=event_default_transtype;
	  grace_time=-1;
	  q->seek(-1);
	  fit=true;
	}
      }
    }
    delete q;
  }

  //
  // Fill Check
  //
  if(event_autofill_slop>=0) {
    int slop=QTime(0,0,0).msecsTo(end_time)-QTime(0,0,0).msecsTo(time);
    if(abs(slop)>=event_autofill_slop) {
      if(slop>0) {
	*errors+=QString("  ")+rda->timeString(time)+
	  " -- \""+event_name+"\" "+QObject::tr("is underscheduled by")+" "+
	  rda->timeString(QTime(0,0,0).addMSecs(slop))+".\n";
      }
      else {
	*errors+=QString("  ")+rda->timeString(time)+
	  " -- \""+event_name+"\" "+QObject::tr("is overscheduled by")+" "+
	  rda->timeString(QTime(0,0,0).addMSecs(-slop))+".\n";
      }
    }
  }
}


QString RDEventLine::propertiesText() const
{
  QString ret;
  QString sql=QString("select ")+
    "`NAME`,"+              // 00
    "`COLOR`,"+             // 01
    "`PREPOSITION`,"+       // 02
    "`FIRST_TRANS_TYPE`,"+  // 03
    "`TIME_TYPE`,"+         // 04
    "`GRACE_TIME`,"+        // 05
    "`USE_AUTOFILL`,"+      // 06
    "`IMPORT_SOURCE`,"+     // 07
    "`NESTED_EVENT` "+      // 08
    "from `EVENTS` where "+
    "`NAME`='"+RDEscapeString(event_name)+"'";
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(q->first()) {
    ret=RDEventLine::
      propertiesText(q->value(2).toInt(),
		     (RDLogLine::TransType)q->value(3).toUInt(),
		     (RDLogLine::TimeType)q->value(4).toUInt(),
		     q->value(5).toInt(),
		     RDBool(q->value(6).toString()),
		     (RDEventLine::ImportSource)q->value(7).toUInt(),
		     !q->value(8).toString().isEmpty());  
  }
  delete q;

  return ret;
}


QString RDEventLine::propertiesText(int prepos_msec,
				    RDLogLine::TransType first_trans,
				    RDLogLine::TimeType time_type,
				    int grace_msec,
				    bool autofill,
				    RDEventLine::ImportSource import_src,
				    bool inline_tfc)
{
  QString ret="";
  QString str;

  if(prepos_msec>=0) {
    ret+=QObject::tr("Cue")+
      "(-"+QTime(0,0,0).addMSecs(prepos_msec).toString("mm:ss")+"), ";
  }

  if(time_type==RDLogLine::Hard) {
    switch(grace_msec) {
    case 0:
      ret+=QObject::tr("Timed(Start), ");
      break;

    case -1:
      ret+=QObject::tr("Timed(MakeNext), ");
      break;

    default:
      ret+=", "+QObject::tr("Timed(Wait)")+" "+
	QTime(0,0,0).addMSecs(grace_msec).toString("mm:ss")+", ";
      break;
    }
  }

  if(autofill) {
    ret+=QObject::tr("Fill")+", ";
  }

  switch(import_src) {
  case RDEventLine::Traffic:
    ret+=QObject::tr("Traffic, ");
    break;

  case RDEventLine::Music:
    ret+=QObject::tr("Music, ");
    break;

  case RDEventLine::Scheduler:
    ret+=QObject::tr("Scheduler, ");
    break;

  default:
    break;
  }

  if(inline_tfc) {
    ret+=QObject::tr("Inline Traffic, ");
  }

  return ret.left(ret.length()-2);
}


void RDEventLine::GenerateMusicSchedEvent(__RDEventLine_GeneratorState *state,
					  const QString &logname,
					  const QString &svcname,
					  const QString &clockname,
					  QString *report)
{
  QString sql;
  RDSqlQuery *q=NULL;
  int artistsep;
  int titlesep;
  int stackid;
  int counter;   		
  RDLogLine::Source source=RDLogLine::Music;
  
    
  state->start_time=state->start_time.addMSecs(state->length);

  if(event_artist_sep>=-1 && event_artist_sep<=50000) {
    artistsep = event_artist_sep;
  }
  else {
    artistsep = 15;
  }

  if(event_title_sep>=-1 && event_title_sep<=50000) {
    titlesep = event_title_sep;
  }
  else {
    titlesep = 100;
  }

  //
  // Get cache instance and next stack id
  //
  RDLogGenerationCache *cache = RDLogGenerationCache::instance();
  stackid = cache->getNextStackId();
  
  //
  // Load carts from cache (will be loaded already during init)
  //
  QString group = schedGroup();
  if (!cache->hasGroup(group)) {
    cache->loadCartsForGroup(group);
  }
  QList<RDSchedCart> cached_carts = cache->getCartsForGroup(group);
  
  //
  // Copy carts into schedCL for filtering
  //
  RDSchedCartList *schedCL = new RDSchedCartList();
  for (int i = 0; i < cached_carts.size(); i++) {
    schedCL->insertItem(cached_carts[i].cart_number, 0, 0,
                        cached_carts[i].artist, cached_carts[i].title,
                        cached_carts[i].sched_codes);
  }

  //////////////////////////////////
  //                              //
  // Add deconflicting rules here //
  //                              //
  //////////////////////////////////

  // Reduce schedCL to match requested scheduler code
  if(event_have_code!=""||event_have_code2!="") {
    QStringList codes;
    if(event_have_code!="") {
      codes << event_have_code;
    }
    if(event_have_code2!="") {
      codes << event_have_code2;
    }
    for(counter=0;counter<schedCL->getNumberOfItems();counter++) { 
      if(!schedCL->itemHasCodes(counter,codes)) {
	schedCL->removeItem(counter);
	counter--;
      }
    }
  }

  if(schedCL->getNumberOfItems()) {
    //
    // Title separation - use cache instead of database query
    //
    if(titlesep>=0) {
      schedCL->save();
      QStringList recent_titles = cache->getTitlesInRange(stackid - titlesep);
      for (int t = 0; t < recent_titles.size(); t++) {
        for(counter=0; counter<schedCL->getNumberOfItems(); counter++) { 
          if(recent_titles[t] == schedCL->getItemTitle(counter)) {
            schedCL->removeItem(counter);
            counter--;
          }
        }
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Title separation");
	if(!HaveCode().isEmpty()) {
	  *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
	}
	*report+="\n";
	schedCL->restore();
      }
    }
      
    //
    // Artist separation - use cache instead of database query
    //
    if(artistsep>=0) {
      schedCL->save();
      QStringList recent_artists = cache->getArtistsInRange(stackid - artistsep);
      for (int a = 0; a < recent_artists.size(); a++) {
        for(counter=0; counter<schedCL->getNumberOfItems(); counter++) { 
          if(recent_artists[a] == schedCL->getItemArtist(counter)) {
            schedCL->removeItem(counter);
            counter--;
          }
        }
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Artist separation");
	if(!HaveCode().isEmpty()) {
	  *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
	}
	*report+="\n";
	schedCL->restore();
      }
    }
      
    //
    // Clock Scheduler Rules - use cached rules
    //
    if (!cache->hasClockRules(clockname)) {
      cache->loadRulesForClock(clockname);
    }
    QList<RDSchedulerRule> rules = cache->getRulesForClock(clockname);
    
    for (int r = 0; r < rules.size(); r++) {
      const RDSchedulerRule &rule = rules[r];
      
      // max in a row, min wait - skip if MAX_ROW was NULL (no restriction)
      if (!rule.max_row_valid) {
        continue;  // NULL MAX_ROW means no restriction for this code
      }
      schedCL->save();	
      int range = rule.max_row + rule.min_wait; 
      int allowed = rule.max_row;
      
      QList<unsigned> carts_with_code = cache->getCartsWithCodeInRange(rule.code, stackid - range);
      if(carts_with_code.size() >= allowed || allowed == 0) {
	for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	  if(schedCL->removeIfCode(counter, rule.code)) {
	    counter--;
	  }
	}
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Max. in a Row/Min. Wait for ")+
	  rule.code+"\n";
	schedCL->restore();
      }

      // do not play after
      if(!rule.not_after.isEmpty()) {
	schedCL->save();	
	if(cache->previousItemHasCode(rule.not_after)) {
	  for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	    if(schedCL->removeIfCode(counter, rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule ")+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.not_after+"\n";
	  schedCL->restore();
	}
      }
      
      // or after
      if (!rule.or_after.isEmpty()) {
	schedCL->save();
	if(cache->previousItemHasCode(rule.or_after)) {
	  for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	    if(schedCL->removeIfCode(counter, rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule")+" "+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.or_after+"\n";
	  schedCL->restore();
	}
      }
      
      // or after II
      if (!rule.or_after_ii.isEmpty()) {
	schedCL->save();
	if(cache->previousItemHasCode(rule.or_after_ii)) {
	  for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	    if(schedCL->removeIfCode(counter, rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule")+" "+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.or_after_ii+"\n";
	  schedCL->restore();
	}
      }
    }

    ////////////////////////////////
    //                            //
    // End of deconflicting rules //
    //                            //
    ////////////////////////////////
      
    //
    // Pick a random cart from those that are remaining.
    //
    int schedpos=rand()%schedCL->getNumberOfItems();
    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`=\""+RDEscapeString(logname)+"\","+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",RDLogLine::Cart)+
      QString::asprintf("`SOURCE`=%d,",source)+
      QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(state->start_time))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`CART_NUMBER`=%u,",schedCL->getItemCartNumber(schedpos))+
      QString::asprintf("`TIME_TYPE`=%d,",state->time_type)+
      QString::asprintf("`TRANS_TYPE`=%d,",state->trans_type)+
      "`EXT_START_TIME`="+RDCheckDateTime(state->start_time,"hh:mm:ss")+","+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    q=new RDSqlQuery(sql);
    delete q;

    state->count++;

    //
    // Add to cache stack and also persist to database immediately
    // (This is the non-cached code path so we need immediate inserts)
    //
    cache->addToStack(schedCL->getItemCartNumber(schedpos),
                      schedCL->getItemArtist(schedpos),
                      schedCL->getItemTitle(schedpos),
                      schedCL->getItemSchedCodes(schedpos));
    
    // Also do immediate database insert for non-cached path
    sql = QString("INSERT INTO `STACK_LINES` SET ") +
      "`SERVICE_NAME`='" + RDEscapeString(svcname) + "'," +
      "`SCHEDULED_AT`=NOW()," +
      QString::asprintf("`SCHED_STACK_ID`=%u,", cache->getCurrentStackId()) +
      QString::asprintf("`CART`=%u,", schedCL->getItemCartNumber(schedpos)) +
      "`ARTIST`='" + RDEscapeString(schedCL->getItemArtist(schedpos)) + "'," +
      "`TITLE`='" + RDEscapeString(schedCL->getItemTitle(schedpos)) + "'";
    unsigned line_id = RDSqlQuery::run(sql).toUInt();
    
    // Insert sched codes
    QStringList codes = schedCL->getItemSchedCodes(schedpos);
    for (int sc = 0; sc < codes.size(); sc++) {
      sql = QString("INSERT INTO `STACK_SCHED_CODES` SET ") +
        QString::asprintf("`STACK_LINES_ID`=%u,", line_id) +
        "`SCHED_CODE`='" + RDEscapeString(codes.at(sc)) + "'";
      RDSqlQuery::apply(sql);
    }
    
    delete schedCL;
  }
  else {
    // We don't have any carts to work with
    *report+=rda->timeString(state->start_time)+
      " "+QObject::tr("No carts found in group")+" "+schedGroup();
    if(!HaveCode().isEmpty()) {
      *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
    }
    *report+="\n";
    
    delete schedCL;
  }
}


//
// Fully cached version of GenerateMusicSchedEvent - uses singleton RDLogGenerationCache
// NO DATABASE QUERIES - all data from cache, log lines buffered
//
void RDEventLine::GenerateMusicSchedEventCached(__RDEventLine_GeneratorState *state,
					  const QString &logname,
					  const QString &svcname,
					  const QString &clockname,
					  QString *report)
{
  int artistsep;
  int titlesep;
  int stackid;
  int counter;   		
  RDLogLine::Source source=RDLogLine::Music;
    
  state->start_time=state->start_time.addMSecs(state->length);

  if(event_artist_sep>=-1 && event_artist_sep<=50000) {
    artistsep = event_artist_sep;
  }
  else {
    artistsep = 15;
  }

  if(event_title_sep>=-1 && event_title_sep<=50000) {
    titlesep = event_title_sep;
  }
  else {
    titlesep = 100;
  }

  //
  // Get cache instance and next stack id
  //
  RDLogGenerationCache *cache = RDLogGenerationCache::instance();
  stackid = cache->getNextStackId();
  
  //
  // Load carts from cache (should already be loaded during init)
  //
  QString group = schedGroup();
  if (!cache->hasGroup(group)) {
    cache->loadCartsForGroup(group);
  }
  QList<RDSchedCart> cached_carts = cache->getCartsForGroup(group);
  
  //
  // Copy carts into schedCL for filtering
  //
  RDSchedCartList *schedCL = new RDSchedCartList();
  for (int i = 0; i < cached_carts.size(); i++) {
    schedCL->insertItem(cached_carts[i].cart_number, 0, 0,
                        cached_carts[i].artist, cached_carts[i].title,
                        cached_carts[i].sched_codes);
  }

  // Reduce schedCL to match requested scheduler code
  if(event_have_code!=""||event_have_code2!="") {
    QStringList codes;
    if(event_have_code!="") {
      codes << event_have_code;
    }
    if(event_have_code2!="") {
      codes << event_have_code2;
    }
    for(counter=0;counter<schedCL->getNumberOfItems();counter++) { 
      if(!schedCL->itemHasCodes(counter,codes)) {
	schedCL->removeItem(counter);
	counter--;
      }
    }
  }

  if(schedCL->getNumberOfItems()) {
    //
    // Title separation - from cache
    //
    if(titlesep>=0) {
      schedCL->save();
      QStringList recent_titles = cache->getTitlesInRange(stackid - titlesep);
      for (int t = 0; t < recent_titles.size(); t++) {
        for(counter=0; counter<schedCL->getNumberOfItems(); counter++) { 
          if(recent_titles[t] == schedCL->getItemTitle(counter)) {
            schedCL->removeItem(counter);
            counter--;
          }
        }
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Title separation");
	if(!HaveCode().isEmpty()) {
	  *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
	}
	*report+="\n";
	schedCL->restore();
      }
    }
      
    //
    // Artist separation - from cache
    //
    if(artistsep>=0) {
      schedCL->save();
      QStringList recent_artists = cache->getArtistsInRange(stackid - artistsep);
      for (int a = 0; a < recent_artists.size(); a++) {
        for(counter=0; counter<schedCL->getNumberOfItems(); counter++) { 
          if(recent_artists[a] == schedCL->getItemArtist(counter)) {
            schedCL->removeItem(counter);
            counter--;
          }
        }
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Artist separation");
	if(!HaveCode().isEmpty()) {
	  *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
	}
	*report+="\n";
	schedCL->restore();
      }
    }
      
    //
    // Clock Scheduler Rules - from cache
    //
    if (!cache->hasClockRules(clockname)) {
      cache->loadRulesForClock(clockname);
    }
    QList<RDSchedulerRule> rules = cache->getRulesForClock(clockname);
    
    for (int r = 0; r < rules.size(); r++) {
      const RDSchedulerRule &rule = rules[r];
      
      // max in a row, min wait - skip if MAX_ROW was NULL (no restriction)
      if (!rule.max_row_valid) {
        continue;  // NULL MAX_ROW means no restriction for this code
      }
      schedCL->save();	
      int range = rule.max_row + rule.min_wait; 
      int allowed = rule.max_row;
      
      QList<unsigned> carts_with_code = cache->getCartsWithCodeInRange(rule.code, stackid - range);
      if(carts_with_code.size() >= allowed || allowed == 0) {
	for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	  if(schedCL->removeIfCode(counter, rule.code)) {
	    counter--;
	  }
	}
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Max. in a Row/Min. Wait for ")+
	  rule.code+"\n";
	schedCL->restore();
      }

      // do not play after
      if(!rule.not_after.isEmpty()) {
	schedCL->save();	
	if(cache->previousItemHasCode(rule.not_after)) {
	  for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	    if(schedCL->removeIfCode(counter, rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule ")+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.not_after+"\n";
	  schedCL->restore();
	}
      }
      
      // or after
      if (!rule.or_after.isEmpty()) {
	schedCL->save();
	if(cache->previousItemHasCode(rule.or_after)) {
	  for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	    if(schedCL->removeIfCode(counter, rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule")+" "+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.or_after+"\n";
	  schedCL->restore();
	}
      }
      
      // or after II
      if (!rule.or_after_ii.isEmpty()) {
	schedCL->save();
	if(cache->previousItemHasCode(rule.or_after_ii)) {
	  for(counter=0; counter<schedCL->getNumberOfItems(); counter++) {
	    if(schedCL->removeIfCode(counter, rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule")+" "+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.or_after_ii+"\n";
	  schedCL->restore();
	}
      }
    }      
    //
    // Pick a random cart from those that are remaining - buffer to cache
    //
    int schedpos=rand()%schedCL->getNumberOfItems();
    
    int count = cache->getNextCount();  // Get count when actually adding a line
    RDPendingLogLine line;
    line.line_id = count;
    line.count = count;
    line.type = RDLogLine::Cart;
    line.source = source;
    line.start_time = QTime(0,0,0).msecsTo(state->start_time);
    line.grace_time = state->grace_time;
    line.cart_number = schedCL->getItemCartNumber(schedpos);
    line.time_type = state->time_type;
    line.trans_type = state->trans_type;
    line.comment = QString();
    line.event_length = event_length;
    line.link_event_name = QString();
    line.link_start_time = 0;
    line.link_length = 0;
    line.link_id = -1;
    line.link_start_slop = 0;
    line.link_end_slop = 0;
    
    cache->addLogLine(line);

    //
    // Add to cache stack (buffered - will be persisted at flush time)
    //
    cache->addToStack(schedCL->getItemCartNumber(schedpos),
                      schedCL->getItemArtist(schedpos),
                      schedCL->getItemTitle(schedpos),
                      schedCL->getItemSchedCodes(schedpos));
    
    delete schedCL;
  }
  else {
    // We don't have any carts to work with
    *report+=rda->timeString(state->start_time)+
      " "+QObject::tr("No carts found in group")+" "+schedGroup();
    if(!HaveCode().isEmpty()) {
      *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
    }
    *report+="\n";
    
    delete schedCL;
  }
}


int RDEventLine::GetLength(unsigned cartnum,int def_length)
{
  // Use the singleton cache - it does lazy loading and caching
  RDLogGenerationCache *cache = RDLogGenerationCache::instance();
  return cache->getCartLength(cartnum, def_length);
}


int RDEventLine::GetLength(unsigned cartnum,RDCartCache *cart_cache,
			   int def_length)
{
  if(cart_cache==NULL) {
    return GetLength(cartnum,def_length);
  }
  int length=cart_cache->getCartLength(cartnum);
  if(length<0) {
    return def_length;
  }
  return length;
}


bool RDEventLine::generateLog(const QString &logname,const QString &svcname,
			      const QString &clockname,QString *report,
			      RDEventLineCache *event_cache,
			      RDCartCache *cart_cache,
			      RDClockCache *clock_cache)
{
  // If no caches provided, fall back to original implementation
  if(event_cache==NULL || cart_cache==NULL || clock_cache==NULL) {
    return generateLog(logname,svcname,clockname,report);
  }

  QString sql;
  RDSqlQuery *q;
  QTime fill_start_time;
  QString import_table;
  __RDEventLine_GeneratorState *state=new __RDEventLine_GeneratorState();
  state->start_time=event_start_time;
  state->length=0;
  state->count=0;
  state->trans_type=event_first_transtype;
  state->time_type=event_time_type;
  state->link_type=RDLogLine::MusicLink;
  state->grace_time=event_grace_time;
  state->link_id=0;

  //
  // Get Current Count and Link ID
  //
  sql=QString("select `COUNT` from `LOG_LINES` where ")+
    "`LOG_NAME`='"+RDEscapeString(logname)+"' "+
    "order by `COUNT` desc";
  q=new RDSqlQuery(sql);
  if(q->first()) {
    state->count=q->value(0).toInt()+1;
  }
  delete q;

  sql=QString("select `LINK_ID` from `LOG_LINES` where ")+
    "`LOG_NAME`='"+RDEscapeString(logname)+"' && "+
    "`LINK_ID`>=0 "+
    "order by `LINK_ID` desc";
  q=new RDSqlQuery(sql);
  if(q->first()) {
    state->link_id=q->value(0).toInt()+1;
  }
  delete q;

  //
  // Override Default Parameters if Preposition Set
  //
  if(event_preposition>=0) {
    state->time_type=RDLogLine::Hard;
    state->grace_time=-1;
    if(QTime(0,0,0).msecsTo(state->start_time)>event_preposition) {
      state->start_time=state->start_time.addMSecs(-event_preposition);
    }
    else {
      state->start_time=QTime();
    }
  }

  //
  // Pre-Import Carts (using cached GetLength)
  //
  state->length=0;
  for(int i=0;i<event_preimport_list->size()-1;i++) {
    RDEventImportItem *i_item=event_preimport_list->item(i);
    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`='"+RDEscapeString(logname)+"',"+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",i_item->eventType())+
      QString::asprintf("`SOURCE`=%d,",RDLogLine::Template)+
      QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(state->start_time))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`CART_NUMBER`=%u,",i_item->cartNumber())+
      QString::asprintf("`TIME_TYPE`=%d,",state->time_type)+
      QString::asprintf("`TRANS_TYPE`=%d,",i_item->transType())+
      "`COMMENT`='"+RDEscapeString(i_item->markerComment())+"',"+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    RDSqlQuery::apply(sql);
    state->count++;
    state->trans_type=event_default_transtype;
    state->time_type=RDLogLine::Relative;
    state->grace_time=-1;

    // Use cached GetLength
    state->length+=GetLength(i_item->cartNumber(),cart_cache);
  }

  //
  // Import Links
  //
  if(event_import_source==RDEventLine::Traffic || event_import_source==RDEventLine::Music) {
    switch(event_import_source) {
    case RDEventLine::Traffic:
      state->link_type=RDLogLine::TrafficLink;
      break;
	  
    case RDEventLine::Music:
      state->link_type=RDLogLine::MusicLink;
      break;
	  
    default:
      break;
    }
    QTime end_start_time=event_start_time.addMSecs(event_length);

    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`='"+RDEscapeString(logname)+"',"+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",state->link_type)+
      QString::asprintf("`SOURCE`=%d,",RDLogLine::Template)+
      QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(state->start_time))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`TIME_TYPE`=%d,",state->time_type)+
      QString::asprintf("`TRANS_TYPE`=%d,",state->trans_type)+
      "`LINK_EVENT_NAME`='"+RDEscapeString(event_name)+"',"+
      QString::asprintf("`LINK_START_TIME`=%d,",
			QTime(0,0,0).msecsTo(event_start_time))+
      QString::asprintf("`LINK_LENGTH`=%d,",
			event_start_time.msecsTo(end_start_time))+
      QString::asprintf("`LINK_ID`=%d,",state->link_id)+
      QString::asprintf("`LINK_START_SLOP`=%d,",event_start_slop)+
      QString::asprintf("`LINK_END_SLOP`=%d,",event_end_slop)+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    q=new RDSqlQuery(sql);
    delete q;
    state->count++;
    state->start_time=state->start_time.addMSecs(event_length);
    state->trans_type=event_default_transtype;
    state->time_type=RDLogLine::Relative;
    state->grace_time=-1;
  }

  //
  // Music Scheduler (using cached version)
  //
  if(event_import_source == RDEventLine::Scheduler) {
    GenerateMusicSchedEvent(state,logname,svcname,clockname,report,
			    event_cache,cart_cache,clock_cache);
  }

  //
  // Post-Import Carts (using cached GetLength)
  //
  for(int i=0;i<event_postimport_list->size()-1;i++) {
    RDEventImportItem *i_item=event_postimport_list->item(i);
    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`='"+RDEscapeString(logname)+"',"+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",i_item->eventType())+
      QString::asprintf("`SOURCE`=%d,",RDLogLine::Template)+
      QString::asprintf("`START_TIME`=%d,",
			QTime(0,0,0).msecsTo(state->start_time.addMSecs(state->length)))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`CART_NUMBER`=%u,",i_item->cartNumber())+
      QString::asprintf("`TIME_TYPE`=%d,",RDLogLine::Relative)+
      QString::asprintf("`TRANS_TYPE`=%d,",i_item->transType())+
      "`COMMENT`='"+RDEscapeString(i_item->markerComment())+"',"+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    RDSqlQuery::apply(sql);
    state->count++;
    // Use cached GetLength
    state->length+=GetLength(i_item->cartNumber(),cart_cache);
  }

  //
  // Autofill
  //
  if(event_use_autofill) {
    fill_start_time=state->start_time.addMSecs(state->length);
    QTime end_time=event_start_time.addMSecs(event_length);
    int remaining=fill_start_time.msecsTo(end_time);
    int real_remaining=remaining;
    if(remaining>0) {
      // Get autofill carts from cache
      QVector<RDCartCache::AutofillCart> autofill_carts=
	cart_cache->getAutofillCarts(event_autofill_slop);
      
      for(int i=0;i<autofill_carts.size() && real_remaining>0;i++) {
	const RDCartCache::AutofillCart &cart=autofill_carts.at(i);
	if(cart.forced_length<=remaining) {
	  sql=QString("insert into `LOG_LINES` set ")+
	    "`LOG_NAME`='"+RDEscapeString(logname)+"',"+
	    QString::asprintf("`LINE_ID`=%d,",state->count)+
	    QString::asprintf("`COUNT`=%d,",state->count)+
	    QString::asprintf("`TYPE`=%d,",RDLogLine::Cart)+
	    QString::asprintf("`SOURCE`=%d,",RDLogLine::Template)+
	    QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(fill_start_time))+
	    QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
	    QString::asprintf("`CART_NUMBER`=%u,",cart.cart_number)+
	    QString::asprintf("`TIME_TYPE`=%d,",RDLogLine::Relative)+
	    QString::asprintf("`TRANS_TYPE`=%d,",event_default_transtype)+
	    QString::asprintf("`EVENT_LENGTH`=%d",event_length);
	  RDSqlQuery::apply(sql);
	  state->count++;
	  remaining-=cart.forced_length;
	  fill_start_time=fill_start_time.addMSecs(cart.forced_length);
	}
	real_remaining=remaining;
      }
    }
  }

  delete state;
  return true;
}


void RDEventLine::GenerateMusicSchedEvent(__RDEventLine_GeneratorState *state,
					  const QString &logname,
					  const QString &svcname,
					  const QString &clockname,
					  QString *report,
					  RDEventLineCache *event_cache,
					  RDCartCache *cart_cache,
					  RDClockCache *clock_cache)
{
  // If no caches provided, fall back to original implementation
  if(event_cache==NULL || cart_cache==NULL || clock_cache==NULL) {
    GenerateMusicSchedEvent(state,logname,svcname,clockname,report);
    return;
  }

  QString sql;
  RDSqlQuery *q=NULL;
  int artistsep;
  int titlesep;
  int stackid;
  int counter;   		
  RDLogLine::Source source=RDLogLine::Music;
    
  state->start_time=state->start_time.addMSecs(state->length);

  if(event_artist_sep>=-1 && event_artist_sep<=50000) {
    artistsep = event_artist_sep;
  }
  else {
    artistsep = 15;
  }

  if(event_title_sep>=-1 && event_title_sep<=50000) {
    titlesep = event_title_sep;
  }
  else {
    titlesep = 100;
  }

  //
  // Get next stack id from the cache
  //
  stackid=event_cache->currentStackId()+1;
      
  //
  // Load all carts in requested group from cache (THE BIG WIN!)
  //
  QVector<const RDCartCache::CartData*> cart_list=
    cart_cache->getCartsInGroup(schedGroup());
  
  RDSchedCartList *schedCL=new RDSchedCartList();
  for(int i=0;i<cart_list.size();i++) {
    const RDCartCache::CartData *cart=cart_list.at(i);
    schedCL->insertItem(cart->number,0,0,cart->artist,cart->title,cart->sched_codes);
  }

  //////////////////////////////////
  //                              //
  // Add deconflicting rules here //
  //                              //
  //////////////////////////////////

  // Reduce schedCL to match requested scheduler code
  if(event_have_code!=""||event_have_code2!="") {
    QStringList codes;
    if(event_have_code!="") {
      codes << event_have_code;
    }
    if(event_have_code2!="") {
      codes << event_have_code2;
    }
    for(counter=0;counter<schedCL->getNumberOfItems();counter++) { 
      if(!schedCL->itemHasCodes(counter,codes)) {
	schedCL->removeItem(counter);
	counter--;
      }
    }
  }

  if(schedCL->getNumberOfItems()) {
    //
    // Title separation - use cached stack data
    //
    if(titlesep>=0) {
      schedCL->save();
      for(counter=0;counter<schedCL->getNumberOfItems();counter++) {
	if(event_cache->isTitleInStack(schedCL->getItemTitle(counter),titlesep)) {
	  schedCL->removeItem(counter);
	  counter--;
	}
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Title separation");
	if(!HaveCode().isEmpty()) {
	  *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
	}
	*report+="\n";
	schedCL->restore();
      }
    }
      
    //
    // Artist separation - use cached stack data
    //
    if(artistsep>=0) {
      schedCL->save();
      for(counter=0;counter<schedCL->getNumberOfItems();counter++) {
	if(event_cache->isArtistInStack(schedCL->getItemArtist(counter),artistsep)) {
	  schedCL->removeItem(counter);
	  counter--;
	}
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Artist separation");
	if(!HaveCode().isEmpty()) {
	  *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
	}
	*report+="\n";
	schedCL->restore();
      }
    }
      
    // Clock Scheduler Rules - use cached rules
    QVector<RDClockCache::ClockRule> rules=clock_cache->getRulesForClock(clockname);
    for(int r=0;r<rules.size();r++) {
      const RDClockCache::ClockRule &rule=rules.at(r);
      
      // max in a row, min wait - skip if MAX_ROW was NULL (no restriction)
      if (!rule.max_row_valid) {
        continue;  // NULL MAX_ROW means no restriction for this code
      }
      schedCL->save();	
      int range=rule.max_row+rule.min_wait; 
      int allowed=rule.max_row;
      
      // Check how many times this code appears in range using cache
      int code_count=event_cache->countSchedCodeInRange(rule.code,
							stackid-range,stackid);
      if(code_count>=allowed || allowed==0) {
	for(counter=0;counter<schedCL->getNumberOfItems();counter++) {
	  if(schedCL->removeIfCode(counter,rule.code)) {
	    counter--;
	  }
	}
      }
      if(schedCL->getNumberOfItems()==0) {
	*report+=rda->timeString(state->start_time)+" "+
	  QObject::tr("Rule broken: Max. in a Row/Min. Wait for ")+
	  rule.code+"\n";
	schedCL->restore();
      }

      // do not play after
      if(!rule.not_after.isEmpty()) {
	schedCL->save();
	if(event_cache->hasSchedCodeAtPosition(rule.not_after,stackid-1)) {
	  for(counter=0;counter<schedCL->getNumberOfItems();counter++) {
	    if(schedCL->removeIfCode(counter,rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule ")+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.not_after+"\n";
	  schedCL->restore();
	}
      }
      // or after
      if (!rule.or_after.isEmpty()) {
	schedCL->save();
	if(event_cache->hasSchedCodeAtPosition(rule.or_after,stackid-1)) {
	  for(counter=0;counter<schedCL->getNumberOfItems();counter++) {
	    if(schedCL->removeIfCode(counter,rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule")+" "+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.or_after+"\n";
	  schedCL->restore();
	}
      }
      // or after II
      if (!rule.or_after_ii.isEmpty()) {
	schedCL->save();
	if(event_cache->hasSchedCodeAtPosition(rule.or_after_ii,stackid-1)) {
	  for(counter=0;counter<schedCL->getNumberOfItems();counter++) {
	    if(schedCL->removeIfCode(counter,rule.code)) {
	      counter--;
	    }
	  }
	}
	if(schedCL->getNumberOfItems()==0) {
	  *report+=rda->timeString(state->start_time)+" "+
	    QObject::tr("Rule broken: Do not schedule")+" "+
	    rule.code+" "+QObject::tr("after")+" "+
	    rule.or_after_ii+"\n";
	  schedCL->restore();
	}
      }
    }

    ////////////////////////////////
    //                            //
    // End of deconflicting rules //
    //                            //
    ////////////////////////////////
      
    //
    // Pick a random cart from those that are remaining.
    //
    int schedpos=rand()%schedCL->getNumberOfItems();
    sql=QString("insert into `LOG_LINES` set ")+
      "`LOG_NAME`=\""+RDEscapeString(logname)+"\","+
      QString::asprintf("`LINE_ID`=%d,",state->count)+
      QString::asprintf("`COUNT`=%d,",state->count)+
      QString::asprintf("`TYPE`=%d,",RDLogLine::Cart)+
      QString::asprintf("`SOURCE`=%d,",source)+
      QString::asprintf("`START_TIME`=%d,",QTime(0,0,0).msecsTo(state->start_time))+
      QString::asprintf("`GRACE_TIME`=%d,",state->grace_time)+
      QString::asprintf("`CART_NUMBER`=%u,",schedCL->getItemCartNumber(schedpos))+
      QString::asprintf("`TIME_TYPE`=%d,",state->time_type)+
      QString::asprintf("`TRANS_TYPE`=%d,",state->trans_type)+
      "`EXT_START_TIME`="+RDCheckDateTime(state->start_time,"hh:mm:ss")+","+
      QString::asprintf("`EVENT_LENGTH`=%d",event_length);
    q=new RDSqlQuery(sql);
    delete q;

    state->count++;

    sql=QString("insert into `STACK_LINES` set ")+
      "`SERVICE_NAME`='"+RDEscapeString(svcname)+"',"+
      "`SCHEDULED_AT`=now(),"+
      QString::asprintf("`SCHED_STACK_ID`=%u,",stackid)+
      QString::asprintf("`CART`=%u,",schedCL->getItemCartNumber(schedpos))+
      "`ARTIST`='"+RDEscapeString(schedCL->getItemArtist(schedpos))+"',"+
      "`TITLE`='"+RDEscapeString(schedCL->getItemTitle(schedpos))+"'";
    unsigned line_id=RDSqlQuery::run(sql).toUInt();
    QStringList codes=schedCL->getItemSchedCodes(schedpos);
    for(int i=0;i<codes.size();i++) {
      sql=QString("insert into `STACK_SCHED_CODES` set ")+
	QString::asprintf("`STACK_LINES_ID`=%u,",line_id)+
	"`SCHED_CODE`='"+RDEscapeString(codes.at(i))+"'";
      RDSqlQuery::apply(sql);
    }
    
    // Update the cache with the new stack entry
    event_cache->pushStackEntry(schedCL->getItemCartNumber(schedpos),
				schedCL->getItemArtist(schedpos),
				schedCL->getItemTitle(schedpos),
				codes);
    
    delete schedCL;
  }
  else {
    // We don't have any carts to work with
    *report+=rda->timeString(state->start_time)+
      " "+QObject::tr("No carts found in group")+" "+schedGroup();
    if(!HaveCode().isEmpty()) {
      *report+=QObject::tr(" with sched code(s): ")+HaveCode()+" "+HaveCode2();
    }
    *report+="\n";
    
    delete schedCL;
  }
}
