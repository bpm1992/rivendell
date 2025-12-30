// rdcut_cache.cpp
//
// Cut data cache for batch loading 
//
// Purpose: Eliminate ~800 database queries per log load by batch-loading all
// cut data for all carts in a log with a single SQL query. Implements cut
// rotation logic (Sequential/Random/Weighted) using cached data.
//
// Cache lifetime: 5 minutes (configurable) to balance performance with
// freshness for ad-hoc carts added during playback.
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

#include "rdapplication.h"
#include "rdconf.h"
#include "rdcut_cache.h"
#include "rddb.h"
#include "rdescape_string.h"

#include <QRandomGenerator>
#include <limits>

RDCutData::RDCutData()
{
  clear();
}


void RDCutData::clear()
{
  cut_name="";
  cut_number=0;
  length=0;
  start_point=-1;
  end_point=-1;
  segue_start_point=-1;
  segue_end_point=-1;
  segue_gain=0;
  talk_start_point=-1;
  talk_end_point=-1;
  hook_start_point=-1;
  hook_end_point=-1;
  outcue="";
  isrc="";
  isci="";
  description="";
  recording_mbid="";
  release_mbid="";
  start_datetime=QDateTime();
  end_datetime=QDateTime();
  start_daypart=QTime();
  end_daypart=QTime();
  mon=false;
  tue=false;
  wed=false;
  thu=false;
  fri=false;
  sat=false;
  sun=false;
  weight=1;
  local_counter=0;
  last_play_datetime=QDateTime();
  play_order=0;
  evergreen=false;
}


RDCutCache::RDCutCache()
{
  cache_timeout_msecs=300000;  // Default: 5 minutes
  clear();
}


RDCutCache::~RDCutCache()
{
  clear();
}


void RDCutCache::clear()
{
  cuts_by_cart.clear();
  cuts_by_name.clear();
  cache_load_time=QDateTime();
  cache_last_access=QDateTime();
}


int RDCutCache::batchLoadCuts(const QVector<unsigned> &cart_numbers)
{
  if(cart_numbers.isEmpty()) {
    return 0;
  }
  
  clear();
  
  // Build single SQL query to load ALL cuts for ALL carts in the log
  // Uses IN clause: WHERE CART_NUMBER IN (001234, 001235, ...)
  // This replaces ~400-800 individual queries with one efficient query
  QString in_clause;
  for(int i=0; i<cart_numbers.size(); i++) {
    if(i>0) {
      in_clause+=",";
    }
    in_clause+=QString::number(cart_numbers[i]);
  }
  
  // Single batch query to load all cuts for all carts
  QString sql=QString("select ")+
    "`CART_NUMBER`,"+           // 00
    "`CUT_NAME`,"+              // 01
    "`LENGTH`,"+                // 02
    "`START_POINT`,"+           // 03
    "`END_POINT`,"+             // 04
    "`SEGUE_START_POINT`,"+     // 05
    "`SEGUE_END_POINT`,"+       // 06
    "`SEGUE_GAIN`,"+            // 07
    "`TALK_START_POINT`,"+      // 08
    "`TALK_END_POINT`,"+        // 09
    "`HOOK_START_POINT`,"+      // 10
    "`HOOK_END_POINT`,"+        // 11
    "`OUTCUE`,"+                // 12
    "`ISRC`,"+                  // 13
    "`ISCI`,"+                  // 14
    "`DESCRIPTION`,"+           // 15
    "`RECORDING_MBID`,"+        // 16
    "`RELEASE_MBID`,"+          // 17
    "`START_DATETIME`,"+        // 18
    "`END_DATETIME`,"+          // 19
    "`START_DAYPART`,"+         // 20
    "`END_DAYPART`,"+           // 21
    "`MON`,"+                   // 22
    "`TUE`,"+                   // 23
    "`WED`,"+                   // 24
    "`THU`,"+                   // 25
    "`FRI`,"+                   // 26
    "`SAT`,"+                   // 27
    "`SUN`,"+                   // 28
    "`WEIGHT`,"+                // 29
    "`LOCAL_COUNTER`,"+         // 30
    "`LAST_PLAY_DATETIME`,"+   // 31
    "`PLAY_ORDER`,"+            // 32
    "`EVERGREEN` "+             // 33
    "from `CUTS` where "+
    "`CART_NUMBER` in ("+in_clause+") "+
    "order by `CART_NUMBER`,`CUT_NAME`";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  int count=0;
  
  while(q->next()) {
    RDCutData cut;
    unsigned cart_num=q->value(0).toUInt();
    
    cut.cut_name=q->value(1).toString();
    cut.cut_number=cut.cut_name.right(3).toInt();
    cut.length=q->value(2).toUInt();
    cut.start_point=q->value(3).toInt();
    cut.end_point=q->value(4).toInt();
    cut.segue_start_point=q->value(5).toInt();
    cut.segue_end_point=q->value(6).toInt();
    cut.segue_gain=q->value(7).toInt();
    cut.talk_start_point=q->value(8).toInt();
    cut.talk_end_point=q->value(9).toInt();
    cut.hook_start_point=q->value(10).toInt();
    cut.hook_end_point=q->value(11).toInt();
    cut.outcue=q->value(12).toString();
    cut.isrc=q->value(13).toString();
    cut.isci=q->value(14).toString();
    cut.description=q->value(15).toString();
    cut.recording_mbid=q->value(16).toString();
    cut.release_mbid=q->value(17).toString();
    cut.start_datetime=q->value(18).toDateTime();
    cut.end_datetime=q->value(19).toDateTime();
    cut.start_daypart=q->value(20).toTime();
    cut.end_daypart=q->value(21).toTime();
    cut.mon=RDBool(q->value(22).toString());
    cut.tue=RDBool(q->value(23).toString());
    cut.wed=RDBool(q->value(24).toString());
    cut.thu=RDBool(q->value(25).toString());
    cut.fri=RDBool(q->value(26).toString());
    cut.sat=RDBool(q->value(27).toString());
    cut.sun=RDBool(q->value(28).toString());
    cut.weight=q->value(29).toInt();
    cut.local_counter=q->value(30).toInt();
    cut.last_play_datetime=q->value(31).toDateTime();
    cut.play_order=q->value(32).toInt();
    cut.evergreen=RDBool(q->value(33).toString());
    
    // Store by cart number
    cuts_by_cart[cart_num].push_back(cut);
    
    // Store by cut name for fast lookup
    cuts_by_name[cut.cut_name]=cut;
    
    count++;
  }
  
  delete q;
  
  // Set cache timestamps
  cache_load_time=QDateTime::currentDateTime();
  cache_last_access=cache_load_time;
  
  rda->syslog(LOG_DEBUG,"RDCutCache: loaded %d cuts for %d carts in single query (timeout=%dms)",
              count,cart_numbers.size(),cache_timeout_msecs);
  
  return count;
}


