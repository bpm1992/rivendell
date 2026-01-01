// rdclock_cache.cpp
//
// Clock-level cache for log generation
//
// Caches clock definitions (CLOCKS + CLOCK_LINES), service clock grid,
// and clock scheduler rules. Eliminates repeated clock loads when the
// same clock is used for multiple hours.
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

#include "rdclock_cache.h"
#include "rdconf.h"
#include "rddb.h"
#include "rdescape_string.h"

RDClockCache::RDClockCache()
{
  clear();
}


RDClockCache::~RDClockCache()
{
  clear();
}


void RDClockCache::clear()
{
  d_clocks.clear();
  d_clock_grid.clear();
  d_clock_grid.resize(168);  // 7 days × 24 hours
  d_clock_rules.clear();
}


bool RDClockCache::loadClocksForService(const QString &svc_name)
{
  clear();
  
  // Step 1: Load the clock grid (168 hours) and collect unique clock names
  if(!loadClockGrid(svc_name)) {
    return false;
  }
  
  // Step 2: Get unique clock names from grid
  QSet<QString> clock_names_set;
  for(int i=0; i<d_clock_grid.size(); i++) {
    if(!d_clock_grid[i].isEmpty()) {
      clock_names_set.insert(d_clock_grid[i]);
    }
  }
  
  if(clock_names_set.isEmpty()) {
    return true;  // No clocks assigned, but that's OK
  }
  
  // Step 3: Build IN clause for batch loading clocks
  QString in_clause;
  bool first=true;
  for(const QString &name : clock_names_set) {
    if(!first) {
      in_clause+=",";
    }
    in_clause+="'"+RDEscapeString(name)+"'";
    first=false;
  }
  
  // Step 4: Batch load all clock definitions
  QString sql=QString("select ")+
    "`NAME`,"+        // 00
    "`SHORT_NAME`,"+  // 01
    "`COLOR`,"+       // 02
    "`ARTISTSEP`,"+   // 03
    "`REMARKS` "+     // 04
    "from `CLOCKS` where "+
    "`NAME` in ("+in_clause+")";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    ClockData clock;
    clock.name=q->value(0).toString();
    clock.short_name=q->value(1).toString();
    if(q->value(2).isNull()) {
      clock.color=QColor();
    }
    else {
      clock.color=QColor(q->value(2).toString());
    }
    clock.artist_sep=q->value(3).toUInt();
    clock.remarks=q->value(4).toString();
    d_clocks[clock.name]=clock;
  }
  delete q;
  
  // Step 5: Batch load all clock events (CLOCK_LINES)
  sql=QString("select ")+
    "`CLOCK_NAME`,"+  // 00
    "`EVENT_NAME`,"+  // 01
    "`START_TIME`,"+  // 02
    "`LENGTH` "+      // 03
    "from `CLOCK_LINES` where "+
    "`CLOCK_NAME` in ("+in_clause+") "+
    "order by `CLOCK_NAME`,`START_TIME`";
  
  q=new RDSqlQuery(sql);
  while(q->next()) {
    QString clock_name=q->value(0).toString();
    if(d_clocks.contains(clock_name)) {
      ClockEvent event;
      event.event_name=q->value(1).toString();
      event.start_time_ms=q->value(2).toInt();
      event.length_ms=q->value(3).toInt();
      d_clocks[clock_name].events.push_back(event);
    }
  }
  delete q;
  
  // Step 6: Load scheduler rules for all clocks
  loadClockRules();
  
  return true;
}


bool RDClockCache::loadClockGrid(const QString &svc_name)
{
  d_clock_grid.clear();
  d_clock_grid.resize(168);
  
  // SERVICE_CLOCKS table has HOUR field (0-167) representing week position
  // HOUR = (day_of_week - 1) * 24 + hour_of_day
  QString sql=QString("select ")+
    "`HOUR`,"+        // 00
    "`CLOCK_NAME` "+  // 01
    "from `SERVICE_CLOCKS` where "+
    "`SERVICE_NAME`='"+RDEscapeString(svc_name)+"'";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    int hour=q->value(0).toInt();
    if(hour>=0 && hour<168) {
      d_clock_grid[hour]=q->value(1).toString();
    }
  }
  delete q;
  
  return true;
}


bool RDClockCache::loadClockRules()
{
  d_clock_rules.clear();
  
  if(d_clocks.isEmpty()) {
    return true;
  }
  
  // Build IN clause for clock names
  QString in_clause;
  bool first=true;
  for(auto it=d_clocks.constBegin(); it!=d_clocks.constEnd(); ++it) {
    if(!first) {
      in_clause+=",";
    }
    in_clause+="'"+RDEscapeString(it.key())+"'";
    first=false;
  }
  
  // Batch load all rules for all clocks
  QString sql=QString("select ")+
    "`CLOCK_NAME`,"+  // 00
    "`CODE`,"+        // 01
    "`MAX_ROW`,"+     // 02
    "`MIN_WAIT`,"+    // 03
    "`NOT_AFTER`,"+   // 04
    "`OR_AFTER`,"+    // 05
    "`OR_AFTER_II` "+ // 06
    "from `RULE_LINES` where "+
    "`CLOCK_NAME` in ("+in_clause+") "+
    "order by `CLOCK_NAME`,`CODE`";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    QString clock_name=q->value(0).toString();
    ClockRule rule;
    rule.code=q->value(1).toString();
    rule.max_row=q->value(2).toInt();
    rule.min_wait=q->value(3).toInt();
    rule.not_after=q->value(4).toString();
    rule.or_after=q->value(5).toString();
    rule.or_after_ii=q->value(6).toString();
    d_clock_rules[clock_name].push_back(rule);
  }
  delete q;
  
  return true;
}


bool RDClockCache::hasClock(const QString &clock_name) const
{
  return d_clocks.contains(clock_name);
}


const RDClockCache::ClockData* RDClockCache::getClock(const QString &clock_name) const
{
  auto it=d_clocks.constFind(clock_name);
  if(it!=d_clocks.constEnd()) {
    return &it.value();
  }
  return nullptr;
}


QString RDClockCache::getClockForHour(int day_of_week, int hour) const
{
  // day_of_week: 1=Monday ... 7=Sunday (Qt standard)
  // hour: 0-23
  if(day_of_week<1 || day_of_week>7 || hour<0 || hour>23) {
    return QString();
  }
  
  int index=(day_of_week-1)*24+hour;
  if(index>=0 && index<d_clock_grid.size()) {
    return d_clock_grid[index];
  }
  return QString();
}


QVector<RDClockCache::ClockRule> RDClockCache::getRulesForClock(const QString &clock_name) const
{
  auto it=d_clock_rules.constFind(clock_name);
  if(it!=d_clock_rules.constEnd()) {
    return it.value();
  }
  return QVector<ClockRule>();
}


QStringList RDClockCache::getAllEventNames() const
{
  QSet<QString> event_names;
  
  for(auto it=d_clocks.constBegin(); it!=d_clocks.constEnd(); ++it) {
    const ClockData &clock=it.value();
    for(int i=0; i<clock.events.size(); i++) {
      if(!clock.events[i].event_name.isEmpty()) {
        event_names.insert(clock.events[i].event_name);
      }
    }
  }
  
  return event_names.values();
}
