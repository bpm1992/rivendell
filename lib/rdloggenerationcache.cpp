// rdloggenerationcache.cpp
//
// Singleton cache for log generation to minimize database queries.
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

#include <stdio.h>
#include <QTime>
#include <QSet>
#include <QDateTime>

#include "rdloggenerationcache.h"
#include "rddb.h"
#include "rdescape_string.h"

// Static member initialization
RDLogGenerationCache *RDLogGenerationCache::s_instance = nullptr;
QMutex RDLogGenerationCache::s_mutex;


RDLogGenerationCache *RDLogGenerationCache::instance()
{
  QMutexLocker locker(&s_mutex);
  if (s_instance == nullptr) {
    s_instance = new RDLogGenerationCache();
  }
  return s_instance;
}


void RDLogGenerationCache::destroyInstance()
{
  QMutexLocker locker(&s_mutex);
  if (s_instance != nullptr) {
    delete s_instance;
    s_instance = nullptr;
  }
}


RDLogGenerationCache::RDLogGenerationCache()
{
  d_initialized = false;
  d_validation_loaded = false;
  d_current_stack_id = 0;
  d_next_count = 0;
  d_next_link_id = 0;
}


RDLogGenerationCache::~RDLogGenerationCache()
{
  d_cart_cache.clear();
  d_cart_length_cache.clear();
  d_stack.clear();
  d_pending_stack.clear();
  d_rule_cache.clear();
  d_event_cache.clear();
  d_pending_log_lines.clear();
  d_cart_validation_cache.clear();
  d_cut_validity_cache.clear();
}


void RDLogGenerationCache::initialize(const QString &service_name, const QString &log_name)
{
  QTime timer;
  timer.start();
  
  // Clear any existing cache data
  d_cart_cache.clear();
  d_cart_length_cache.clear();
  d_stack.clear();
  d_pending_stack.clear();
  d_rule_cache.clear();
  d_event_cache.clear();
  d_clock_lines_cache.clear();
  d_clock_grid.clear();
  d_pending_log_lines.clear();
  d_cart_validation_cache.clear();
  d_cut_validity_cache.clear();
  d_validation_loaded = false;
  
  d_service_name = service_name;
  d_log_name = log_name;
  d_next_count = 0;
  d_next_link_id = 0;
  d_initialized = true;
  
  // Load the stack from database for this service
  // Use a reasonable depth to cover artist/title separation needs (typically 100-200)
  loadStackFromDatabase(service_name, 200);
  
  // Pre-load cart lengths for all carts that might be used
  loadCartLengths(service_name);
  
  // Pre-load service clock grid (which clock for each of 168 hours)
  loadServiceClockGrid(service_name);
  
  // Pre-load clock lines for all clocks used by this service
  loadClockLinesForService(service_name);
  
  fprintf(stderr, "DEBUG: RDLogGenerationCache initialized for service '%s', log '%s' in %d ms\n",
          service_name.toUtf8().constData(), log_name.toUtf8().constData(), timer.elapsed());
}


void RDLogGenerationCache::loadCartsForGroup(const QString &group_name)
{
  if (d_cart_cache.contains(group_name)) {
    return;  // Already loaded
  }
  
  QTime timer;
  timer.start();
  
  QString sql = QString("SELECT `NUMBER`, `ARTIST`, `TITLE`, ") +
    "CONCAT(GROUP_CONCAT(RPAD(`SC`.`SCHED_CODE`,11,'|') SEPARATOR ''),'.') AS `SCHED_CODES` " +
    "FROM `CART` LEFT JOIN `CART_SCHED_CODES` AS `SC` ON (`NUMBER`=`SC`.`CART_NUMBER`) " +
    "WHERE `GROUP_NAME`='" + RDEscapeString(group_name) + "' " +
    "GROUP BY `NUMBER`";
  
  QList<RDSchedCart> carts;
  RDSqlQuery *q = new RDSqlQuery(sql);
  while (q->next()) {
    RDSchedCart cart;
    cart.cart_number = q->value(0).toUInt();
    cart.artist = q->value(1).toString();
    cart.title = q->value(2).toString();
    
    QStringList codes = q->value(3).toString().split("|", QString::SkipEmptyParts);
    if ((codes.size() > 0) && (codes.last() == ".")) {
      codes.removeLast();
    }
    cart.sched_codes = codes;
    
    carts.append(cart);
  }
  delete q;
  
  d_cart_cache.insert(group_name, carts);
  
  //fprintf(stderr, "DEBUG: Loaded %d carts for group '%s' in %d ms\n",
  //        carts.size(), group_name.toUtf8().constData(), timer.elapsed());
}


