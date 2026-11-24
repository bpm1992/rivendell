// list_mixminus.cpp
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

#include <QMessageBox>

#include <rdapplication.h>
#include <rdaudio_port.h>
#include <rdmacro.h>
#include <rdstation.h>

#include "globals.h"
#include "list_mixminus.h"

ListMixMinus::ListMixMinus(const QString &station,QWidget *parent)
  : RDDialog(parent)
{
  list_station=station;
  list_card=-1;
  list_inputs=0;
  list_outputs=0;
  list_modified=false;

  setWindowTitle("RDAdmin - "+tr("Mix-Minus Outputs for")+" "+station);
  setMinimumSize(sizeHint());

  //
  // Dialogs
  //
  list_edit_dialog=new EditMixMinus(this);

  //
  // The audio engine's card (mix-minus runs in the JACK audio engine)
  //
  RDStation *rmt_station=new RDStation(station);
  for(int i=0;i<RD_MAX_CARDS;i++) {
    if(rmt_station->cardDriver(i)==RDStation::Jack) {
      list_card=i;
      list_inputs=rmt_station->cardInputs(i);
      list_outputs=rmt_station->cardOutputs(i);
      break;
    }
  }
  delete rmt_station;

  //
  // List
  //
  list_title_label=new QLabel(tr("Mix-Minus Outputs")+":",this);
  list_title_label->setFont(labelFont());
  list_model=new QStandardItemModel(this);
  list_model->setHorizontalHeaderLabels(QStringList()<<tr("Output")<<
					tr("Source")<<tr("Excludes")<<
					tr("Level")<<tr("Enabled")<<
					tr("Description"));
  list_view=new RDTableView(this);
  list_view->setModel(list_model);
  list_view->setSelectionBehavior(QAbstractItemView::SelectRows);
  list_view->setSelectionMode(QAbstractItemView::SingleSelection);
  connect(list_view,SIGNAL(doubleClicked(const QModelIndex &)),
	  this,SLOT(doubleClickedData(const QModelIndex &)));

  list_note_label=new QLabel(this);
  list_note_label->setWordWrap(true);
  list_note_label->
    setText(tr("Each mix-minus output carries the full sum of its source "
	       "output less the inputs it excludes: a contributor on an "
	       "excluded input hears everything but themselves."));

  //
  //  Buttons
  //
  list_add_button=new QPushButton(tr("Add"),this);
  list_add_button->setFont(buttonFont());
  connect(list_add_button,SIGNAL(clicked()),this,SLOT(addData()));

  list_edit_button=new QPushButton(tr("Edit"),this);
  list_edit_button->setFont(buttonFont());
  connect(list_edit_button,SIGNAL(clicked()),this,SLOT(editData()));

  list_delete_button=new QPushButton(tr("Delete"),this);
  list_delete_button->setFont(buttonFont());
  connect(list_delete_button,SIGNAL(clicked()),this,SLOT(deleteData()));

  list_close_button=new QPushButton(tr("Close"),this);
  list_close_button->setDefault(true);
  list_close_button->setFont(buttonFont());
  connect(list_close_button,SIGNAL(clicked()),this,SLOT(closeData()));

  //
  // What this host can't do
  //
  QString why;
  if(!RDMixMinus::tableExists()) {
    why=tr("This database has no MIX_MINUS table.");
  }
  else if(list_card<0) {
    why=tr("Mix-minus outputs need the JACK audio engine; this host has "
	   "no JACK card.");
  }
  if(!why.isEmpty()) {
    list_note_label->setText(why);
    list_add_button->setDisabled(true);
    list_edit_button->setDisabled(true);
    list_delete_button->setDisabled(true);
  }
  Refresh();
}


ListMixMinus::~ListMixMinus()
{
  delete list_edit_dialog;
}


QSize ListMixMinus::sizeHint() const
{
  return QSize(640,360);
}


QSizePolicy ListMixMinus::sizePolicy() const
{
  return QSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
}


void ListMixMinus::addData()
{
  //
  // Start on the highest free output, taking the first output's sum
  //
  RDMixMinus mm;
  mm.setStationName(list_station);
  mm.setCard(list_card);
  mm.setSource(0);
  for(int i=list_outputs-1;i>0;i--) {
    bool used=false;
    for(int j=0;j<list_mms.size();j++) {
      used=used||(list_mms.at(j).output()==i);
    }
    if(!used) {
      mm.setOutput(i);
      break;
    }
  }
  if(mm.output()<0) {
    QMessageBox::information(this,"RDAdmin - "+tr("Mix-Minus Outputs"),
			     tr("Every output already carries a mix-minus."));
    return;
  }
  if(Edit(&mm)) {
    Refresh(mm.id());
  }
}


