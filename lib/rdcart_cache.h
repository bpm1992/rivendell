// rdcart_cache.h
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

#ifndef RDCART_CACHE_H
#define RDCART_CACHE_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <QDate>
#include <QColor>

class RDCartCache
{
 public:
  RDCartCache();
  ~RDCartCache();
  
  void clear();
  
  //=========================================================================
  // COMPREHENSIVE CART DATA STRUCTURE
  //=========================================================================
  struct CartData {
    unsigned number;
    int type;                    // RDCart::Type (Audio=1, Macro=2)
    QString group_name;
    QString title;
    QString artist;
    QString album;
    QDate year;
    QString label;
    QString client;
    QString agency;
    QString publisher;
    QString composer;
    QString user_defined;
    QString song_id;
    QString notes;
    int usage_code;
    int forced_length;
    int average_length;
    int average_segue_length;
    int cut_quantity;
    int last_cut_played;
    int play_order;              // RDCart::PlayOrder
    bool enforce_length;
    bool preserve_pitch;
    bool use_weighting;
    int validity;                // RDCart::Validity
    QStringList sched_codes;     // Pre-loaded scheduler codes
    QColor group_color;          // From GROUPS table
    
    void clear();
  };
  
  //=========================================================================
  // BATCH LOADING METHODS
  //=========================================================================
  
  // Load all carts in specified groups (for scheduler)
  // This is the BIG win - replaces 48+ queries with 1
  int loadCartsForGroups(const QStringList &group_names);
  
  // Load specific cart numbers (for pre/post import lists, autofills)
  int loadCarts(const QVector<unsigned> &cart_numbers);
  
  // Load autofill carts for a service (includes forced_length)
  int loadAutofillCarts(const QString &svc_name);
  
  //=========================================================================
  // CART RETRIEVAL
  //=========================================================================
  
  // Get single cart by number
  bool hasCart(unsigned cart_number) const;
  const CartData* getCart(unsigned cart_number) const;
  
  // Convenience accessors (return defaults if cart not found)
  int getCartLength(unsigned cart_number, int default_length=0) const;
  QString getCartTitle(unsigned cart_number) const;
  QString getCartArtist(unsigned cart_number) const;
  QString getCartGroupName(unsigned cart_number) const;
  
  //=========================================================================
  // SCHEDULER-SPECIFIC METHODS
  //=========================================================================
  
  // Get all carts in a group (for RDSchedCartList building)
  QVector<const CartData*> getCartsInGroup(const QString &group_name) const;
  
  // Get carts in group filtered by required sched codes
  QVector<const CartData*> getCartsInGroup(const QString &group_name,
                                           const QString &have_code,
                                           const QString &have_code2=QString()) const;
  
  // Check if cart has specific scheduler code
  bool cartHasSchedCode(unsigned cart_number, const QString &code) const;
  
  //=========================================================================
  // AUTOFILL-SPECIFIC METHODS
  //=========================================================================
  
  struct AutofillCart {
    unsigned cart_number;
    int forced_length;
  };
  
  // Get autofill carts that fit within time limit (sorted by length desc)
  QVector<AutofillCart> getAutofillCarts(int max_length_ms) const;
  
  //=========================================================================
  // STATISTICS
  //=========================================================================
  int cartCount() const { return d_carts.size(); }
  int groupCount() const { return d_carts_by_group.size(); }

 private:
  // Primary storage: cart_number -> CartData
  QMap<unsigned, CartData> d_carts;
  
  // Index by group for fast scheduler lookups
  QMap<QString, QVector<unsigned> > d_carts_by_group;  // group_name -> cart_numbers
  
  // Autofill carts (sorted by forced_length desc)
  QVector<AutofillCart> d_autofill_carts;
  
  // Helper to load sched codes for a set of carts
  void loadSchedCodes(const QVector<unsigned> &cart_numbers);
  
  // Helper to load group colors
  void loadGroupColors(const QStringList &group_names);
  
  // Group colors cache
  QMap<QString, QColor> d_group_colors;
};

#endif  // RDCART_CACHE_H
