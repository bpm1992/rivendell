// rdloggenerationcache.h
//
// Singleton cache for log generation to minimize database queries.
// Pre-loads carts, clocks, events, rules, and maintains an in-memory
// stack for artist/title separation during log generation.
//
// Key design principles:
// 1. All data is loaded at start - NO queries during generation
// 2. Log lines are buffered in memory and flushed in one transaction at end
// 3. Stack is maintained in memory during generation
// 4. COUNT and LINK_ID tracked in memory
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

#ifndef RDLOGGENERATIONCACHE_H
#define RDLOGGENERATIONCACHE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <QTime>
#include <QDateTime>
#include <QDate>

//
// Cart data structure
//
struct RDSchedCart {
  unsigned cart_number;
  QString artist;
  QString title;
  QStringList sched_codes;
};

//
// Stack line entry for separation tracking
//
struct RDStackEntry {
  unsigned sched_stack_id;
  unsigned cart_number;
  QString artist;
  QString title;
  QStringList sched_codes;
};

//
// Clock scheduler rule
//
struct RDSchedulerRule {
  QString code;
  int max_row;
  int min_wait;
  QString not_after;
  QString or_after;
  QString or_after_ii;
  bool max_row_valid;    // false if MAX_ROW was NULL in database
  bool min_wait_valid;   // false if MIN_WAIT was NULL in database
};

//
// Event import item
//
struct RDCachedImportItem {
  int event_type;
  unsigned cart_number;
  int trans_type;
  QString marker_comment;
};

//
// Event data structure
//
struct RDCachedEvent {
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
  QString color;
  int autofill_slop;
  QString nested_event;
  QString sched_group;
  int artist_sep;
  int title_sep;
  QString have_code;
  QString have_code2;
  QList<RDCachedImportItem> preimport_list;
  QList<RDCachedImportItem> postimport_list;
};

//
// Pending log line for batch insert
//
struct RDPendingLogLine {
  int line_id;
  int count;
  int type;               // RDLogLine::Type
  int source;             // RDLogLine::Source
  int start_time;         // msecs
  int grace_time;
  unsigned cart_number;
  int time_type;          // RDLogLine::TimeType
  int trans_type;         // RDLogLine::TransType
  QString comment;
  int event_length;
  QString link_event_name;
  int link_start_time;
  int link_length;
  int link_id;
  int link_start_slop;
  int link_end_slop;
};

//
// Pending stack entry for batch insert
//
struct RDPendingStackEntry {
  unsigned sched_stack_id;
  unsigned cart_number;
  QString artist;
  QString title;
  QStringList sched_codes;
};

//
// Cart validation data (for validate())
//
struct RDCartValidation {
  int cart_type;       // RDCart::Type
  QString title;
};

//
// Cut validity window (for validate())
//
struct RDCutValidity {
  QDateTime start_datetime;  // null = no restriction
  QDateTime end_datetime;    // null = no restriction
  QTime start_daypart;       // null = no restriction
  QTime end_daypart;         // null = no restriction
  bool dow[7];               // day of week flags (0=Sunday...6=Saturday)
  int length;                // must be > 0 to be valid
};

class RDLogGenerationCache
{
 public:
  //
  // Singleton access
  //
  static RDLogGenerationCache *instance();
  static void destroyInstance();

  //
  // Initialization - call at start of log generation
  //
  void initialize(const QString &service_name, const QString &log_name);
  bool isInitialized() const { return d_initialized; }
  QString logName() const { return d_log_name; }
  QString serviceName() const { return d_service_name; }
  
  //
  // Cart cache - keyed by group name, includes forced_length
  //
  void loadCartsForGroup(const QString &group_name);
  bool hasGroup(const QString &group_name) const;
  QList<RDSchedCart> getCartsForGroup(const QString &group_name) const;
  int getCartCountForGroup(const QString &group_name) const;
  
  //
  // Cart length cache - for GetLength() calls
  //
  void loadCartLengths(const QString &service_name);
  int getCartLength(unsigned cart_number, int def_length = 0) const;
  bool hasCartLength(unsigned cart_number) const;
  
  //
  // Stack cache - in-memory tracking of scheduled items
  // NOTE: Entries are buffered in memory and only written at flush time
  //
  void loadStackFromDatabase(const QString &service_name, int depth);
  unsigned getCurrentStackId() const { return d_current_stack_id; }
  unsigned getNextStackId();
  
  // Add a scheduled item to the in-memory stack only (buffered for later insert)
  void addToStack(unsigned cart_number, const QString &artist, 
                  const QString &title, const QStringList &sched_codes);
  
