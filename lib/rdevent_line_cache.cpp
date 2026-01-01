// rdevent_line_cache.cpp
//
// Event-level cache for log generation
//
// Caches event definitions (EVENTS + EVENT_LINES pre/post imports) and
// stack history for deconflicting. Eliminates repeated event loads when
// the same event is used multiple times in a day.
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

#include <QSet>

#include "rdcart_cache.h"
#include "rdconf.h"
#include "rddb.h"
#include "rdescape_string.h"
#include "rdevent_line_cache.h"

void RDEventLineCache::EventData::clear()
{
  name="";
  preposition=0;
  time_type=0;
  grace_time=0;
  use_autofill=false;
  use_timescale=false;
  import_source=0;
  start_slop=0;
  end_slop=0;
  first_trans_type=0;
  default_trans_type=0;
  color=QColor();
  autofill_slop=-1;
  nested_event="";
  sched_group="";
  artist_sep=15;
  title_sep=100;
  have_code="";
  have_code2="";
  preimport_items.clear();
  postimport_items.clear();
}


RDEventLineCache::RDEventLineCache()
{
  d_current_stack_id=0;
  clear();
}


RDEventLineCache::~RDEventLineCache()
{
  clear();
}


void RDEventLineCache::clear()
{
  d_events.clear();
  d_stack.clear();
  d_current_stack_id=0;
  d_svc_name="";
}


bool RDEventLineCache::loadEventsForService(const QString &svc_name)
{
  d_svc_name=svc_name;
  
  // First, get all events used by clocks assigned to this service
  // Step 1: Get all clocks used by the service
  QString sql=QString("select distinct `CLOCK_NAME` from `SERVICE_CLOCKS` where ")+
    "`SERVICE_NAME`='"+RDEscapeString(svc_name)+"' && "+
    "`CLOCK_NAME` is not null && `CLOCK_NAME`!=''";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  QStringList clock_names;
  while(q->next()) {
    clock_names.push_back(q->value(0).toString());
  }
  delete q;
  
  if(clock_names.isEmpty()) {
    return true;  // No clocks, no events to load
  }
  
  // Step 2: Get all events used by those clocks
  QString in_clause;
  for(int i=0; i<clock_names.size(); i++) {
    if(i>0) {
      in_clause+=",";
    }
    in_clause+="'"+RDEscapeString(clock_names[i])+"'";
  }
  
  sql=QString("select distinct `EVENT_NAME` from `CLOCK_LINES` where ")+
    "`CLOCK_NAME` in ("+in_clause+") && "+
    "`EVENT_NAME` is not null && `EVENT_NAME`!=''";
  
  q=new RDSqlQuery(sql);
  QStringList event_names;
  while(q->next()) {
    event_names.push_back(q->value(0).toString());
  }
  delete q;
  
  if(event_names.isEmpty()) {
    return true;
  }
  
  return loadEvents(event_names);
}


