// rdsvc_cache.cpp
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

#include <algorithm>

#include "rdconf.h"
#include "rddb.h"
#include "rdescape_string.h"
#include "rdsvc_cache.h"

RDSvcCache::RDSvcCache()
{
  clear();
}


RDSvcCache::~RDSvcCache()
{
  clear();
}


void RDSvcCache::clear()
{
  d_name="";
  d_description="";
  d_name_template="";
  d_description_template="";
  d_program_code="";
  d_track_group="";
  d_autospot_group="";
  d_bypass_mode=false;
  d_chain_log=false;
  d_auto_refresh=false;
  d_sub_event_inheritance=0;
  d_default_log_shelflife=0;
  d_log_shelflife_origin=0;
  d_elr_shelflife=0;
  
  d_mus_path="";
  d_mus_preimport_cmd="";
  d_mus_import_template="";
  d_mus_break_string="";
  d_mus_track_string="";
  d_mus_label_cart="";
  d_mus_track_cart="";
  d_include_mus_import_markers=false;
  
  d_tfc_path="";
  d_tfc_preimport_cmd="";
  d_tfc_import_template="";
  d_tfc_break_string="";
  d_tfc_track_string="";
  d_tfc_label_cart="";
  d_tfc_track_cart="";
  d_include_tfc_import_markers=false;
  
  d_autofill_carts.clear();
}


bool RDSvcCache::loadService(const QString &svc_name)
{
  clear();
  
  // Single query to load all service config fields
  QString sql=QString("select ")+
    "`NAME`,"+                    // 00
    "`DESCRIPTION`,"+             // 01
    "`NAME_TEMPLATE`,"+           // 02
    "`DESCRIPTION_TEMPLATE`,"+    // 03
    "`PROGRAM_CODE`,"+            // 04
    "`TRACK_GROUP`,"+             // 05
    "`AUTOSPOT_GROUP`,"+          // 06
    "`BYPASS_MODE`,"+             // 07
    "`CHAIN_LOG`,"+               // 08
    "`AUTO_REFRESH`,"+            // 09
    "`SUB_EVENT_INHERITANCE`,"+   // 10
    "`DEFAULT_LOG_SHELFLIFE`,"+   // 11
    "`LOG_SHELFLIFE_ORIGIN`,"+    // 12
    "`ELR_SHELFLIFE`,"+           // 13
    "`MUS_PATH`,"+                // 14
    "`MUS_PREIMPORT_CMD`,"+       // 15
    "`MUS_IMPORT_TEMPLATE`,"+     // 16
    "`MUS_BREAK_STRING`,"+        // 17
    "`MUS_TRACK_STRING`,"+        // 18
    "`MUS_LABEL_CART`,"+          // 19
    "`MUS_TRACK_CART`,"+          // 20
    "`INCLUDE_MUS_IMPORT_MARKERS`,"+  // 21
    "`TFC_PATH`,"+                // 22
    "`TFC_PREIMPORT_CMD`,"+       // 23
    "`TFC_IMPORT_TEMPLATE`,"+     // 24
    "`TFC_BREAK_STRING`,"+        // 25
    "`TFC_TRACK_STRING`,"+        // 26
    "`TFC_LABEL_CART`,"+          // 27
    "`TFC_TRACK_CART`,"+          // 28
    "`INCLUDE_TFC_IMPORT_MARKERS` "+  // 29
    "from `SERVICES` where "+
    "`NAME`='"+RDEscapeString(svc_name)+"'";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(!q->first()) {
    delete q;
    return false;
  }
  
  d_name=q->value(0).toString();
  d_description=q->value(1).toString();
  d_name_template=q->value(2).toString();
  d_description_template=q->value(3).toString();
  d_program_code=q->value(4).toString();
  d_track_group=q->value(5).toString();
  d_autospot_group=q->value(6).toString();
  d_bypass_mode=RDBool(q->value(7).toString());
  d_chain_log=RDBool(q->value(8).toString());
  d_auto_refresh=RDBool(q->value(9).toString());
  d_sub_event_inheritance=q->value(10).toInt();
  d_default_log_shelflife=q->value(11).toInt();
  d_log_shelflife_origin=q->value(12).toInt();
  d_elr_shelflife=q->value(13).toInt();
  
  d_mus_path=q->value(14).toString();
  d_mus_preimport_cmd=q->value(15).toString();
  d_mus_import_template=q->value(16).toString();
  d_mus_break_string=q->value(17).toString();
  d_mus_track_string=q->value(18).toString();
  d_mus_label_cart=q->value(19).toString();
  d_mus_track_cart=q->value(20).toString();
  d_include_mus_import_markers=RDBool(q->value(21).toString());
  
  d_tfc_path=q->value(22).toString();
  d_tfc_preimport_cmd=q->value(23).toString();
  d_tfc_import_template=q->value(24).toString();
  d_tfc_break_string=q->value(25).toString();
  d_tfc_track_string=q->value(26).toString();
  d_tfc_label_cart=q->value(27).toString();
  d_tfc_track_cart=q->value(28).toString();
  d_include_tfc_import_markers=RDBool(q->value(29).toString());
  
  delete q;
  return true;
}


bool RDSvcCache::loadAutofills(const QString &svc_name)
{
  d_autofill_carts.clear();
  
  // Load autofill carts with their lengths from CART table
  QString sql=QString("select ")+
    "`AUTOFILLS`.`CART_NUMBER`,"+  // 00
    "`CART`.`FORCED_LENGTH` "+     // 01
    "from `AUTOFILLS` "+
    "left join `CART` on `AUTOFILLS`.`CART_NUMBER`=`CART`.`NUMBER` "+
    "where `AUTOFILLS`.`SERVICE`='"+RDEscapeString(svc_name)+"' "+
    "order by `CART`.`FORCED_LENGTH` desc";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    AutofillCart af;
    af.cart_number=q->value(0).toUInt();
    af.forced_length=q->value(1).toInt();
    d_autofill_carts.push_back(af);
  }
  delete q;
  
  return true;
}


QVector<RDSvcCache::AutofillCart> RDSvcCache::getAutofillCarts(int max_length_ms) const
{
  QVector<AutofillCart> result;
  
  // Autofills are already sorted by length desc
  for(int i=0; i<d_autofill_carts.size(); i++) {
    if(d_autofill_carts[i].forced_length <= max_length_ms) {
      result.push_back(d_autofill_carts[i]);
    }
  }
  
  return result;
}
