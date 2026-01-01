// rdcart_cache.cpp
//
// Comprehensive cart data cache for log generation
//
// Caches ALL cart data needed during log generation. This is critical because
// the music scheduler queries the CART table repeatedly for every music event.
// By batch-loading all carts in scheduler groups once at startup, we replace
// 48+ large queries with 2 queries.
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

#include "rdcart_cache.h"
#include "rdconf.h"
#include "rddb.h"
#include "rdescape_string.h"

void RDCartCache::CartData::clear()
{
  number=0;
  type=1;  // Audio
  group_name="";
  title="";
  artist="";
  album="";
  year=QDate();
  label="";
  client="";
  agency="";
  publisher="";
  composer="";
  user_defined="";
  song_id="";
  notes="";
  usage_code=0;
  forced_length=0;
  average_length=0;
  average_segue_length=0;
  cut_quantity=0;
  last_cut_played=0;
  play_order=0;
  enforce_length=false;
  preserve_pitch=false;
  use_weighting=true;
  validity=0;
  sched_codes.clear();
  group_color=QColor();
}


RDCartCache::RDCartCache()
{
  clear();
}


RDCartCache::~RDCartCache()
{
  clear();
}


void RDCartCache::clear()
{
  d_carts.clear();
  d_carts_by_group.clear();
  d_autofill_carts.clear();
  d_group_colors.clear();
}


int RDCartCache::loadCartsForGroups(const QStringList &group_names)
{
  if(group_names.isEmpty()) {
    return 0;
  }
  
  // Build IN clause for groups
  QString in_clause;
  for(int i=0; i<group_names.size(); i++) {
    if(i>0) {
      in_clause+=",";
    }
    in_clause+="'"+RDEscapeString(group_names[i])+"'";
  }
  
  // Load group colors first
  loadGroupColors(group_names);
  
  // Single batch query to load ALL carts in specified groups
  // This replaces 48+ queries (one per music event) with 1 query
  QString sql=QString("select ")+
    "`NUMBER`,"+             // 00
    "`TYPE`,"+               // 01
    "`GROUP_NAME`,"+         // 02
    "`TITLE`,"+              // 03
    "`ARTIST`,"+             // 04
    "`ALBUM`,"+              // 05
    "`YEAR`,"+               // 06
    "`LABEL`,"+              // 07
    "`CLIENT`,"+             // 08
    "`AGENCY`,"+             // 09
    "`PUBLISHER`,"+          // 10
    "`COMPOSER`,"+           // 11
    "`USER_DEFINED`,"+       // 12
    "`SONG_ID`,"+            // 13
    "`NOTES`,"+              // 14
    "`USAGE_CODE`,"+         // 15
    "`FORCED_LENGTH`,"+      // 16
    "`AVERAGE_LENGTH`,"+     // 17
    "`AVERAGE_SEGUE_LENGTH`,"+  // 18
    "`CUT_QUANTITY`,"+       // 19
    "`LAST_CUT_PLAYED`,"+    // 20
    "`PLAY_ORDER`,"+         // 21
    "`ENFORCE_LENGTH`,"+     // 22
    "`PRESERVE_PITCH`,"+     // 23
    "`USE_WEIGHTING`,"+      // 24
    "`VALIDITY` "+           // 25
    "from `CART` where "+
    "`GROUP_NAME` in ("+in_clause+") "+
    "order by `NUMBER`";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  QVector<unsigned> cart_numbers;
  int count=0;
  
  while(q->next()) {
    CartData cart;
    cart.number=q->value(0).toUInt();
    cart.type=q->value(1).toInt();
    cart.group_name=q->value(2).toString();
    cart.title=q->value(3).toString();
    cart.artist=q->value(4).toString();
    cart.album=q->value(5).toString();
    cart.year=q->value(6).toDate();
    cart.label=q->value(7).toString();
    cart.client=q->value(8).toString();
    cart.agency=q->value(9).toString();
    cart.publisher=q->value(10).toString();
    cart.composer=q->value(11).toString();
    cart.user_defined=q->value(12).toString();
    cart.song_id=q->value(13).toString();
    cart.notes=q->value(14).toString();
    cart.usage_code=q->value(15).toInt();
    cart.forced_length=q->value(16).toInt();
    cart.average_length=q->value(17).toInt();
    cart.average_segue_length=q->value(18).toInt();
    cart.cut_quantity=q->value(19).toInt();
    cart.last_cut_played=q->value(20).toInt();
    cart.play_order=q->value(21).toInt();
    cart.enforce_length=RDBool(q->value(22).toString());
    cart.preserve_pitch=RDBool(q->value(23).toString());
    cart.use_weighting=RDBool(q->value(24).toString());
    cart.validity=q->value(25).toInt();
    
    // Apply group color
    if(d_group_colors.contains(cart.group_name)) {
      cart.group_color=d_group_colors[cart.group_name];
    }
    
    // Store cart and index by group
    d_carts[cart.number]=cart;
    d_carts_by_group[cart.group_name].push_back(cart.number);
    cart_numbers.push_back(cart.number);
    count++;
  }
  delete q;
  
  // Load scheduler codes for all carts
  if(!cart_numbers.isEmpty()) {
    loadSchedCodes(cart_numbers);
  }
  
  return count;
}


