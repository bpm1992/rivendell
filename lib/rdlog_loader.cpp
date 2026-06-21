// rdlog_loader.cpp
//
// Log Loading Logic for RDLogPlay
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

#include <syslog.h>

#include "rdlog_loader.h"
#include "rdlog_line.h"

RDLogLoader::RDLogLoader()
{
  loader_cut_cache=NULL;
  last_line_count=0;
  last_cart_count=0;
}


RDLogLoader::~RDLogLoader()
{
  delete loader_cut_cache;
  loader_cut_cache=NULL;
}


int RDLogLoader::loadLog(RDLogModel *log_model,
                         bool enable_timescaling)
{
  if(log_model==NULL) {
    last_error="NULL log model provided";
    return -1;
  }

  QString log_name=log_model->logName();
  if(log_name.isEmpty()) {
    last_error="Log name not set";
    return -1;
  }

  last_error.clear();
  last_line_count=0;
  last_cart_count=0;
  delete loader_cut_cache;
  loader_cut_cache=NULL;

  //
  // STEP 1: Load log lines from database
  //
  int line_count=log_model->load();

  if(line_count<=0) {
    last_error=QString("Failed to load log '%1' (not found or empty)").
      arg(log_name);
    return -1;
  }

  last_line_count=line_count;

  //
  // STEP 2: Apply timescaling if requested
  //
  if(enable_timescaling) {
    for(int i=0;i<line_count;i++) {
      RDLogLine *ll=log_model->logLine(i);
      if(ll!=NULL) {
        ll->setTimescalingActive(ll->enforceLength());
      }
    }
  }

  //
  // STEP 3: Batch-load cuts into a temporary cache (load-time optimization).
  //
  // A single SQL query fetches all cut data for all carts in the log,
  // avoiding N individual queries during the RefreshEvents() pass that
  // follows.  The cache is owned by this loader object, passed by pointer
  // into RefreshEvents()/setEvent(), and destroyed when this loader goes
  // out of scope.  It is never stored on log lines or retained after load.
  //
  QVector<uint> cart_numbers;
  for(int i=0;i<line_count;i++) {
    RDLogLine *ll=log_model->logLine(i);
    if(ll!=NULL&&ll->type()==RDLogLine::Cart&&ll->cartNumber()>0) {
      if(!cart_numbers.contains(ll->cartNumber())) {
        cart_numbers.push_back(ll->cartNumber());
      }
    }
  }

  last_cart_count=cart_numbers.size();

  if(cart_numbers.isEmpty()) {
    syslog(LOG_INFO,
           "RDLogLoader: no audio carts in log '%s', skipping cut cache",
           log_name.toUtf8().constData());
  }
  else {
    loader_cut_cache=new RDCutCache();
    if(!loader_cut_cache->batchLoadCuts(cart_numbers)) {
      delete loader_cut_cache;
      loader_cut_cache=NULL;
      syslog(LOG_WARNING,
             "RDLogLoader: cut cache failed for log '%s', "
             "cuts will load on demand",
             log_name.toUtf8().constData());
    }
    else {
      syslog(LOG_INFO,
             "RDLogLoader: batch-loaded cuts for %d carts in log '%s' "
             "(%d lines)",
             cart_numbers.size(),
             log_name.toUtf8().constData(),
             line_count);
    }
  }

  return line_count;
}


RDCutCache *RDLogLoader::cutCache() const
{
  return loader_cut_cache;
}