void RDLogGenerationCache::loadCartLengths(const QString &service_name)
{
  QTime timer;
  timer.start();
  
  // Load FORCED_LENGTH for all carts in groups used by this service's events
  // This covers pre-import, post-import, and scheduler carts
  QString sql = QString("SELECT DISTINCT `C`.`NUMBER`, `C`.`FORCED_LENGTH` FROM `CART` `C` ") +
    "INNER JOIN `GROUPS` `G` ON `C`.`GROUP_NAME`=`G`.`NAME` " +
    "WHERE `C`.`FORCED_LENGTH` > 0";
  
  RDSqlQuery *q = new RDSqlQuery(sql);
  while (q->next()) {
    unsigned cart_num = q->value(0).toUInt();
    int length = q->value(1).toInt();
    d_cart_length_cache.insert(cart_num, length);
  }
  delete q;
  
  //fprintf(stderr, "DEBUG: Loaded %d cart lengths in %d ms\n",
  //        d_cart_length_cache.size(), timer.elapsed());
}


int RDLogGenerationCache::getCartLength(unsigned cart_number, int def_length) const
{
  QHash<unsigned, int>::const_iterator it = d_cart_length_cache.find(cart_number);
  if (it != d_cart_length_cache.end()) {
    return it.value();
  }
  return def_length;
}


bool RDLogGenerationCache::hasCartLength(unsigned cart_number) const
{
  return d_cart_length_cache.contains(cart_number);
}

bool RDLogGenerationCache::hasGroup(const QString &group_name) const
{
  return d_cart_cache.contains(group_name);
}


QList<RDSchedCart> RDLogGenerationCache::getCartsForGroup(const QString &group_name) const
{
  return d_cart_cache.value(group_name);
}


int RDLogGenerationCache::getCartCountForGroup(const QString &group_name) const
{
  if (d_cart_cache.contains(group_name)) {
    return d_cart_cache.value(group_name).size();
  }
  return 0;
}


void RDLogGenerationCache::loadStackFromDatabase(const QString &service_name, int depth)
{
  QTime timer;
  timer.start();
  
  d_stack.clear();
  d_current_stack_id = 0;
  
  // First get the max stack ID
  QString sql = QString("SELECT MAX(`SCHED_STACK_ID`) FROM `STACK_LINES` WHERE ") +
    "`SERVICE_NAME`='" + RDEscapeString(service_name) + "'";
  
  RDSqlQuery *q = new RDSqlQuery(sql);
  unsigned max_stack_id = 0;
  if (q->next() && !q->value(0).isNull()) {
    max_stack_id = q->value(0).toUInt();
  }
  delete q;
  
  d_current_stack_id = max_stack_id;
  
  // Load recent stack entries with their sched codes
  // We need entries within the separation window (typically artist_sep up to 100)
  unsigned from_id = (max_stack_id > (unsigned)depth) ? (max_stack_id - depth) : 0;
  
  sql = QString("SELECT ") +
    "`SL`.`SCHED_STACK_ID`, " +   // 0
    "`SL`.`CART`, " +              // 1
    "`SL`.`ARTIST`, " +            // 2
    "`SL`.`TITLE`, " +             // 3
    "CONCAT(GROUP_CONCAT(RPAD(`SSC`.`SCHED_CODE`,11,'|') SEPARATOR ''),'.') AS `SCHED_CODES` " +  // 4
    "FROM `STACK_LINES` AS `SL` " +
    "LEFT JOIN `STACK_SCHED_CODES` AS `SSC` ON `SL`.`ID`=`SSC`.`STACK_LINES_ID` " +
    "WHERE `SL`.`SERVICE_NAME`='" + RDEscapeString(service_name) + "' " +
    QString::asprintf("AND `SL`.`SCHED_STACK_ID` >= %u ", from_id) +
    "GROUP BY `SL`.`ID` " +
    "ORDER BY `SL`.`SCHED_STACK_ID` ASC";
  
  q = new RDSqlQuery(sql);
  while (q->next()) {
    RDStackEntry entry;
    entry.sched_stack_id = q->value(0).toUInt();
    entry.cart_number = q->value(1).toUInt();
    entry.artist = q->value(2).toString();
    entry.title = q->value(3).toString();
    
    QStringList codes = q->value(4).toString().split("|", QString::SkipEmptyParts);
    if ((codes.size() > 0) && (codes.last() == ".")) {
      codes.removeLast();
    }
    entry.sched_codes = codes;
    
    d_stack.append(entry);
  }
  delete q;
  
  //fprintf(stderr, "DEBUG: Loaded %d stack entries (stack_id %u to %u) in %d ms\n",
  //        d_stack.size(), from_id, max_stack_id, timer.elapsed());
}


unsigned RDLogGenerationCache::getNextStackId()
{
  return ++d_current_stack_id;
}


void RDLogGenerationCache::addToStack(unsigned cart_number,
                                      const QString &artist, const QString &title,
                                      const QStringList &sched_codes)
{
  // Add to in-memory stack for queries during generation
  RDStackEntry entry;
  entry.sched_stack_id = d_current_stack_id;
  entry.cart_number = cart_number;
  entry.artist = artist;
  entry.title = title;
  entry.sched_codes = sched_codes;
  d_stack.append(entry);
  
  // Also add to pending buffer for batch insert at end
  RDPendingStackEntry pending;
  pending.sched_stack_id = d_current_stack_id;
  pending.cart_number = cart_number;
  pending.artist = artist;
  pending.title = title;
  pending.sched_codes = sched_codes;
  d_pending_stack.append(pending);
  
  // Trim in-memory stack if it gets too large (keep last 300 entries)
  while (d_stack.size() > 300) {
    d_stack.removeFirst();
  }
}


