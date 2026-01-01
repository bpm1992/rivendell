// rdclock_cache.h
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

#ifndef RDCLOCK_CACHE_H
#define RDCLOCK_CACHE_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QColor>

class RDClockCache
{
 public:
  RDClockCache();
  ~RDClockCache();
  
  // Load all clocks used by a service
  bool loadClocksForService(const QString &svc_name);
  void clear();
  
  //=========================================================================
  // CLOCK DATA
  //=========================================================================
  struct ClockEvent {
    QString event_name;
    int start_time_ms;  // msecs from hour start
    int length_ms;
  };
  
  struct ClockData {
    QString name;
    QString short_name;
    QColor color;
    QString remarks;
    unsigned artist_sep;
    QVector<ClockEvent> events;
  };
  
  bool hasClock(const QString &clock_name) const;
  const ClockData* getClock(const QString &clock_name) const;
  int clockCount() const { return d_clocks.size(); }
  
  //=========================================================================
  // SERVICE CLOCK GRID (168 hours = 7 days × 24 hours)
  //=========================================================================
  // day_of_week: 1=Monday ... 7=Sunday (Qt standard)
  // hour: 0-23
  QString getClockForHour(int day_of_week, int hour) const;
  
  //=========================================================================
  // CLOCK SCHEDULER RULES
  //=========================================================================
  struct ClockRule {
    QString code;
    int max_row;
    int min_wait;
    QString not_after;
    QString or_after;
    QString or_after_ii;
    bool max_row_valid;    // false if MAX_ROW was NULL in database
    bool min_wait_valid;   // false if MIN_WAIT was NULL in database
  };
  
  QVector<ClockRule> getRulesForClock(const QString &clock_name) const;
  
  //=========================================================================
  // UTILITY
  //=========================================================================
  // Get list of all unique event names used across loaded clocks
  QStringList getAllEventNames() const;

 private:
  bool loadClockGrid(const QString &svc_name);
  bool loadClockRules();
  
  QMap<QString, ClockData> d_clocks;              // clock_name -> data
  QVector<QString> d_clock_grid;                  // 168 entries indexed by (day-1)*24+hour
  QMap<QString, QVector<ClockRule>> d_clock_rules; // clock_name -> rules
};

#endif  // RDCLOCK_CACHE_H