bool RDEventLineCache::loadEvents(const QStringList &event_names)
{
  if(event_names.isEmpty()) {
    return true;
  }
  
  // Build IN clause
  QString in_clause;
  for(int i=0; i<event_names.size(); i++) {
    if(i>0) {
      in_clause+=",";
    }
    in_clause+="'"+RDEscapeString(event_names[i])+"'";
  }
  
  // Batch load all events
  QString sql=QString("select ")+
    "`NAME`,"+                // 00
    "`PREPOSITION`,"+         // 01
    "`TIME_TYPE`,"+           // 02
    "`GRACE_TIME`,"+          // 03
    "`USE_AUTOFILL`,"+        // 04
    "`USE_TIMESCALE`,"+       // 05
    "`IMPORT_SOURCE`,"+       // 06
    "`START_SLOP`,"+          // 07
    "`END_SLOP`,"+            // 08
    "`FIRST_TRANS_TYPE`,"+    // 09
    "`DEFAULT_TRANS_TYPE`,"+  // 10
    "`COLOR`,"+               // 11
    "`AUTOFILL_SLOP`,"+       // 12
    "`NESTED_EVENT`,"+        // 13
    "`SCHED_GROUP`,"+         // 14
    "`ARTIST_SEP`,"+          // 15
    "`TITLE_SEP`,"+           // 16
    "`HAVE_CODE`,"+           // 17
    "`HAVE_CODE2` "+          // 18
    "from `EVENTS` where "+
    "`NAME` in ("+in_clause+")";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    EventData event;
    event.name=q->value(0).toString();
    event.preposition=q->value(1).toInt();
    event.time_type=q->value(2).toInt();
    event.grace_time=q->value(3).toInt();
    event.use_autofill=RDBool(q->value(4).toString());
    event.use_timescale=RDBool(q->value(5).toString());
    event.import_source=q->value(6).toInt();
    event.start_slop=q->value(7).toInt();
    event.end_slop=q->value(8).toInt();
    event.first_trans_type=q->value(9).toInt();
    event.default_trans_type=q->value(10).toInt();
    if(q->value(11).isNull()) {
      event.color=QColor();
    }
    else {
      event.color=QColor(q->value(11).toString());
    }
    event.autofill_slop=q->value(12).toInt();
    event.nested_event=q->value(13).toString();
    event.sched_group=q->value(14).toString();
    event.artist_sep=q->value(15).toInt();
    event.title_sep=q->value(16).toInt();
    event.have_code=q->value(17).toString();
    event.have_code2=q->value(18).toString();
    
    d_events[event.name]=event;
  }
  delete q;
  
  // Load import lists for all events
  loadEventImportLists();
  
  return true;
}


bool RDEventLineCache::loadEventImportLists()
{
  if(d_events.isEmpty()) {
    return true;
  }
  
  // Build IN clause for event names
  QString in_clause;
  bool first=true;
  for(auto it=d_events.constBegin(); it!=d_events.constEnd(); ++it) {
    if(!first) {
      in_clause+=",";
    }
    in_clause+="'"+RDEscapeString(it.key())+"'";
    first=false;
  }
  
  // Load preimport lists (TYPE=0)
  QString sql=QString("select ")+
    "`EVENT_NAME`,"+    // 00
    "`TYPE`,"+          // 01
    "`CART_NUMBER`,"+   // 02
    "`TRANS_TYPE`,"+    // 03
    "`MARKER_COMMENT` "+// 04
    "from `EVENT_LINES` where "+
    "`EVENT_NAME` in ("+in_clause+") "+
    "order by `EVENT_NAME`,`TYPE`,`COUNT`";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    QString event_name=q->value(0).toString();
    int list_type=q->value(1).toInt();  // 0=preimport, 1=postimport
    
    if(d_events.contains(event_name)) {
      EventImportItem item;
      item.event_type=list_type;
      item.cart_number=q->value(2).toUInt();
      item.trans_type=q->value(3).toInt();
      item.marker_comment=q->value(4).toString();
      
      if(list_type==0) {
        d_events[event_name].preimport_items.push_back(item);
      }
      else {
        d_events[event_name].postimport_items.push_back(item);
      }
    }
  }
  delete q;
  
  return true;
}


bool RDEventLineCache::hasEvent(const QString &event_name) const
{
  return d_events.contains(event_name);
}


const RDEventLineCache::EventData* RDEventLineCache::getEvent(const QString &event_name) const
{
  auto it=d_events.constFind(event_name);
  if(it!=d_events.constEnd()) {
    return &it.value();
  }
  return nullptr;
}