void ListMixMinus::editData()
{
  int row=SelectedRow();
  if(row<0) {
    return;
  }
  RDMixMinus mm=list_mms.at(row);
  if(Edit(&mm)) {
    Refresh(mm.id());
  }
}


void ListMixMinus::deleteData()
{
  int row=SelectedRow();
  if(row<0) {
    return;
  }
  const RDMixMinus &mm=list_mms.at(row);
  if(QMessageBox::question(this,"RDAdmin - "+tr("Delete Mix-Minus Output"),
			   tr("Are you sure you want to delete the mix-minus "
			      "on")+" "+mm.outputName()+"?",
			   QMessageBox::Yes,QMessageBox::No)!=
     QMessageBox::Yes) {
    return;
  }
  RDMixMinus::remove(mm.id());
  list_modified=true;
  Refresh();
}


void ListMixMinus::doubleClickedData(const QModelIndex &index)
{
  editData();
}


void ListMixMinus::closeData()
{
  //
  // Have the host's audio engine load the changes now
  //
  if(list_modified) {
    RDStation *rmt_station=new RDStation(list_station);
    RDMacro macro;
    macro.setCommand(RDMacro::MX);
    macro.setRole(RDMacro::Cmd);
    macro.setEchoRequested(false);
    macro.setAddress(rmt_station->address());
    macro.addArg(list_card);
    rda->ripc()->sendRml(&macro);
    delete rmt_station;
  }
  done(true);
}


void ListMixMinus::resizeEvent(QResizeEvent *e)
{
  list_title_label->setGeometry(14,5,size().width()-28,19);
  list_view->setGeometry(10,24,size().width()-20,size().height()-154);
  list_note_label->setGeometry(10,size().height()-126,size().width()-20,40);
  list_add_button->setGeometry(10,size().height()-80,80,50);
  list_edit_button->setGeometry(100,size().height()-80,80,50);
  list_delete_button->setGeometry(190,size().height()-80,80,50);
  list_close_button->setGeometry(size().width()-90,size().height()-60,80,50);
}


void ListMixMinus::Refresh(int select_id)
{
  list_mms=RDMixMinus::list(list_station,list_card);
  list_model->removeRows(0,list_model->rowCount());
  for(int i=0;i<list_mms.size();i++) {
    const RDMixMinus &mm=list_mms.at(i);
    QList<QStandardItem *> items;
    items.push_back(new QStandardItem(mm.outputName()));
    items.push_back(new QStandardItem(mm.sourceName()));
    items.push_back(new QStandardItem(mm.excludedNames()));
    items.push_back(new QStandardItem(mm.levelText()));
    items.push_back(new QStandardItem(mm.isEnabled()?tr("Yes"):tr("No")));
    items.push_back(new QStandardItem(mm.description()));
    for(int j=0;j<items.size();j++) {
      items.at(j)->setEditable(false);
    }
    list_model->appendRow(items);
    if(mm.id()==select_id) {
      list_view->selectRow(i);
    }
  }
  list_view->resizeColumnsToContents();
}


int ListMixMinus::SelectedRow() const
{
  QModelIndexList rows=list_view->selectionModel()->selectedRows();
  if(rows.size()!=1) {
    return -1;
  }
  return rows.first().row();
}


bool ListMixMinus::Edit(RDMixMinus *mm)
{
  RDAudioPort *port=new RDAudioPort(list_station,list_card);
  QStringList inputs;
  QStringList outputs;
  for(int i=0;i<list_inputs;i++) {
    inputs.push_back(port->inputPortLabel(i));
  }
  for(int i=0;i<list_outputs;i++) {
    outputs.push_back(port->outputPortLabel(i));
  }
  delete port;
  QList<RDMixMinus> others;
  for(int i=0;i<list_mms.size();i++) {
    if(list_mms.at(i).id()!=mm->id()) {
      others.push_back(list_mms.at(i));
    }
  }
  if(!list_edit_dialog->exec(mm,others,inputs,outputs)) {
    return false;
  }
  QString err;
  if(!mm->save(&err)) {
    QMessageBox::warning(this,"RDAdmin - "+tr("Mix-Minus Outputs"),err);
    return false;
  }
  list_modified=true;
  return true;
}