QStringList RDLogGenerationCache::getTitlesInRange(int from_stack_id) const
{
  QStringList titles;
  for (int i = 0; i < d_stack.size(); i++) {
    if ((int)d_stack[i].sched_stack_id >= from_stack_id) {
      titles.append(d_stack[i].title);
    }
  }
  return titles;
}


QStringList RDLogGenerationCache::getArtistsInRange(int from_stack_id) const
{
  QStringList artists;
  for (int i = 0; i < d_stack.size(); i++) {
    if ((int)d_stack[i].sched_stack_id >= from_stack_id) {
      artists.append(d_stack[i].artist);
    }
  }
  return artists;
}


QList<unsigned> RDLogGenerationCache::getCartsWithCodeInRange(const QString &code, 
                                                               int from_stack_id) const
{
  QList<unsigned> carts;
  QString normalized_code = code;
  normalized_code += "          ";
  normalized_code = normalized_code.left(11);
  
  for (int i = 0; i < d_stack.size(); i++) {
    if ((int)d_stack[i].sched_stack_id >= from_stack_id) {
      for (int j = 0; j < d_stack[i].sched_codes.size(); j++) {
        if (d_stack[i].sched_codes[j] == normalized_code || 
            d_stack[i].sched_codes[j].trimmed() == code.trimmed()) {
          carts.append(d_stack[i].cart_number);
          break;
        }
      }
    }
  }
  return carts;
}


bool RDLogGenerationCache::previousItemHasCode(const QString &code) const
{
  if (d_stack.isEmpty()) {
    return false;
  }
  
  // Find the entry with stack_id == current_stack_id - 1
  unsigned prev_id = d_current_stack_id - 1;
  QString normalized_code = code;
  normalized_code += "          ";
  normalized_code = normalized_code.left(11);
  
  for (int i = d_stack.size() - 1; i >= 0; i--) {
    if (d_stack[i].sched_stack_id == prev_id) {
      for (int j = 0; j < d_stack[i].sched_codes.size(); j++) {
        if (d_stack[i].sched_codes[j] == normalized_code ||
            d_stack[i].sched_codes[j].trimmed() == code.trimmed()) {
          return true;
        }
      }
      return false;
    }
  }
  return false;
}


void RDLogGenerationCache::loadRulesForClock(const QString &clock_name)
{
  if (d_rule_cache.contains(clock_name)) {
    return;  // Already loaded
  }
  
  QString sql = QString("SELECT ") +
    "`CODE`, " +        // 0
    "`MAX_ROW`, " +     // 1
    "`MIN_WAIT`, " +    // 2
    "`NOT_AFTER`, " +   // 3
    "`OR_AFTER`, " +    // 4
    "`OR_AFTER_II` " +  // 5
    "FROM `RULE_LINES` WHERE " +
    "`CLOCK_NAME`='" + RDEscapeString(clock_name) + "'";
  
  QList<RDSchedulerRule> rules;
  RDSqlQuery *q = new RDSqlQuery(sql);
  while (q->next()) {
    RDSchedulerRule rule;
    rule.code = q->value(0).toString();
    rule.max_row = q->value(1).toInt();
    rule.min_wait = q->value(2).toInt();
    rule.not_after = q->value(3).toString();
    rule.or_after = q->value(4).toString();
    rule.or_after_ii = q->value(5).toString();
    rules.append(rule);
  }
  delete q;
  
  d_rule_cache.insert(clock_name, rules);
  
  //fprintf(stderr, "DEBUG: Loaded %d rules for clock '%s'\n",
  //        rules.size(), clock_name.toUtf8().constData());
}


