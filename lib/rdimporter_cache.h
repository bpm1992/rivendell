// rdimporter_cache.h
//
// In-memory cache for log import data, replacing IMPORTER_LINES table usage.
//
//   Copyright (C) 2025 Fred Gleason <fredg@paravelsystems.com>
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

#ifndef RDIMPORTER_CACHE_H
#define RDIMPORTER_CACHE_H

#include <QString>
#include <QList>
#include <QTime>
#include <QMutex>

#include "rdlog_line.h"

//
// Structure representing a single imported line
//
struct RDImporterLine {
  int id;                    // Auto-incremented ID
  int file_line;             // Line number in source file
  int line_id;               // Logical line ID
  int type;                  // RDLogLine::Type
  int start_hour;            // -1 if not set
  int start_secs;            // -1 if not set
  unsigned cart_number;
  QString title;
  int length;                // -1 if not set
  int trans_type;            // RDLogLine::TransType
  int time_type;             // RDLogLine::TimeType
  int grace_time;
  QString ext_data;
  QString ext_event_id;
  QString ext_annc_type;
  QString ext_cart_name;
  QTime link_start_time;
  int link_length;
  bool event_used;
  
  RDImporterLine() {
    id = 0;
    file_line = 0;
    line_id = 0;
    type = 0;
    start_hour = -1;
    start_secs = -1;
    cart_number = 0;
    length = -1;
    trans_type = RDLogLine::NoTrans;
    time_type = RDLogLine::Relative;
    grace_time = 0;
    link_length = 0;
    event_used = false;
  }
};


//
// Singleton cache for importer lines
//
class RDImporterCache
{
 public:
  static RDImporterCache *instance();
  static void destroyInstance();
  
  // Initialize/clear the cache for a new import operation
  void clear();
  
  // Add a line to the cache
  void addLine(const RDImporterLine &line);
  
  // Get all lines (for iteration)
  const QList<RDImporterLine> &lines() const { return d_lines; }
  
  // Get lines matching criteria for linkLog operations
  // Returns lines where:
  //   start_hour == hour AND
  //   start_secs >= start_secs_min AND start_secs <= start_secs_max AND
  //   event_used == false
  // Ordered by line_id
  QList<RDImporterLine*> getMatchingLines(int hour, int start_secs_min, 
                                          int start_secs_max);
  
  // Mark matching lines as used (by time range)
  void markLinesAsUsed(int hour, int start_secs_min, int start_secs_max);
  
  // Mark specific lines as used (by list of IDs)
  void markLinesAsUsed(const QList<int> &ids);
  
  // Get unused lines (for "events not placed" report)
  QList<const RDImporterLine*> getUnusedLines() const;
  
  // Resolve implied start times for lines without explicit times
  // This replicates the ParentEvent inheritance logic
  void resolveImpliedTimes();
  
  // Get line count
  int lineCount() const { return d_lines.size(); }
  
  // Access line by index (for implied time resolution)
  RDImporterLine *lineAt(int index);
  
  // Find traffic link lines within a time range for validation
  // Returns lines where type==TrafficLink, start_hour==hour, 
  // start_secs >= min_secs and start_secs < max_secs
  QList<RDImporterLine*> getTrafficLinksInRange(int hour, int min_secs, int max_secs);
  
  // Get lines of specific types that lack explicit timing (for validation)
  QList<const RDImporterLine*> getLinesWithMissingTiming() const;
  
 private:
  RDImporterCache();
  ~RDImporterCache();
  
  static RDImporterCache *s_instance;
  static QMutex s_mutex;
  
  QList<RDImporterLine> d_lines;
  int d_next_id;
};

#endif  // RDIMPORTER_CACHE_H