int RDCartCache::loadCarts(const QVector<unsigned> &cart_numbers)
{
  if(cart_numbers.isEmpty()) {
    return 0;
  }
  
  // Build IN clause
  QString in_clause;
  for(int i=0; i<cart_numbers.size(); i++) {
    if(i>0) {
      in_clause+=",";
    }
    in_clause+=QString::number(cart_numbers[i]);
  }
  
  // Load carts
  QString sql=QString("select ")+
    "`NUMBER`,"+             // 00
    "`TYPE`,"+               // 01
    "`GROUP_NAME`,"+         // 02
    "`TITLE`,"+              // 03
    "`ARTIST`,"+             // 04
    "`ALBUM`,"+              // 05
    "`YEAR`,"+               // 06
    "`LABEL`,"+              // 07
    "`CLIENT`,"+             // 08
    "`AGENCY`,"+             // 09
    "`PUBLISHER`,"+          // 10
    "`COMPOSER`,"+           // 11
    "`USER_DEFINED`,"+       // 12
    "`SONG_ID`,"+            // 13
    "`NOTES`,"+              // 14
    "`USAGE_CODE`,"+         // 15
    "`FORCED_LENGTH`,"+      // 16
    "`AVERAGE_LENGTH`,"+     // 17
    "`AVERAGE_SEGUE_LENGTH`,"+  // 18
    "`CUT_QUANTITY`,"+       // 19
    "`LAST_CUT_PLAYED`,"+    // 20
    "`PLAY_ORDER`,"+         // 21
    "`ENFORCE_LENGTH`,"+     // 22
    "`PRESERVE_PITCH`,"+     // 23
    "`USE_WEIGHTING`,"+      // 24
    "`VALIDITY` "+           // 25
    "from `CART` where "+
    "`NUMBER` in ("+in_clause+")";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  QStringList groups_to_load;
  int count=0;
  
  while(q->next()) {
    CartData cart;
    cart.number=q->value(0).toUInt();
    cart.type=q->value(1).toInt();
    cart.group_name=q->value(2).toString();
    cart.title=q->value(3).toString();
    cart.artist=q->value(4).toString();
    cart.album=q->value(5).toString();
    cart.year=q->value(6).toDate();
    cart.label=q->value(7).toString();
    cart.client=q->value(8).toString();
    cart.agency=q->value(9).toString();
    cart.publisher=q->value(10).toString();
    cart.composer=q->value(11).toString();
    cart.user_defined=q->value(12).toString();
    cart.song_id=q->value(13).toString();
    cart.notes=q->value(14).toString();
    cart.usage_code=q->value(15).toInt();
    cart.forced_length=q->value(16).toInt();
    cart.average_length=q->value(17).toInt();
    cart.average_segue_length=q->value(18).toInt();
    cart.cut_quantity=q->value(19).toInt();
    cart.last_cut_played=q->value(20).toInt();
    cart.play_order=q->value(21).toInt();
    cart.enforce_length=RDBool(q->value(22).toString());
    cart.preserve_pitch=RDBool(q->value(23).toString());
    cart.use_weighting=RDBool(q->value(24).toString());
    cart.validity=q->value(25).toInt();
    
    // Track groups for color loading
    if(!groups_to_load.contains(cart.group_name)) {
      groups_to_load.push_back(cart.group_name);
    }
    
    d_carts[cart.number]=cart;
    d_carts_by_group[cart.group_name].push_back(cart.number);
    count++;
  }
  delete q;
  
  // Load group colors
  if(!groups_to_load.isEmpty()) {
    loadGroupColors(groups_to_load);
    // Apply colors to loaded carts
    for(auto it=d_carts.begin(); it!=d_carts.end(); ++it) {
      if(d_group_colors.contains(it.value().group_name)) {
        it.value().group_color=d_group_colors[it.value().group_name];
      }
    }
  }
  
  // Load scheduler codes
  loadSchedCodes(cart_numbers);
  
  return count;
}


int RDCartCache::loadAutofillCarts(const QString &svc_name)
{
  d_autofill_carts.clear();
  
  // Load autofill carts with their lengths
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
  
  return d_autofill_carts.size();
}