void RDLogGenerationCache::loadAllRulesForService(const QString &service_name)
{
  QTime timer;
  timer.start();
  
  // Load ALL rules for ALL clocks used by this service in ONE query
  QString sql = QString("SELECT ") +
    "`RL`.`CLOCK_NAME`, " + // 0
    "`RL`.`CODE`, " +       // 1
    "`RL`.`MAX_ROW`, " +    // 2
    "`RL`.`MIN_WAIT`, " +   // 3
    "`RL`.`NOT_AFTER`, " +  // 4
    "`RL`.`OR_AFTER`, " +   // 5
    "`RL`.`OR_AFTER_II` " + // 6
    "FROM `RULE_LINES` `RL` " +
    "INNER JOIN `SERVICE_CLOCKS` `SC` ON `RL`.`CLOCK_NAME`=`SC`.`CLOCK_NAME` " +
    "WHERE `SC`.`SERVICE_NAME`='" + RDEscapeString(service_name) + "' " +
    "ORDER BY `RL`.`CLOCK_NAME`";
  
  RDSqlQuery *q = new RDSqlQuery(sql);
  QString current_clock;
  QList<RDSchedulerRule> current_rules;
  int total_rules = 0;
  int total_clocks = 0;
  
  while (q->next()) {
    QString clock_name = q->value(0).toString();
    
    // If we're on a new clock, save the previous clock's rules
    if (clock_name != current_clock && !current_clock.isEmpty()) {
      d_rule_cache.insert(current_clock, current_rules);
      total_clocks++;
      current_rules.clear();
    }
    current_clock = clock_name;
    
    RDSchedulerRule rule;
    rule.code = q->value(1).toString();
    rule.max_row = q->value(2).toInt();
    rule.min_wait = q->value(3).toInt();
    rule.not_after = q->value(4).toString();
    rule.or_after = q->value(5).toString();
    rule.or_after_ii = q->value(6).toString();
    current_rules.append(rule);
    total_rules++;
  }
  delete q;
  
  // Don't forget the last clock
  if (!current_clock.isEmpty()) {
    d_rule_cache.insert(current_clock, current_rules);
    total_clocks++;
  }
  
  //fprintf(stderr, "DEBUG: Loaded %d rules for %d clocks in service '%s' in %d ms\n",
  //        total_rules, total_clocks, service_name.toUtf8().constData(), timer.elapsed());
}


bool RDLogGenerationCache::hasClockRules(const QString &clock_name) const
{
  return d_rule_cache.contains(clock_name);
}


QList<RDSchedulerRule> RDLogGenerationCache::getRulesForClock(const QString &clock_name) const
{
  return d_rule_cache.value(clock_name);
}


int RDLogGenerationCache::getTotalCarts() const
{
  int total = 0;
  for (auto it = d_cart_cache.constBegin(); it != d_cart_cache.constEnd(); ++it) {
    total += it.value().size();
  }
  return total;
}


int RDLogGenerationCache::getTotalRules() const
{
  int total = 0;
  for (auto it = d_rule_cache.constBegin(); it != d_rule_cache.constEnd(); ++it) {
    total += it.value().size();
  }
  return total;
}


void RDLogGenerationCache::loadEventsForService(const QString &service_name)
{
  QTime timer;
  timer.start();
  
  // Get all event names used by clocks in this service
  QString sql = QString("SELECT DISTINCT `CL`.`EVENT_NAME` FROM `CLOCK_LINES` AS `CL` ") +
    "INNER JOIN `SERVICE_CLOCKS` AS `SC` ON `CL`.`CLOCK_NAME`=`SC`.`CLOCK_NAME` " +
    "WHERE `SC`.`SERVICE_NAME`='" + RDEscapeString(service_name) + "'";
  
  RDSqlQuery *q = new RDSqlQuery(sql);
  QStringList event_names;
  while (q->next()) {
    event_names.append(q->value(0).toString());
  }
  delete q;
  
  if (event_names.isEmpty()) {
    return;
  }
  
  // Build IN clause for bulk queries
  QString in_clause = "(";
  for (int i = 0; i < event_names.size(); i++) {
    if (i > 0) in_clause += ",";
    in_clause += "'" + RDEscapeString(event_names[i]) + "'";
  }
  in_clause += ")";
  
  // Load ALL event data in ONE query
  sql = QString("SELECT ") +
    "`NAME`, " +               // 0
    "`PREPOSITION`, " +        // 1
    "`TIME_TYPE`, " +          // 2
    "`GRACE_TIME`, " +         // 3
    "`USE_AUTOFILL`, " +       // 4
    "`USE_TIMESCALE`, " +      // 5
    "`IMPORT_SOURCE`, " +      // 6
    "`START_SLOP`, " +         // 7
    "`END_SLOP`, " +           // 8
    "`FIRST_TRANS_TYPE`, " +   // 9
    "`DEFAULT_TRANS_TYPE`, " + // 10
    "`COLOR`, " +              // 11
    "`AUTOFILL_SLOP`, " +      // 12
    "`NESTED_EVENT`, " +       // 13
    "`SCHED_GROUP`, " +        // 14
    "`ARTIST_SEP`, " +         // 15
    "`TITLE_SEP`, " +          // 16
    "`HAVE_CODE`, " +          // 17
    "`HAVE_CODE2` " +          // 18
    "FROM `EVENTS` WHERE `NAME` IN " + in_clause;
  
  q = new RDSqlQuery(sql);
  while (q->next()) {
    QString event_name = q->value(0).toString();
    RDCachedEvent event;
    event.name = event_name;
    event.preposition = q->value(1).toInt();
    event.time_type = q->value(2).toInt();
    event.grace_time = q->value(3).toInt();
    event.use_autofill = (q->value(4).toString() == "Y");
    event.use_timescale = (q->value(5).toString() == "Y");
    event.import_source = q->value(6).toInt();
    event.start_slop = q->value(7).toInt();
    event.end_slop = q->value(8).toInt();
    event.first_trans_type = q->value(9).toInt();
    event.default_trans_type = q->value(10).toInt();
    event.color = q->value(11).toString();
    event.autofill_slop = q->value(12).toInt();
    event.nested_event = q->value(13).toString();
    event.sched_group = q->value(14).toString();
    event.artist_sep = q->value(15).toInt();
    event.title_sep = q->value(16).toInt();
    event.have_code = q->value(17).toString();
    event.have_code2 = q->value(18).toString();
    d_event_cache.insert(event_name, event);
  }
  delete q;
  
  // Load ALL preimport items in ONE query (TYPE=0)
  sql = QString("SELECT `EVENT_NAME`, `EVENT_TYPE`, `CART_NUMBER`, `TRANS_TYPE`, `MARKER_COMMENT` ") +
    "FROM `EVENT_LINES` WHERE `EVENT_NAME` IN " + in_clause + " " +
    "AND `TYPE`=0 ORDER BY `EVENT_NAME`, `COUNT`";
  q = new RDSqlQuery(sql);
  while (q->next()) {
    QString event_name = q->value(0).toString();
    if (d_event_cache.contains(event_name)) {
      RDCachedImportItem item;
      item.event_type = q->value(1).toInt();
      item.cart_number = q->value(2).toUInt();
      item.trans_type = q->value(3).toInt();
      item.marker_comment = q->value(4).toString();
      d_event_cache[event_name].preimport_list.append(item);
    }
  }
  delete q;
  
  // Load ALL postimport items in ONE query (TYPE=1)
  sql = QString("SELECT `EVENT_NAME`, `EVENT_TYPE`, `CART_NUMBER`, `TRANS_TYPE`, `MARKER_COMMENT` ") +
    "FROM `EVENT_LINES` WHERE `EVENT_NAME` IN " + in_clause + " " +
    "AND `TYPE`=1 ORDER BY `EVENT_NAME`, `COUNT`";
  q = new RDSqlQuery(sql);
  while (q->next()) {
    QString event_name = q->value(0).toString();
    if (d_event_cache.contains(event_name)) {
      RDCachedImportItem item;
      item.event_type = q->value(1).toInt();
      item.cart_number = q->value(2).toUInt();
      item.trans_type = q->value(3).toInt();
      item.marker_comment = q->value(4).toString();
      d_event_cache[event_name].postimport_list.append(item);
    }
  }
  delete q;
  
  //fprintf(stderr, "DEBUG: Loaded %d events for service '%s' in %d ms\n",
  //        d_event_cache.size(), service_name.toUtf8().constData(), timer.elapsed());
}


