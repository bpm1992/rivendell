// edit_mixminus.cpp
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

#include <QMessageBox>

#include "edit_mixminus.h"

#define EDIT_MIXMINUS_COLUMNS 4

EditMixMinus::EditMixMinus(QWidget *parent)
  : RDDialog(parent)
{
  edit_mm=NULL;
  edit_inputs=0;
  edit_outputs=0;

  setWindowTitle("RDAdmin - "+tr("Edit Mix-Minus Output"));

  //
  // Output
  //
  edit_output_box=new QComboBox(this);
  edit_output_box->setGeometry(130,11,220,22);
  QLabel *label=new QLabel(tr("Output:"),this);
  label->setGeometry(10,11,115,22);
  label->setFont(labelFont());
  label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
  edit_output_box->
    setToolTip(tr("The output that carries the mix-minus.  It carries\n"
		  "nothing else: audio set to play there isn't heard."));

  //
  // Source
  //
  edit_source_box=new QComboBox(this);
  edit_source_box->setGeometry(130,37,220,22);
  label=new QLabel(tr("Source:"),this);
  label->setGeometry(10,37,115,22);
  label->setFont(labelFont());
  label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
  edit_source_box->
    setToolTip(tr("Everything mixed into this output (every cart and\n"
		  "every input passed to it) before its own level."));

  //
  // Excluded Inputs
  //
  edit_exclude_label=new QLabel(tr("Exclude Inputs:"),this);
  edit_exclude_label->setGeometry(10,63,115,20);
  edit_exclude_label->setFont(labelFont());
  edit_exclude_label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
  for(int i=0;i<RD_MAX_PORTS;i++) {
    edit_exclude_check[i]=
      new QCheckBox(RDMixMinus::portName(tr("Input"),i),this);
    edit_exclude_check[i]->
      setGeometry(130+80*(i%EDIT_MIXMINUS_COLUMNS),63+22*(i/EDIT_MIXMINUS_COLUMNS),
		  78,20);
    edit_exclude_check[i]->hide();
  }

  //
  // Level
  //
  edit_level_spin=new QDoubleSpinBox(this);
  edit_level_spin->setRange((double)RD_MUTE_DEPTH/100.0,10.0);
  edit_level_spin->setDecimals(1);
  edit_level_spin->setSingleStep(0.5);
  edit_level_spin->setSuffix(" dB");
  edit_level_spin->setSpecialValueText(tr("Muted"));

  //
  // Description
  //
  edit_description_edit=new QLineEdit(this);
  edit_description_edit->setMaxLength(64);

  //
  // Enabled
  //
  edit_enabled_check=new QCheckBox(tr("Enabled"),this);
  edit_enabled_check->setFont(labelFont());

  //
  //  Ok Button
  //
  edit_ok_button=new QPushButton(this);
  edit_ok_button->setDefault(true);
  edit_ok_button->setFont(buttonFont());
  edit_ok_button->setText(tr("OK"));
  connect(edit_ok_button,SIGNAL(clicked()),this,SLOT(okData()));

  //
  //  Cancel Button
  //
  edit_cancel_button=new QPushButton(this);
  edit_cancel_button->setFont(buttonFont());
  edit_cancel_button->setText(tr("Cancel"));
  connect(edit_cancel_button,SIGNAL(clicked()),this,SLOT(cancelData()));
}


EditMixMinus::~EditMixMinus()
{
}


QSize EditMixMinus::sizeHint() const
{
  int rows=(edit_inputs+EDIT_MIXMINUS_COLUMNS-1)/EDIT_MIXMINUS_COLUMNS;
  if(rows<1) {
    rows=1;
  }
  return QSize(460,63+22*rows+160);
}


QSizePolicy EditMixMinus::sizePolicy() const
{
  return QSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
}


