// rdsvc_cache.h
//
// Service-level cache for log generation
//
// Caches service configuration data to eliminate repeated database queries
// during log generation. Loads all SERVICES table fields in a single query.
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

#ifndef RDSVC_CACHE_H
#define RDSVC_CACHE_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QDate>

class RDSvcCache
{
 public:
  RDSvcCache();
  ~RDSvcCache();
  
  // Load all service data in one query
  bool loadService(const QString &svc_name);
  void clear();
  
  //=========================================================================
  // SERVICE CONFIGURATION DATA
  //=========================================================================
  QString name() const { return d_name; }
  QString description() const { return d_description; }
  QString nameTemplate() const { return d_name_template; }
  QString descriptionTemplate() const { return d_description_template; }
  QString programCode() const { return d_program_code; }
  QString trackGroup() const { return d_track_group; }
  QString autospotGroup() const { return d_autospot_group; }
  bool bypassMode() const { return d_bypass_mode; }
  bool chainLog() const { return d_chain_log; }
  bool autoRefresh() const { return d_auto_refresh; }
  int subEventInheritance() const { return d_sub_event_inheritance; }
  int defaultLogShelflife() const { return d_default_log_shelflife; }
  int logShelflifeOrigin() const { return d_log_shelflife_origin; }
  int elrShelflife() const { return d_elr_shelflife; }
  
  // Music import settings
  QString musPath() const { return d_mus_path; }
  QString musPreimportCmd() const { return d_mus_preimport_cmd; }
  QString musImportTemplate() const { return d_mus_import_template; }
  QString musBreakString() const { return d_mus_break_string; }
  QString musTrackString() const { return d_mus_track_string; }
  QString musLabelCart() const { return d_mus_label_cart; }
  QString musTrackCart() const { return d_mus_track_cart; }
  bool includeMusImportMarkers() const { return d_include_mus_import_markers; }
  
  // Traffic import settings  
  QString tfcPath() const { return d_tfc_path; }
  QString tfcPreimportCmd() const { return d_tfc_preimport_cmd; }
  QString tfcImportTemplate() const { return d_tfc_import_template; }
  QString tfcBreakString() const { return d_tfc_break_string; }
  QString tfcTrackString() const { return d_tfc_track_string; }
  QString tfcLabelCart() const { return d_tfc_label_cart; }
  QString tfcTrackCart() const { return d_tfc_track_cart; }
  bool includeTfcImportMarkers() const { return d_include_tfc_import_markers; }
  
  //=========================================================================
  // AUTOFILL DATA
  //=========================================================================
  struct AutofillCart {
    unsigned cart_number;
    int forced_length;
  };
  
  bool loadAutofills(const QString &svc_name);
  QVector<AutofillCart> getAutofillCarts(int max_length_ms) const;
  int autofillCount() const { return d_autofill_carts.size(); }

 private:
  // Service config
  QString d_name;
  QString d_description;
  QString d_name_template;
  QString d_description_template;
  QString d_program_code;
  QString d_track_group;
  QString d_autospot_group;
  bool d_bypass_mode;
  bool d_chain_log;
  bool d_auto_refresh;
  int d_sub_event_inheritance;
  int d_default_log_shelflife;
  int d_log_shelflife_origin;
  int d_elr_shelflife;
  
  // Music import
  QString d_mus_path;
  QString d_mus_preimport_cmd;
  QString d_mus_import_template;
  QString d_mus_break_string;
  QString d_mus_track_string;
  QString d_mus_label_cart;
  QString d_mus_track_cart;
  bool d_include_mus_import_markers;
  
  // Traffic import
  QString d_tfc_path;
  QString d_tfc_preimport_cmd;
  QString d_tfc_import_template;
  QString d_tfc_break_string;
  QString d_tfc_track_string;
  QString d_tfc_label_cart;
  QString d_tfc_track_cart;
  bool d_include_tfc_import_markers;
  
  // Autofills (sorted by length desc)
  QVector<AutofillCart> d_autofill_carts;
};

#endif  // RDSVC_CACHE_H