bool RDLogGenerationCache::hasEvent(const QString &event_name) const
{
  return d_event_cache.contains(event_name);
}


const RDCachedEvent *RDLogGenerationCache::getEvent(const QString &event_name) const
{
  QHash<QString, RDCachedEvent>::const_iterator it = d_event_cache.find(event_name);
  if (it != d_event_cache.end()) {
    return &(*it);
  }
  return nullptr;
}


int RDLogGenerationCache::getTotalEvents() const
{
  return d_event_cache.size();
}


void RDLogGenerationCache::loadClockLinesForService(const QString &service_name)
{
  QTime timer;
  timer.start();
  
  // Get all clocks used by this service and load their lines
  QString sql = QString("SELECT DISTINCT `CLOCK_NAME` FROM `SERVICE_CLOCKS` WHERE ") +
    "`SERVICE_NAME`='" + RDEscapeString(service_name) + "' " +
    "AND `CLOCK_NAME` IS NOT NULL AND `CLOCK_NAME`!=''";
  
  RDSqlQuery *q = new RDSqlQuery(sql);
  QStringList clock_names;
  while (q->next()) {
    clock_names.append(q->value(0).toString());
  }
  delete q;
  
  // Load lines for each clock
  for (int i = 0; i < clock_names.size(); i++) {
    QString clock_name = clock_names[i];
    
    if (d_clock_lines_cache.contains(clock_name)) {
      continue;  // Already loaded
    }
    
    sql = QString("SELECT `EVENT_NAME`, `START_TIME`, `LENGTH` ") +
      "FROM `CLOCK_LINES` WHERE `CLOCK_NAME`='" + RDEscapeString(clock_name) + "' " +
      "ORDER BY `START_TIME`";
    
    q = new RDSqlQuery(sql);
    QList<ClockLineEntry> lines;
    while (q->next()) {
      ClockLineEntry entry;
      entry.event_name = q->value(0).toString();
      entry.start_time = q->value(1).toInt();
      entry.length = q->value(2).toInt();
      lines.append(entry);
    }
    delete q;
    
    d_clock_lines_cache.insert(clock_name, lines);
  }
  
  //fprintf(stderr, "DEBUG: Loaded clock lines for %d clocks in %d ms\n",
  //        d_clock_lines_cache.size(), timer.elapsed());
}