bool RDEventLineCache::loadStack(const QString &svc_name, int max_depth)
{
  d_stack.clear();
  d_svc_name=svc_name;
  
  // Get the current max stack ID
  QString sql=QString("select MAX(`SCHED_STACK_ID`) from `STACK_LINES` where ")+
    "`SERVICE_NAME`='"+RDEscapeString(svc_name)+"'";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(q->first() && !q->value(0).isNull()) {
    d_current_stack_id=q->value(0).toInt();
  }
  else {
    d_current_stack_id=0;
  }
  delete q;
  
  // Load recent stack entries with their sched codes
  sql=QString("select ")+
    "`STACK_LINES`.`ID`,"+           // 00
    "`STACK_LINES`.`SCHED_STACK_ID`,"+  // 01
    "`STACK_LINES`.`CART`,"+         // 02
    "`STACK_LINES`.`ARTIST`,"+       // 03
    "`STACK_LINES`.`TITLE`,"+        // 04
    "GROUP_CONCAT(`STACK_SCHED_CODES`.`SCHED_CODE`) "+  // 05
    "from `STACK_LINES` "+
    "left join `STACK_SCHED_CODES` on `STACK_LINES`.`ID`=`STACK_SCHED_CODES`.`STACK_LINES_ID` "+
    "where `STACK_LINES`.`SERVICE_NAME`='"+RDEscapeString(svc_name)+"' "+
    "group by `STACK_LINES`.`ID` "+
    "order by `STACK_LINES`.`SCHED_STACK_ID` desc "+
    QString::asprintf("limit %d", max_depth);
  
  q=new RDSqlQuery(sql);
  while(q->next()) {
    StackEntry entry;
    entry.stack_id=q->value(1).toInt();
    entry.cart_number=q->value(2).toUInt();
    entry.artist=q->value(3).toString();
    entry.title=q->value(4).toString();
    
    // Parse sched codes from GROUP_CONCAT result
    QString codes_str=q->value(5).toString();
    if(!codes_str.isEmpty()) {
      entry.sched_codes=codes_str.split(",", Qt::SkipEmptyParts);
    }
    
    d_stack.push_back(entry);
  }
  delete q;
  
  return true;
}


bool RDEventLineCache::isTitleInStack(const QString &title, int depth) const
{
  int check_depth=qMin(depth, d_stack.size());
  for(int i=0; i<check_depth; i++) {
    if(d_stack[i].title==title) {
      return true;
    }
  }
  return false;
}


bool RDEventLineCache::isArtistInStack(const QString &artist, int depth) const
{
  int check_depth=qMin(depth, d_stack.size());
  for(int i=0; i<check_depth; i++) {
    if(d_stack[i].artist==artist) {
      return true;
    }
  }
  return false;
}


bool RDEventLineCache::hasSchedCodeAtPosition(const QString &code, int stack_pos) const
{
  if(stack_pos<0 || stack_pos>=d_stack.size()) {
    return false;
  }
  return d_stack[stack_pos].sched_codes.contains(code);
}


int RDEventLineCache::countSchedCodeInRange(const QString &code, int from_pos, int to_pos) const
{
  int count=0;
  int start=qMax(0, from_pos);
  int end=qMin(to_pos, d_stack.size()-1);
  
  for(int i=start; i<=end; i++) {
    if(d_stack[i].sched_codes.contains(code)) {
      count++;
    }
  }
  return count;
}


void RDEventLineCache::pushStackEntry(unsigned cart_number, RDCartCache *cart_cache)
{
  QString artist;
  QString title;
  QStringList codes;
  
  if(cart_cache) {
    const RDCartCache::CartData *cart=cart_cache->getCart(cart_number);
    if(cart) {
      artist=cart->artist;
      title=cart->title;
      codes=cart->sched_codes;
    }
  }
  
  pushStackEntry(cart_number, artist, title, codes);
}


void RDEventLineCache::pushStackEntry(unsigned cart_number, const QString &artist,
                                      const QString &title, const QStringList &sched_codes)
{
  // Increment stack ID
  d_current_stack_id++;
  
  // Create new entry
  StackEntry entry;
  entry.stack_id=d_current_stack_id;
  entry.cart_number=cart_number;
  entry.artist=artist;
  entry.title=title;
  entry.sched_codes=sched_codes;
  
  // Insert at front (most recent first)
  d_stack.prepend(entry);
}
