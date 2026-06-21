// rdlog_loader.h
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

#ifndef RDLOG_LOADER_H
#define RDLOG_LOADER_H

#include <QString>
#include <QVector>

#include <rdlogmodel.h>
#include <rdcut_cache.h>

//
// Encapsulates the log loading process:
//   1. Load log lines from database (RDLogModel::load)
//   2. Batch-load cut metadata into a temporary cache (one SQL query)
//
// The cache is owned by this object and destroyed with it.
// Callers pass cutCache() into RefreshEvents() for use during the initial
// setEvent() pass only; it is never stored on log lines or retained after load.
//
class RDLogLoader
{
 public:
  RDLogLoader();
  ~RDLogLoader();

  //
  // Load a log into the provided RDLogModel.
  // Returns number of lines loaded, or -1 on error.
  //
  int loadLog(RDLogModel *log_model,bool enable_timescaling=false);

  //
  // Temporary cut cache built during load.
  // Valid until this RDLogLoader is destroyed.
  // Pass to RefreshEvents() immediately after loadLog(); do not store.
  //
  RDCutCache *cutCache() const;

  int lastLineCount() const { return last_line_count; }
  int lastCartCount() const { return last_cart_count; }
  QString lastError() const { return last_error; }

 private:
  RDCutCache *loader_cut_cache;
  int last_line_count;
  int last_cart_count;
  QString last_error;
};

#endif  // RDLOG_LOADER_H