bool RDLogGenerationCache::hasClockLines(const QString &clock_name) const
{
  return d_clock_lines_cache.contains(clock_name);
}


QList<RDLogGenerationCache::ClockLineEntry> RDLogGenerationCache::getClockLines(const QString &clock_name) const
{
  return d_clock_lines_cache.value(clock_name);
}


void RDLogGenerationCache::loadServiceClockGrid(const QString &service_name)
{
  QTime timer;
  timer.start();
  
  d_clock_grid.clear();
  
  // Load all 168 hours (7 days * 24 hours)
  QString sql = QString("SELECT `HOUR`, `CLOCK_NAME` FROM `SERVICE_CLOCKS` WHERE ") +
    "`SERVICE_NAME`='" + RDEscapeString(service_name) + "'";
  
  RDSqlQuery *q = new RDSqlQuery(sql);
  while (q->next()) {
    int hour = q->value(0).toInt();
    QString clock_name = q->value(1).toString();
    if (!clock_name.isEmpty()) {
      d_clock_grid.insert(hour, clock_name);
    }
  }
  delete q;
  
  //fprintf(stderr, "DEBUG: Loaded service clock grid with %d entries in %d ms\n",
  //        d_clock_grid.size(), timer.elapsed());
}


QString RDLogGenerationCache::getClockForHour(int day_of_week, int hour) const
{
  // day_of_week: 1=Monday ... 7=Sunday (Qt standard)
  // Grid index: (day-1)*24 + hour
  int index = (day_of_week - 1) * 24 + hour;
  return d_clock_grid.value(index, QString());
}


void RDLogGenerationCache::addLogLine(const RDPendingLogLine &line)
{
  d_pending_log_lines.append(line);
}


bool RDLogGenerationCache::flushToDatabase()
{
  QTime timer;
  timer.start();
  
  //fprintf(stderr, "DEBUG: Flushing %d log lines and %d stack entries to database...\n",
  //        d_pending_log_lines.size(), d_pending_stack.size());
  
  // Start a transaction for atomicity
  QString sql = "START TRANSACTION";
  if(!RDSqlQuery::apply(sql)) {
    fprintf(stderr,"ERROR: Failed to start transaction when flushing log generation cache.\n");
    return false;
  }
  
  //
  // Batch insert all log lines (up to 100 at a time)
  //
  int batch_size = 100;
  for (int batch_start = 0; batch_start < d_pending_log_lines.size(); batch_start += batch_size) {
    int batch_end = qMin(batch_start + batch_size, d_pending_log_lines.size());
    
    sql = QString("INSERT INTO `LOG_LINES` "
      "(`LOG_NAME`,`LINE_ID`,`COUNT`,`TYPE`,`SOURCE`,`START_TIME`,`GRACE_TIME`,"
      "`CART_NUMBER`,`TIME_TYPE`,`TRANS_TYPE`,`COMMENT`,`EVENT_LENGTH`,"
      "`LINK_EVENT_NAME`,`LINK_START_TIME`,`LINK_LENGTH`,`LINK_ID`,`LINK_START_SLOP`,`LINK_END_SLOP`) VALUES ");
    
    for (int i = batch_start; i < batch_end; i++) {
      const RDPendingLogLine &line = d_pending_log_lines[i];
      
      if (i > batch_start) {
        sql += ",";
      }
      
      sql += QString("('%1',%2,%3,%4,%5,%6,%7,%8,%9,%10,'%11',%12,'%13',%14,%15,%16,%17,%18)")
        .arg(RDEscapeString(d_log_name))
        .arg(line.line_id)
        .arg(line.count)
        .arg(line.type)
        .arg(line.source)
        .arg(line.start_time)
        .arg(line.grace_time)
        .arg(line.cart_number)
        .arg(line.time_type)
        .arg(line.trans_type)
        .arg(RDEscapeString(line.comment))
        .arg(line.event_length)
        .arg(RDEscapeString(line.link_event_name))
        .arg(line.link_start_time)
        .arg(line.link_length)
        .arg(line.link_id)
        .arg(line.link_start_slop)
        .arg(line.link_end_slop);
    }
    
    if(!RDSqlQuery::apply(sql)) {
      fprintf(stderr,"ERROR: Failed to insert log line batch; rolling back.\n");
      RDSqlQuery::apply("ROLLBACK");
      return false;
    }
  }
  
  //
  // Batch insert all stack entries
  //
  if (!d_pending_stack.isEmpty()) {
    // First insert stack lines in batch
    sql = QString("INSERT INTO `STACK_LINES` "
      "(`SERVICE_NAME`,`SCHEDULED_AT`,`SCHED_STACK_ID`,`CART`,`ARTIST`,`TITLE`) VALUES ");
    
    for (int i = 0; i < d_pending_stack.size(); i++) {
      const RDPendingStackEntry &entry = d_pending_stack[i];
      
      if (i > 0) {
        sql += ",";
      }
      
      sql += QString("('%1',NOW(),%2,%3,'%4','%5')")
        .arg(RDEscapeString(d_service_name))
        .arg(entry.sched_stack_id)
        .arg(entry.cart_number)
        .arg(RDEscapeString(entry.artist))
        .arg(RDEscapeString(entry.title));
    }
    
    if(!RDSqlQuery::apply(sql)) {
      fprintf(stderr,"ERROR: Failed to insert stack lines; rolling back.\n");
      RDSqlQuery::apply("ROLLBACK");
      return false;
    }
    
    // Now get the inserted IDs and batch insert sched codes
    // First get the starting ID
    // Map inserted rows to IDs using LAST_INSERT_ID() to avoid races
    sql = "SELECT LAST_INSERT_ID()";
    RDSqlQuery *q = new RDSqlQuery(sql);
    unsigned last_id = 0;
    if(q->next()) {
      last_id = q->value(0).toUInt();
    }
    delete q;

    if(last_id==0) {
      fprintf(stderr,"ERROR: Failed to obtain LAST_INSERT_ID() for stack lines; rolling back.\n");
      RDSqlQuery::apply("ROLLBACK");
      return false;
    }

    unsigned base_id = last_id - d_pending_stack.size() + 1;
    
    // Now batch insert all sched codes
    bool has_codes = false;
    sql = QString("INSERT INTO `STACK_SCHED_CODES` (`STACK_LINES_ID`,`SCHED_CODE`) VALUES ");
    
    for (int i = 0; i < d_pending_stack.size(); i++) {
      const RDPendingStackEntry &entry = d_pending_stack[i];
      unsigned stack_id = base_id + i;
      
      for (int j = 0; j < entry.sched_codes.size(); j++) {
        if (has_codes) {
          sql += ",";
        }
        sql += QString("(%1,'%2')")
          .arg(stack_id)
          .arg(RDEscapeString(entry.sched_codes.at(j)));
        has_codes = true;
      }
    }
    
    if (has_codes) {
      if(!RDSqlQuery::apply(sql)) {
        fprintf(stderr,"ERROR: Failed to insert stack sched codes; rolling back.\n");
        RDSqlQuery::apply("ROLLBACK");
        return false;
      }
    }
  }
  
  // Commit the transaction
  sql = "COMMIT";
  if(!RDSqlQuery::apply(sql)) {
    fprintf(stderr,"ERROR: Failed to commit transaction when flushing log generation cache; rolling back.\n");
    RDSqlQuery::apply("ROLLBACK");
    return false;
  }
  
  //fprintf(stderr, "DEBUG: Flush completed in %d ms\n", timer.elapsed());
  
  // Clear the pending buffers
  d_pending_log_lines.clear();
  d_pending_stack.clear();
  
  return true;
}


