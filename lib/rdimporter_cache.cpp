// rdimporter_cache.cpp
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

#include "rdimporter_cache.h"

// Static member initialization
RDImporterCache *RDImporterCache::s_instance = nullptr;
QMutex RDImporterCache::s_mutex;


RDImporterCache *RDImporterCache::instance()
{
  QMutexLocker locker(&s_mutex);
  if (s_instance == nullptr) {
    s_instance = new RDImporterCache();
  }
  return s_instance;
}


void RDImporterCache::destroyInstance()
{
  QMutexLocker locker(&s_mutex);
  if (s_instance != nullptr) {
    delete s_instance;
    s_instance = nullptr;
  }
}


RDImporterCache::RDImporterCache()
{
  d_next_id = 1;
}


RDImporterCache::~RDImporterCache()
{
  d_lines.clear();
}


void RDImporterCache::clear()
{
  d_lines.clear();
  d_next_id = 1;
}


void RDImporterCache::addLine(const RDImporterLine &line)
{
  RDImporterLine new_line = line;
  new_line.id = d_next_id++;
  d_lines.append(new_line);
}


QList<RDImporterLine*> RDImporterCache::getMatchingLines(int hour, 
                                                          int start_secs_min,
                                                          int start_secs_max)
{
  QList<RDImporterLine*> result;
  
  for (int i = 0; i < d_lines.size(); i++) {
    RDImporterLine &line = d_lines[i];
    if (line.start_hour == hour &&
        line.start_secs >= start_secs_min &&
        line.start_secs <= start_secs_max &&
        !line.event_used) {
      result.append(&line);
    }
  }
  
  // Sort by line_id (should already be in order, but ensure it)
  std::sort(result.begin(), result.end(),
            [](const RDImporterLine *a, const RDImporterLine *b) {
              return a->line_id < b->line_id;
            });
  
  return result;
}


void RDImporterCache::markLinesAsUsed(int hour, int start_secs_min, 
                                       int start_secs_max)
{
  for (int i = 0; i < d_lines.size(); i++) {
    RDImporterLine &line = d_lines[i];
    if (line.start_hour == hour &&
        line.start_secs >= start_secs_min &&
        line.start_secs <= start_secs_max &&
        !line.event_used) {
      line.event_used = true;
    }
  }
}


void RDImporterCache::markLinesAsUsed(const QList<int> &ids)
{
  for (int i = 0; i < d_lines.size(); i++) {
    if (ids.contains(d_lines[i].id)) {
      d_lines[i].event_used = true;
    }
  }
}


QList<const RDImporterLine*> RDImporterCache::getUnusedLines() const
{
  QList<const RDImporterLine*> result;
  
  for (int i = 0; i < d_lines.size(); i++) {
    if (!d_lines[i].event_used) {
      result.append(&d_lines[i]);
    }
  }
  
  // Sort by line_id
  std::sort(result.begin(), result.end(),
            [](const RDImporterLine *a, const RDImporterLine *b) {
              return a->line_id < b->line_id;
            });
  
  return result;
}


void RDImporterCache::resolveImpliedTimes()
{
  //
  // This replicates the logic from rdsvc.cpp that resolves implied
  // start times for inline events when SubEventInheritance == ParentEvent
  //
  int prev_hour = 0;
  int prev_secs = 0;
  int prev_length = 0;
  QList<int> prev_indices;  // Indices of lines needing time resolution
  
  for (int i = 0; i < d_lines.size(); i++) {
    RDImporterLine &line = d_lines[i];
    
    // Check if this line has explicit time information
    if (line.start_hour >= 0 && line.start_secs >= 0 && line.length >= 0) {
      // This line has explicit timing - use it to resolve previous lines
      if (prev_indices.size() > 0) {
        int len = 1000 * (line.start_secs - prev_secs) - prev_length;
        if (len < 0) {
          len = 0;
        }
        
        // Update all pending lines with resolved times
        for (int j = 0; j < prev_indices.size(); j++) {
          RDImporterLine &prev_line = d_lines[prev_indices[j]];
          prev_line.start_hour = prev_hour;
          prev_line.start_secs = prev_secs + prev_length / 1000;
          prev_line.length = len;
        }
        prev_indices.clear();
      }
      
      // Remember this line's timing for resolving subsequent lines
      prev_hour = line.start_hour;
      prev_secs = line.start_secs;
      prev_length = line.length;
    }
    else {
      // This line needs time resolution - add to pending list
      prev_indices.append(i);
    }
  }
  
  // Handle trailing lines that need resolution
  if (prev_indices.size() > 0) {
    for (int j = 0; j < prev_indices.size(); j++) {
      RDImporterLine &prev_line = d_lines[prev_indices[j]];
      prev_line.start_hour = prev_hour;
      prev_line.start_secs = prev_secs + prev_length / 1000;
      prev_line.length = 0;
    }
  }
}


RDImporterLine *RDImporterCache::lineAt(int index)
{
  if (index >= 0 && index < d_lines.size()) {
    return &d_lines[index];
  }
  return nullptr;
}


QList<RDImporterLine*> RDImporterCache::getTrafficLinksInRange(int hour, 
                                                                int min_secs, 
                                                                int max_secs)
{
  QList<RDImporterLine*> result;
  
  for (int i = 0; i < d_lines.size(); i++) {
    RDImporterLine &line = d_lines[i];
    if (line.type == RDLogLine::TrafficLink &&
        line.start_hour == hour &&
        line.start_secs >= min_secs &&
        line.start_secs < max_secs) {
      result.append(&line);
    }
  }
  
  return result;
}


QList<const RDImporterLine*> RDImporterCache::getLinesWithMissingTiming() const
{
  QList<const RDImporterLine*> result;
  
  for (int i = 0; i < d_lines.size(); i++) {
    const RDImporterLine &line = d_lines[i];
    // Check for TrafficLink, Marker, or Track types with missing timing
    if ((line.type == RDLogLine::TrafficLink ||
         line.type == RDLogLine::Marker ||
         line.type == RDLogLine::Track) &&
        (line.start_hour < 0 || line.start_secs < 0 || line.length < 0)) {
      result.append(&line);
    }
  }
  
  return result;
}
