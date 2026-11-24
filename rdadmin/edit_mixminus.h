// edit_mixminus.h
//
// Edit a Rivendell mix-minus output
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

#ifndef EDIT_MIXMINUS_H
#define EDIT_MIXMINUS_H

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>

#include <rd.h>
#include <rddialog.h>
#include <rdmixminus.h>

class EditMixMinus : public RDDialog
{
  Q_OBJECT
 public:
  EditMixMinus(QWidget *parent=0);
  ~EditMixMinus();
  QSize sizeHint() const;
  QSizePolicy sizePolicy() const;

 public slots:
  int exec(RDMixMinus *mm,const QList<RDMixMinus> &others,
	   const QStringList &input_labels,const QStringList &output_labels);

 private slots:
  void okData();
  void cancelData();

 private:
  RDMixMinus *edit_mm;
  QList<RDMixMinus> edit_others;
  int edit_inputs;
  int edit_outputs;
  QComboBox *edit_output_box;
  QComboBox *edit_source_box;
  QLabel *edit_exclude_label;
  QCheckBox *edit_exclude_check[RD_MAX_PORTS];
  QDoubleSpinBox *edit_level_spin;
  QLineEdit *edit_description_edit;
  QCheckBox *edit_enabled_check;
  QPushButton *edit_ok_button;
  QPushButton *edit_cancel_button;
};


#endif  // EDIT_MIXMINUS_H