//
// Validation cache methods
//
void RDLogGenerationCache::loadValidationData(const QList<unsigned> &cart_numbers)
{
  if (cart_numbers.isEmpty()) {
    d_validation_loaded = true;
    return;
  }
  
  QTime timer;
  timer.start();
  
  // Clear existing validation data
  d_cart_validation_cache.clear();
  d_cut_validity_cache.clear();
  
  // Build IN clause for cart numbers
  QString in_clause = "(";
  QSet<unsigned> unique_carts;
  for (int i = 0; i < cart_numbers.size(); i++) {
    if (cart_numbers[i] > 0) {
      unique_carts.insert(cart_numbers[i]);
    }
  }
  
  if (unique_carts.isEmpty()) {
    d_validation_loaded = true;
    return;
  }
  
  bool first = true;
  for (QSet<unsigned>::const_iterator it = unique_carts.constBegin(); 
       it != unique_carts.constEnd(); ++it) {
    if (!first) in_clause += ",";
    in_clause += QString::number(*it);
    first = false;
  }
  in_clause += ")";
  
  // Load cart type and title for all carts in ONE query
  QString sql = QString("SELECT `NUMBER`, `TYPE`, `TITLE` FROM `CART` "
                        "WHERE `NUMBER` IN ") + in_clause;
  RDSqlQuery *q = new RDSqlQuery(sql);
  while (q->next()) {
    RDCartValidation cv;
    cv.cart_type = q->value(1).toInt();
    cv.title = q->value(2).toString();
    d_cart_validation_cache.insert(q->value(0).toUInt(), cv);
  }
  delete q;
  
  //fprintf(stderr, "DEBUG: Loaded validation data for %d carts in %d ms (query 1)\n",
  //        d_cart_validation_cache.size(), timer.elapsed());
  
  // Load cut validity windows for all audio carts in ONE query
  // Only need cuts for Audio type carts (type=1)
  timer.restart();
  sql = QString("SELECT `CART_NUMBER`, `START_DATETIME`, `END_DATETIME`, "
                "`START_DAYPART`, `END_DAYPART`, `LENGTH`, "
                "`SUN`, `MON`, `TUE`, `WED`, `THU`, `FRI`, `SAT` "
                "FROM `CUTS` WHERE `CART_NUMBER` IN ") + in_clause;
  q = new RDSqlQuery(sql);
  while (q->next()) {
    unsigned cart_num = q->value(0).toUInt();
    RDCutValidity cv;
    
    // Parse datetime fields (can be null)
    if (!q->value(1).isNull()) {
      cv.start_datetime = q->value(1).toDateTime();
    }
    if (!q->value(2).isNull()) {
      cv.end_datetime = q->value(2).toDateTime();
    }
    
    // Parse daypart fields (can be null)
    if (!q->value(3).isNull()) {
      cv.start_daypart = q->value(3).toTime();
    }
    if (!q->value(4).isNull()) {
      cv.end_daypart = q->value(4).toTime();
    }
    
    cv.length = q->value(5).toInt();
    
    // Day of week flags (Qt uses 0=Sun through 6=Sat for our purposes,
    // but database has separate columns)
    cv.dow[0] = (q->value(6).toString() == "Y");  // SUN
    cv.dow[1] = (q->value(7).toString() == "Y");  // MON
    cv.dow[2] = (q->value(8).toString() == "Y");  // TUE
    cv.dow[3] = (q->value(9).toString() == "Y");  // WED
    cv.dow[4] = (q->value(10).toString() == "Y"); // THU
    cv.dow[5] = (q->value(11).toString() == "Y"); // FRI
    cv.dow[6] = (q->value(12).toString() == "Y"); // SAT
    
    d_cut_validity_cache[cart_num].append(cv);
  }
  delete q;
  
  //fprintf(stderr, "DEBUG: Loaded cut validity for %d carts in %d ms (query 2)\n",
  //        d_cut_validity_cache.size(), timer.elapsed());
  
  d_validation_loaded = true;
}