QVector<RDCutData> RDCutCache::getCutsForCart(unsigned cart_number) const
{
  if(cuts_by_cart.contains(cart_number)) {
    return cuts_by_cart[cart_number];
  }
  return QVector<RDCutData>();
}


RDCutData RDCutCache::getCut(const QString &cut_name) const
{
  if(cuts_by_name.contains(cut_name)) {
    return cuts_by_name[cut_name];
  }
  return RDCutData();
}


bool RDCutCache::getCutByName(const QString &cut_name, RDCutData *cut_data) const
{
  if(cuts_by_name.contains(cut_name)) {
    *cut_data=cuts_by_name[cut_name];
    const_cast<RDCutCache*>(this)->touch();  // Update access time
    return true;
  }
  return false;
}


bool RDCutCache::hasCut(const QString &cut_name) const
{
  return cuts_by_name.contains(cut_name);
}


int RDCutCache::cutCount() const
{
  return cuts_by_name.size();
}


QString RDCutCache::selectCut(unsigned cart_number, RDCart::PlayOrder play_order,
                              bool use_weighting, const QTime &time)
{
  // Cache-based cut selection - mirrors RDCart::selectCut/GetNextCut
  // Implements rotation respecting:
  //   * use_weighting ("Schedule Cuts By" = By Weight vs By Specific Order)
  //   * cart play_order (Sequence vs Random) when not weighting
  //   * daypart/day-of-week/datetime validity
  // Maintains in-cache rotation state so successive selections rotate even
  //   while the cache is alive.

  if(!cuts_by_cart.contains(cart_number)) {
    return QString();
  }

  // Non-const reference so we can update rotation state in-cache
  QVector<RDCutData> &cuts=cuts_by_cart[cart_number];
  if(cuts.isEmpty()) {
    return QString();
  }

  QDate current_date=QDate::currentDate();
  QDateTime current_datetime(current_date,time);
  int day_of_week=current_date.dayOfWeek();

  QVector<int> valid_indices;
  QVector<int> evergreen_indices;

  for(int i=0;i<cuts.size();i++) {
    const RDCutData &cut=cuts[i];

    if(cut.length==0) {
      continue;
    }

    if(cut.evergreen) {
      evergreen_indices.push_back(i);
      continue;  // Evergreen handled only as fallback, like legacy
    }

    bool datetime_valid=true;
    if(cut.start_datetime.isValid() && (current_datetime<cut.start_datetime)) {
      datetime_valid=false;
    }
    if(datetime_valid && cut.end_datetime.isValid() &&
       (current_datetime>cut.end_datetime)) {
      datetime_valid=false;
    }
    if(!datetime_valid) {
      continue;
    }

    bool day_valid=false;
    switch(day_of_week) {
      case 1: day_valid=cut.mon; break;
      case 2: day_valid=cut.tue; break;
      case 3: day_valid=cut.wed; break;
      case 4: day_valid=cut.thu; break;
      case 5: day_valid=cut.fri; break;
      case 6: day_valid=cut.sat; break;
      case 7: day_valid=cut.sun; break;
    }
    if(!day_valid) {
      continue;
    }

    if(cut.start_daypart.isValid()) {
      if((time<cut.start_daypart)||(time>cut.end_daypart)) {
        continue;
      }
    }

    valid_indices.push_back(i);
  }

  // Helper: weighted selection using LOCAL_COUNTER/WEIGHT ratio
  auto pickWeighted=[&cuts](const QVector<int> &indices)->int {
    if(indices.isEmpty()) {
      return -1;
    }
    double best_ratio=std::numeric_limits<double>::max();
    int best_idx=-1;
    for(int idx:indices) {
      const RDCutData &c=cuts[idx];
      int weight=(c.weight<=0)?1:c.weight;
      double ratio=(double)c.local_counter/(double)weight;
      if((best_idx<0)||
         (ratio<best_ratio)||
         ((ratio==best_ratio)&&(!cuts[best_idx].last_play_datetime.isNull())&&
          (c.last_play_datetime<cuts[best_idx].last_play_datetime))||
         ((ratio==best_ratio)&&(c.last_play_datetime==cuts[best_idx].last_play_datetime)&&
          (c.cut_number<cuts[best_idx].cut_number))) {
        best_ratio=ratio;
        best_idx=idx;
      }
    }
    return best_idx;
  };

  // Helper: sequential rotation (Specific Order)
  auto pickSequential=[&cuts](const QVector<int> &indices)->int {
    if(indices.isEmpty()) {
      return -1;
    }

    // Find most recently played cut among valid indices
    int last_idx=-1;
    QDateTime last_dt;
    for(int idx:indices) {
      const RDCutData &c=cuts[idx];
      if(c.last_play_datetime.isNull()) {
        continue;
      }
      if((last_idx<0)||(c.last_play_datetime>last_dt)) {
        last_idx=idx;
        last_dt=c.last_play_datetime;
      }
    }

    // If nothing has played yet, pick lowest play_order then lowest cut_number
    if(last_idx<0) {
      int best_idx=indices[0];
      for(int idx:indices) {
        const RDCutData &c=cuts[idx];
        const RDCutData &b=cuts[best_idx];
        if((c.play_order<b.play_order)||
           ((c.play_order==b.play_order)&&(c.cut_number<b.cut_number))) {
          best_idx=idx;
        }
      }
      return best_idx;
    }

    int current_order=cuts[last_idx].play_order;
    int wrapped_idx=-1;
    int wrapped_order=std::numeric_limits<int>::max();
    int next_idx=-1;
    int next_order=std::numeric_limits<int>::max();

    for(int idx:indices) {
      const RDCutData &c=cuts[idx];
      if(c.play_order>current_order) {
        if(c.play_order<next_order) {
          next_order=c.play_order;
          next_idx=idx;
        }
      }
      if(c.play_order<wrapped_order) {
        wrapped_order=c.play_order;
        wrapped_idx=idx;
      }
    }

    return (next_idx>=0)?next_idx:wrapped_idx;
  };

  auto pickRandom=[&cuts](const QVector<int> &indices)->int {
    if(indices.isEmpty()) {
      return -1;
    }
    int choice=QRandomGenerator::global()->bounded(indices.size());
    return indices[choice];
  };

  // Decide which pool to use (valid cuts first, then evergreen fallback)
  QVector<int> candidate_indices=valid_indices;
  if(candidate_indices.isEmpty()) {
    candidate_indices=evergreen_indices;
  }

  if(candidate_indices.isEmpty()) {
    return QString();
  }

  int selected_idx=-1;
  if(use_weighting) {
    selected_idx=pickWeighted(candidate_indices);
  }
  else {
    if(play_order==RDCart::Random) {
      selected_idx=pickRandom(candidate_indices);
    }
    else {
      selected_idx=pickSequential(candidate_indices);
    }
  }

  if(selected_idx<0) {
    return QString();
  }

  // Update in-cache rotation state so repeated selections rotate correctly
  RDCutData &selected_cut=cuts[selected_idx];
  selected_cut.local_counter++;
  selected_cut.last_play_datetime=QDateTime::currentDateTime();

  // Keep name index in sync
  cuts_by_name[selected_cut.cut_name]=selected_cut;

  touch();
  return selected_cut.cut_name;
}


void RDCutCache::setCacheTimeout(int msecs)
{
  cache_timeout_msecs=msecs;
}


bool RDCutCache::isValid() const
{
  // Empty cache is invalid
  if(cuts_by_name.isEmpty()) {
    return false;
  }
  
  // No timeout means cache never expires
  if(cache_timeout_msecs<=0) {
    return true;
  }
  
  // Check if cache has expired
  QDateTime now=QDateTime::currentDateTime();
  qint64 age_msecs=cache_load_time.msecsTo(now);
  return age_msecs<cache_timeout_msecs;
}


void RDCutCache::touch()
{
  cache_last_access=QDateTime::currentDateTime();
}


QDateTime RDCutCache::loadTime() const
{
  return cache_load_time;
}