int EditMixMinus::exec(RDMixMinus *mm,const QList<RDMixMinus> &others,
		       const QStringList &input_labels,
		       const QStringList &output_labels)
{
  edit_mm=mm;
  edit_others=others;
  edit_inputs=input_labels.size();
  edit_outputs=output_labels.size();
  if(edit_inputs>RD_MAX_PORTS) {
    edit_inputs=RD_MAX_PORTS;
  }

  //
  // Outputs and sources, named as RDAdmin's Audio Ports name them
  //
  edit_output_box->clear();
  edit_source_box->clear();
  for(int i=0;i<edit_outputs;i++) {
    QString name=RDMixMinus::portName(tr("Output"),i);
    if(!output_labels.at(i).trimmed().isEmpty()) {
      name+=" ("+output_labels.at(i).trimmed()+")";
    }
    edit_output_box->addItem(name,i);
    edit_source_box->addItem(tr("Full summed")+" "+name,i);
  }
  edit_output_box->setCurrentIndex(edit_output_box->findData(mm->output()));
  edit_source_box->setCurrentIndex(edit_source_box->findData(mm->source()));

  QList<int> excluded=mm->excluded();
  for(int i=0;i<RD_MAX_PORTS;i++) {
    if(i<edit_inputs) {
      edit_exclude_check[i]->setChecked(excluded.contains(i));
      edit_exclude_check[i]->setToolTip(input_labels.at(i));
      edit_exclude_check[i]->show();
    }
    else {
      edit_exclude_check[i]->setChecked(false);
      edit_exclude_check[i]->hide();
    }
  }
  edit_level_spin->setValue((double)mm->level()/100.0);
  edit_description_edit->setText(mm->description());
  edit_enabled_check->setChecked(mm->isEnabled());

  //
  // Lay out below the input grid
  //
  int rows=(edit_inputs+EDIT_MIXMINUS_COLUMNS-1)/EDIT_MIXMINUS_COLUMNS;
  if(rows<1) {
    rows=1;
  }
  int y=63+22*rows+8;
  QList<QLabel *> labels=findChildren<QLabel *>("mixminus_row_label");
  for(int i=0;i<labels.size();i++) {
    delete labels.at(i);
  }
  QLabel *label=new QLabel(tr("Level:"),this);
  label->setObjectName("mixminus_row_label");
  label->setGeometry(10,y,115,22);
  label->setFont(labelFont());
  label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
  label->show();
  edit_level_spin->setGeometry(130,y,100,22);
  label=new QLabel(tr("Description:"),this);
  label->setObjectName("mixminus_row_label");
  label->setGeometry(10,y+26,115,22);
  label->setFont(labelFont());
  label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
  label->show();
  edit_description_edit->setGeometry(130,y+26,320,22);
  edit_enabled_check->setGeometry(130,y+52,200,22);
  setFixedSize(sizeHint());
  edit_ok_button->setGeometry(sizeHint().width()-180,sizeHint().height()-60,
			      80,50);
  edit_cancel_button->setGeometry(sizeHint().width()-90,
				  sizeHint().height()-60,80,50);

  return QDialog::exec();
}


void EditMixMinus::okData()
{
  RDMixMinus mm=*edit_mm;
  mm.setOutput(edit_output_box->currentData().toInt());
  mm.setSource(edit_source_box->currentData().toInt());
  QList<int> excluded;
  for(int i=0;i<edit_inputs;i++) {
    if(edit_exclude_check[i]->isChecked()) {
      excluded.push_back(i);
    }
  }
  mm.setExcluded(excluded);
  int level=(int)(edit_level_spin->value()*100.0+
		  ((edit_level_spin->value()<0.0)?-0.5:0.5));
  if(level<=RD_MUTE_DEPTH) {
    level=RD_MUTE_DEPTH;
  }
  mm.setLevel(level);
  mm.setDescription(edit_description_edit->text().trimmed());
  mm.setEnabled(edit_enabled_check->isChecked());

  QString err=mm.validate(edit_others,edit_inputs,edit_outputs);
  if(!err.isEmpty()) {
    QMessageBox::warning(this,"RDAdmin - "+tr("Mix-Minus Output"),err);
    return;
  }
  QStringList conflicts=
    RDMixMinus::conflicts(mm.stationName(),mm.card(),mm.output());
  if(mm.isEnabled()&&(conflicts.size()>0)) {
    if(QMessageBox::question(this,"RDAdmin - "+tr("Mix-Minus Output"),
			     mm.outputName()+" "+tr("is also set to play")+" "+
			     conflicts.join(", ")+".\n\n"+
			     tr("A mix-minus output carries only its mix-minus, "
				"so that audio won't be heard there.")+"\n\n"+
			     tr("Save anyway?"),
			     QMessageBox::Yes,QMessageBox::No)!=
       QMessageBox::Yes) {
      return;
    }
  }
  *edit_mm=mm;
  done(true);
}


void EditMixMinus::cancelData()
{
  done(false);
}