void RDCartCache::loadSchedCodes(const QVector<unsigned> &cart_numbers)
{
  if(cart_numbers.isEmpty()) {
    return;
  }
  
  // Build IN clause
  QString in_clause;
  for(int i=0; i<cart_numbers.size(); i++) {
    if(i>0) {
      in_clause+=",";
    }
    in_clause+=QString::number(cart_numbers[i]);
  }
  
  // Batch load all sched codes for all carts
  QString sql=QString("select ")+
    "`CART_NUMBER`,"+  // 00
    "`SCHED_CODE` "+   // 01
    "from `CART_SCHED_CODES` where "+
    "`CART_NUMBER` in ("+in_clause+") "+
    "order by `CART_NUMBER`";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    unsigned cart_num=q->value(0).toUInt();
    QString code=q->value(1).toString();
    if(d_carts.contains(cart_num)) {
      d_carts[cart_num].sched_codes.push_back(code);
    }
  }
  delete q;
}


void RDCartCache::loadGroupColors(const QStringList &group_names)
{
  if(group_names.isEmpty()) {
    return;
  }
  
  // Build IN clause
  QString in_clause;
  for(int i=0; i<group_names.size(); i++) {
    if(i>0) {
      in_clause+=",";
    }
    in_clause+="'"+RDEscapeString(group_names[i])+"'";
  }
  
  QString sql=QString("select ")+
    "`NAME`,"+   // 00
    "`COLOR` "+  // 01
    "from `GROUPS` where "+
    "`NAME` in ("+in_clause+")";
  
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    QString name=q->value(0).toString();
    if(!q->value(1).isNull()) {
      d_group_colors[name]=QColor(q->value(1).toString());
    }
  }
  delete q;
}


bool RDCartCache::hasCart(unsigned cart_number) const
{
  return d_carts.contains(cart_number);
}


const RDCartCache::CartData* RDCartCache::getCart(unsigned cart_number) const
{
  auto it=d_carts.constFind(cart_number);
  if(it!=d_carts.constEnd()) {
    return &it.value();
  }
  return nullptr;
}


int RDCartCache::getCartLength(unsigned cart_number, int default_length) const
{
  auto it=d_carts.constFind(cart_number);
  if(it!=d_carts.constEnd()) {
    return it.value().forced_length;
  }
  return default_length;
}


QString RDCartCache::getCartTitle(unsigned cart_number) const
{
  auto it=d_carts.constFind(cart_number);
  if(it!=d_carts.constEnd()) {
    return it.value().title;
  }
  return QString();
}


QString RDCartCache::getCartArtist(unsigned cart_number) const
{
  auto it=d_carts.constFind(cart_number);
  if(it!=d_carts.constEnd()) {
    return it.value().artist;
  }
  return QString();
}


QString RDCartCache::getCartGroupName(unsigned cart_number) const
{
  auto it=d_carts.constFind(cart_number);
  if(it!=d_carts.constEnd()) {
    return it.value().group_name;
  }
  return QString();
}


QVector<const RDCartCache::CartData*> RDCartCache::getCartsInGroup(const QString &group_name) const
{
  QVector<const CartData*> result;
  
  auto it=d_carts_by_group.constFind(group_name);
  if(it!=d_carts_by_group.constEnd()) {
    const QVector<unsigned> &cart_nums=it.value();
    for(int i=0; i<cart_nums.size(); i++) {
      const CartData *cart=getCart(cart_nums[i]);
      if(cart) {
        result.push_back(cart);
      }
    }
  }
  
  return result;
}


QVector<const RDCartCache::CartData*> RDCartCache::getCartsInGroup(const QString &group_name,
                                                                   const QString &have_code,
                                                                   const QString &have_code2) const
{
  QVector<const CartData*> result;
  
  auto it=d_carts_by_group.constFind(group_name);
  if(it!=d_carts_by_group.constEnd()) {
    const QVector<unsigned> &cart_nums=it.value();
    for(int i=0; i<cart_nums.size(); i++) {
      const CartData *cart=getCart(cart_nums[i]);
      if(cart) {
        // Filter by have_code if specified
        bool matches=true;
        if(!have_code.isEmpty()) {
          if(!cart->sched_codes.contains(have_code)) {
            matches=false;
          }
        }
        if(matches && !have_code2.isEmpty()) {
          if(!cart->sched_codes.contains(have_code2)) {
            matches=false;
          }
        }
        if(matches) {
          result.push_back(cart);
        }
      }
    }
  }
  
  return result;
}


bool RDCartCache::cartHasSchedCode(unsigned cart_number, const QString &code) const
{
  auto it=d_carts.constFind(cart_number);
  if(it!=d_carts.constEnd()) {
    return it.value().sched_codes.contains(code);
  }
  return false;
}


QVector<RDCartCache::AutofillCart> RDCartCache::getAutofillCarts(int max_length_ms) const
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