bool RDLogGenerationCache::isCartValid(unsigned cart_number) const
{
  return d_cart_validation_cache.contains(cart_number);
}


int RDLogGenerationCache::getCartType(unsigned cart_number) const
{
  QHash<unsigned, RDCartValidation>::const_iterator it = 
    d_cart_validation_cache.find(cart_number);
  if (it != d_cart_validation_cache.end()) {
    return it->cart_type;
  }
  return -1;  // Not found
}


QString RDLogGenerationCache::getCartTitle(unsigned cart_number) const
{
  QHash<unsigned, RDCartValidation>::const_iterator it = 
    d_cart_validation_cache.find(cart_number);
  if (it != d_cart_validation_cache.end()) {
    return it->title;
  }
  return QString();
}


bool RDLogGenerationCache::hasCutValidForDateTime(unsigned cart_number, 
                                                   const QDate &date,
                                                   const QTime &time) const
{
  QHash<unsigned, QList<RDCutValidity>>::const_iterator it = 
    d_cut_validity_cache.find(cart_number);
  
  if (it == d_cut_validity_cache.end()) {
    return false;  // No cuts for this cart
  }
  
  // Convert Qt dayOfWeek (1=Mon...7=Sun) to our array index (0=Sun...6=Sat)
  int dow_index = date.dayOfWeek() % 7;  // 7 (Sun) becomes 0
  
  const QList<RDCutValidity> &cuts = it.value();
  for (int i = 0; i < cuts.size(); i++) {
    const RDCutValidity &cut = cuts[i];
    
    // Check length > 0
    if (cut.length <= 0) {
      continue;
    }
    
    // Check day of week
    if (!cut.dow[dow_index]) {
      continue;
    }
    
    // Build datetime for comparison
    QDateTime check_datetime;
    if (time.isValid() && !time.isNull()) {
      check_datetime = QDateTime(date, time);
    }
    
    // Check START_DATETIME
    if (cut.start_datetime.isValid() && !cut.start_datetime.isNull()) {
      if (check_datetime.isValid()) {
        if (check_datetime < cut.start_datetime) {
          continue;
        }
      } else {
        // No time specified, check against end of day
        QDateTime end_of_day(date, QTime(23, 59, 59));
        if (end_of_day < cut.start_datetime) {
          continue;
        }
      }
    }
    
    // Check END_DATETIME
    if (cut.end_datetime.isValid() && !cut.end_datetime.isNull()) {
      if (check_datetime.isValid()) {
        if (check_datetime > cut.end_datetime) {
          continue;
        }
      } else {
        // No time specified, check against start of day
        QDateTime start_of_day(date, QTime(0, 0, 0));
        if (start_of_day > cut.end_datetime) {
          continue;
        }
      }
    }
    
    // Check daypart (only if time is specified)
    if (time.isValid() && !time.isNull()) {
      if (cut.start_daypart.isValid() && !cut.start_daypart.isNull()) {
        if (time < cut.start_daypart) {
          continue;
        }
      }
      if (cut.end_daypart.isValid() && !cut.end_daypart.isNull()) {
        if (time > cut.end_daypart) {
          continue;
        }
      }
    }
    
    // This cut is valid!
    return true;
  }
  
  return false;  // No valid cut found
}