  // Query stack for separation rules - returns items within the specified range
  QStringList getTitlesInRange(int from_stack_id) const;
  QStringList getArtistsInRange(int from_stack_id) const;
  
  // Query stack for scheduler code rules
  QList<unsigned> getCartsWithCodeInRange(const QString &code, int from_stack_id) const;
  bool previousItemHasCode(const QString &code) const;
  
  //
  // Rule cache - keyed by clock name
  //
  void loadRulesForClock(const QString &clock_name);
  void loadAllRulesForService(const QString &service_name);
  bool hasClockRules(const QString &clock_name) const;
  QList<RDSchedulerRule> getRulesForClock(const QString &clock_name) const;
  
  //
  // Event cache - keyed by event name
  //
  void loadEventsForService(const QString &service_name);
  bool hasEvent(const QString &event_name) const;
  const RDCachedEvent *getEvent(const QString &event_name) const;
  int getTotalEvents() const;
  
  //
  // Clock lines cache - keyed by clock name
  //
  struct ClockLineEntry {
    QString event_name;
    int start_time;  // msecs from hour start
    int length;      // msecs
  };
  void loadClockLinesForService(const QString &service_name);
  bool hasClockLines(const QString &clock_name) const;
  QList<ClockLineEntry> getClockLines(const QString &clock_name) const;
  
  //
  // Service clock grid cache - which clock for each hour
  //
  void loadServiceClockGrid(const QString &service_name);
  QString getClockForHour(int day_of_week, int hour) const;
  
  //
  // Log line buffer - all inserts are buffered and flushed at end
  //
  int getNextCount() { return d_next_count++; }
  int getNextLinkId() { return d_next_link_id++; }
  int currentCount() const { return d_next_count; }
  int currentLinkId() const { return d_next_link_id; }
  
  void addLogLine(const RDPendingLogLine &line);
  int pendingLogLineCount() const { return d_pending_log_lines.size(); }
  
  //
  // Flush all pending data to database in a single transaction
  //
  bool flushToDatabase();
  
  //
  // Statistics
  //
  int getTotalCarts() const;
  int getTotalRules() const;
  int getStackSize() const { return d_stack.size(); }
  int getPendingStackSize() const { return d_pending_stack.size(); }
  
  //
  // Validation cache - for log validation without per-cart queries
  //
  void loadValidationData(const QList<unsigned> &cart_numbers);
  bool hasValidationData() const { return d_validation_loaded; }
  bool isCartValid(unsigned cart_number) const;
  int getCartType(unsigned cart_number) const;
  QString getCartTitle(unsigned cart_number) const;
  bool hasCutValidForDateTime(unsigned cart_number, const QDate &date, 
                              const QTime &time = QTime()) const;

 private:
  RDLogGenerationCache();
  ~RDLogGenerationCache();
  
  // Singleton instance
  static RDLogGenerationCache *s_instance;
  static QMutex s_mutex;
  
  // State
  bool d_initialized;
  QString d_service_name;
  QString d_log_name;
  
  // Cart cache: group_name -> list of carts
  QHash<QString, QList<RDSchedCart>> d_cart_cache;
  
  // Cart length cache: cart_number -> forced_length (mutable for lazy loading)
  mutable QHash<unsigned, int> d_cart_length_cache;
  
  // Stack cache: in-memory representation of STACK_LINES
  QList<RDStackEntry> d_stack;
  unsigned d_current_stack_id;
  
  // Pending stack entries (to be inserted at flush time)
  QList<RDPendingStackEntry> d_pending_stack;
  
  // Rule cache: clock_name -> list of rules
  QHash<QString, QList<RDSchedulerRule>> d_rule_cache;
  
  // Event cache: event_name -> event data (mutable for lazy loading)
  mutable QHash<QString, RDCachedEvent> d_event_cache;
  
  // Clock lines cache: clock_name -> list of event entries
  QHash<QString, QList<ClockLineEntry>> d_clock_lines_cache;
  
  // Service clock grid: 168 hours (day*24+hour) -> clock_name
  QHash<int, QString> d_clock_grid;
  
  // Log line buffer: all log lines pending insert
  QList<RDPendingLogLine> d_pending_log_lines;
  int d_next_count;
  int d_next_link_id;
  
  // Validation cache: cart_number -> validation data
  bool d_validation_loaded;
  QHash<unsigned, RDCartValidation> d_cart_validation_cache;
  QHash<unsigned, QList<RDCutValidity>> d_cut_validity_cache;
};

#endif  // RDLOGGENERATIONCACHE_H
