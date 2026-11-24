// list_mixminus.h
//
// List a Rivendell host's mix-minus outputs
//
//   (C) Copyright 2026 Fred Gleason <fredg@paravelsystems.com>
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

#ifndef LIST_MIXMINUS_H
#define LIST_MIXMINUS_H

#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>

#include <rddialog.h>
#include <rdmixminus.h>
#include <rdtableview.h>

#include "edit_mixminus.h"

class ListMixMinus : public RDDialog
{
 Q_OBJECT
 public:
  ListMixMinus(const QString &station,QWidget *parent=0);
  ~ListMixMinus();
  QSize sizeHint() const;
  QSizePolicy sizePolicy() const;

 private slots:
  void addData();
  void editData();
  void deleteData();
  void doubleClickedData(const QModelIndex &index);
  void closeData();

 protected:
  void resizeEvent(QResizeEvent *e);

 private:
  void Refresh(int select_id=-1);
  int SelectedRow() const;
  bool Edit(RDMixMinus *mm);
  EditMixMinus *list_edit_dialog;
  QLabel *list_title_label;
  QLabel *list_note_label;
  RDTableView *list_view;
  QStandardItemModel *list_model;
  QPushButton *list_add_button;
  QPushButton *list_edit_button;
  QPushButton *list_delete_button;
  QPushButton *list_close_button;
  QString list_station;
  int list_card;
  int list_inputs;
  int list_outputs;
  QList<RDMixMinus> list_mms;
  bool list_modified;
};


#endif  // LIST_MIXMINUS_H
