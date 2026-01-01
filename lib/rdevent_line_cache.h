// rdevent_line_cache.h
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

#ifndef RDEVENT_LINE_CACHE_H
#define RDEVENT_LINE_CACHE_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <QColor>

// Forward declaration
class RDCartCache;

class RDEventLineCache
{
 public:
  RDEventLineCache();
  ~RDEventLineCache();
  
  // Load all events used by clocks in a service
  bool loadEventsForService(const QString &svc_name);
  
  // Load events by explicit list of names
  bool loadEvents(const QStringList &event_names);
  
  void clear();
  
  //=========================================================================
  // EVENT DATA
  //=========================================================================
  struct EventImportItem {
    int event_type;
    unsigned cart_number;
    int trans_type;
    QString marker_comment;
  };
  
  struct EventData {
    QString name;
    int preposition;
    int time_type;
    int grace_time;
    bool use_autofill;
    bool use_timescale;
    int import_source;
    int start_slop;
    int end_slop;
    int first_trans_type;
    int default_trans_type;
    QColor color;
    int autofill_slop;
    QString nested_event;
    QString sched_group;
    int artist_sep;
    int title_sep;
    QString have_code;
    QString have_code2;
    QVector<EventImportItem> preimport_items;
    QVector<EventImportItem> postimport_items;
    
    void clear();
  };
  
  bool hasEvent(const QString &event_name) const;
  const EventData* getEvent(const QString &event_name) const;
  int eventCount() const { return d_events.size(); }
  
  //=========================================================================
  // STACK/HISTORY DATA (for deconflicting)
  //=========================================================================
  // Stack entries track what was scheduled for artist/title separation rules.
  // Note: We pull cart metadata from RDCartCache when building stack entries.
  
  struct StackEntry {
    int stack_id;
    unsigned cart_number;
    QString artist;           // Cached from RDCartCache at insert time
    QString title;            // Cached from RDCartCache at insert time
    QStringList sched_codes;  // Cached from RDCartCache at insert time
  };
  
  // Load stack history for service
  bool loadStack(const QString &svc_name, int max_depth=100);
  
  // Stack queries - work from cached data
  bool isTitleInStack(const QString &title, int depth) const;
  bool isArtistInStack(const QString &artist, int depth) const;
  bool hasSchedCodeAtPosition(const QString &code, int stack_pos) const;
  int countSchedCodeInRange(const QString &code, int from_pos, int to_pos) const;
  int currentStackId() const { return d_current_stack_id; }
  int stackDepth() const { return d_stack.size(); }
  
  // Add new entry to stack during generation (updates in-memory cache)
  // Pulls artist/title/codes from RDCartCache
  void pushStackEntry(unsigned cart_number, RDCartCache *cart_cache);
  
  // Alternative: push with explicit data (when cart_cache not available)
  void pushStackEntry(unsigned cart_number, const QString &artist,
                      const QString &title, const QStringList &sched_codes);

 private:
  bool loadEventImportLists();
  
  QMap<QString, EventData> d_events;                    // event_name -> data
  QVector<StackEntry> d_stack;                          // ordered by stack_id desc
  int d_current_stack_id;
  QString d_svc_name;                                   // For stack operations
};

#endif  // RDEVENT_LINE_CACHE_H
