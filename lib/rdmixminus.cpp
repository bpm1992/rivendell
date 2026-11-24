// rdmixminus.cpp
//
// Abstract a Rivendell mix-minus output
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

#include <algorithm>

#include <QObject>

#include "rd.h"
#include "rddb.h"
#include "rdescape_string.h"
#include "rdmixminus.h"

RDMixMinus::RDMixMinus()
{
  mm_id=-1;
  mm_card=0;
  mm_output=-1;
  mm_source=0;
  mm_level=0;
  mm_enabled=true;
}


int RDMixMinus::id() const
{
  return mm_id;
}


QString RDMixMinus::stationName() const
{
  return mm_station_name;
}


void RDMixMinus::setStationName(const QString &str)
{
  mm_station_name=str;
}


int RDMixMinus::card() const
{
  return mm_card;
}


void RDMixMinus::setCard(int card)
{
  mm_card=card;
}


int RDMixMinus::output() const
{
  return mm_output;
}


void RDMixMinus::setOutput(int port)
{
  mm_output=port;
}


int RDMixMinus::source() const
{
  return mm_source;
}


void RDMixMinus::setSource(int port)
{
  mm_source=port;
}


QList<int> RDMixMinus::excluded() const
{
  return mm_excluded;
}


QStringList RDMixMinus::ignoredExcluded() const
{
  //
  // Entries of the stored EXCLUDED_INPUTS that aren't input numbers
  //
  return mm_ignored;
}


void RDMixMinus::setExcluded(const QList<int> &ports)
{
  mm_excluded=parseExcluded(excludedText(ports));
}


int RDMixMinus::level() const
{
  return mm_level;
}


void RDMixMinus::setLevel(int level)
{
  mm_level=level;
}


QString RDMixMinus::description() const
{
  return mm_description;
}


void RDMixMinus::setDescription(const QString &str)
{
  mm_description=str;
}


bool RDMixMinus::isEnabled() const
{
  return mm_enabled;
}


void RDMixMinus::setEnabled(bool state)
{
  mm_enabled=state;
}


QString RDMixMinus::outputName() const
{
  return portName(QObject::tr("Output"),mm_output);
}


QString RDMixMinus::sourceName() const
{
  return QObject::tr("Full summed")+" "+portName(QObject::tr("Output"),mm_source);
}


QString RDMixMinus::excludedNames() const
{
  QStringList names;
  for(int i=0;i<mm_excluded.size();i++) {
    names.push_back(portName(QObject::tr("Input"),mm_excluded.at(i)));
  }
  return names.join(", ");
}


QString RDMixMinus::levelText() const
{
  if(mm_level<=RD_MUTE_DEPTH) {
    return QObject::tr("Muted");
  }
  return QString::asprintf("%+.1f dB",(double)mm_level/100.0).
    replace("+0.0","0.0");
}


bool RDMixMinus::save(QString *err_msg)
{
  QString sql;
  QString values=
    "`STATION_NAME`='"+RDEscapeString(mm_station_name)+"',"+
    QString::asprintf("`CARD_NUMBER`=%d,",mm_card)+
    QString::asprintf("`PORT_NUMBER`=%d,",mm_output)+
    QString::asprintf("`SOURCE_PORT`=%d,",mm_source)+
    "`EXCLUDED_INPUTS`='"+excludedText(mm_excluded)+"',"+
    QString::asprintf("`LEVEL`=%d,",mm_level)+
    "`DESCRIPTION`='"+RDEscapeString(mm_description)+"',"+
    "`IS_ENABLED`='"+(mm_enabled?"Y":"N")+"'";
  if(mm_id<0) {
    sql="insert into `MIX_MINUS` set "+values;
    bool ok=false;
    QVariant id=RDSqlQuery::run(sql,&ok);
    if(!ok) {
      if(err_msg!=NULL) {
	*err_msg=QObject::tr("The mix-minus could not be saved.");
      }
      return false;
    }
    mm_id=id.toInt();
    return true;
  }
  sql="update `MIX_MINUS` set "+values+
    QString::asprintf(" where `ID`=%d",mm_id);
  if(!RDSqlQuery::apply(sql)) {
    if(err_msg!=NULL) {
      *err_msg=QObject::tr("The mix-minus could not be saved.");
    }
    return false;
  }
  return true;
}


QString RDMixMinus::validate(const QList<RDMixMinus> &others,int inputs,
			     int outputs) const
{
  //
  // Why this row can't be saved, or an empty string.  'others' are the
  // station's other rows on the same card.  The audio engine applies
  // the same rules when it loads the table.
  //
  if((mm_output<0)||(mm_output>=outputs)) {
    return QObject::tr("Choose the output that carries the mix-minus.");
  }
  if((mm_source<0)||(mm_source>=outputs)) {
    return QObject::tr("Choose the output whose sum it takes.");
  }
  if(mm_output==mm_source) {
    return QObject::tr("A mix-minus can't take the sum of its own output.");
  }
  if(mm_enabled&&(mm_excluded.size()==0)) {
    return QObject::tr("Exclude at least one input.")+"\n"+
      QObject::tr("Without one it is a copy of")+" "+
      portName(QObject::tr("Output"),mm_source)+".";
  }
  for(int i=0;i<mm_excluded.size();i++) {
    if(mm_excluded.at(i)>=inputs) {
      return portName(QObject::tr("Input"),mm_excluded.at(i))+" "+
	QObject::tr("doesn't exist on this card.");
    }
  }
  if((mm_level<RD_MUTE_DEPTH)||(mm_level>1000)) {
    return QObject::tr("The level must be between -100 and +10 dB.");
  }
  for(int i=0;i<others.size();i++) {
    const RDMixMinus &o=others.at(i);
    if(((mm_id>=0)&&(o.id()==mm_id))||(o.card()!=mm_card)) {
      continue;
    }
    if(o.output()==mm_output) {
      return outputName()+" "+QObject::tr("already carries a mix-minus.");
    }
    if(mm_enabled&&o.isEnabled()) {
      if(o.output()==mm_source) {
	return portName(QObject::tr("Output"),mm_source)+" "+
	  QObject::tr("is itself a mix-minus, so it can't be a source.");
      }
      if(o.source()==mm_output) {
	return outputName()+" "+
	  QObject::tr("is the source of the mix-minus on")+" "+
	  o.outputName()+".";
      }
    }
  }
  return QString();
}


bool RDMixMinus::tableExists()
{
  QString sql=QString("select `TABLE_NAME` from `information_schema`.`TABLES` ")+
    "where (`TABLE_SCHEMA`=database())&&(`TABLE_NAME`='MIX_MINUS')";
  RDSqlQuery *q=new RDSqlQuery(sql);
  bool ret=q->first();
  delete q;
  return ret;
}


QList<RDMixMinus> RDMixMinus::list(const QString &station,int card)
{
  QList<RDMixMinus> ret;
  if(!tableExists()) {
    return ret;
  }
  QString sql=QString("select ")+
    "`ID`,"+               // 00
    "`STATION_NAME`,"+     // 01
    "`CARD_NUMBER`,"+      // 02
    "`PORT_NUMBER`,"+      // 03
    "`SOURCE_PORT`,"+      // 04
    "`EXCLUDED_INPUTS`,"+  // 05
    "`LEVEL`,"+            // 06
    "`DESCRIPTION`,"+      // 07
    "`IS_ENABLED` "+       // 08
    "from `MIX_MINUS` where "+
    "(`STATION_NAME`='"+RDEscapeString(station)+"')";
  if(card>=0) {
    sql+=QString::asprintf("&&(`CARD_NUMBER`=%d)",card);
  }
  sql+=" order by `CARD_NUMBER`,`PORT_NUMBER`";
  RDSqlQuery *q=new RDSqlQuery(sql);
  while(q->next()) {
    RDMixMinus mm;
    mm.mm_id=q->value(0).toInt();
    mm.mm_station_name=q->value(1).toString();
    mm.mm_card=q->value(2).toInt();
    mm.mm_output=q->value(3).toInt();
    mm.mm_source=q->value(4).toInt();
    mm.mm_excluded=parseExcluded(q->value(5).toString(),&mm.mm_ignored);
    mm.mm_level=q->value(6).toInt();
    mm.mm_description=q->value(7).toString();
    mm.mm_enabled=q->value(8).toString()=="Y";
    ret.push_back(mm);
  }
  delete q;
  return ret;
}


void RDMixMinus::remove(int id)
{
  if(tableExists()) {
    RDSqlQuery::apply(QString::asprintf("delete from `MIX_MINUS` where `ID`=%d",
					id));
  }
}


void RDMixMinus::removeStation(const QString &station)
{
  if(tableExists()) {
    RDSqlQuery::apply("delete from `MIX_MINUS` where `STATION_NAME`='"+
		      RDEscapeString(station)+"'");
  }
}


void RDMixMinus::copyStation(const QString &exemplar,const QString &station)
{
  QList<RDMixMinus> mms=list(exemplar);
  for(int i=0;i<mms.size();i++) {
    RDMixMinus mm=mms.at(i);
    mm.mm_id=-1;
    mm.mm_station_name=station;
    mm.save(NULL);
  }
}


QString RDMixMinus::excludedText(const QList<int> &ports)
{
  //
  // The stored form: inputs from 0, ascending, no repeats, no spaces
  //
  QList<int> sorted;
  for(int i=0;i<ports.size();i++) {
    if((ports.at(i)>=0)&&(ports.at(i)<RD_MAX_PORTS)&&
       (!sorted.contains(ports.at(i)))) {
      sorted.push_back(ports.at(i));
    }
  }
  std::sort(sorted.begin(),sorted.end());
  QStringList f0;
  for(int i=0;i<sorted.size();i++) {
    f0.push_back(QString::asprintf("%d",sorted.at(i)));
  }
  return f0.join(",");
}


QList<int> RDMixMinus::parseExcluded(const QString &str,QStringList *bad)
{
  QList<int> ports;
  QStringList f0=str.split(",",QString::SkipEmptyParts);
  for(int i=0;i<f0.size();i++) {
    bool ok=false;
    int port=f0.at(i).trimmed().toInt(&ok);
    if(ok&&(port>=0)&&(port<RD_MAX_PORTS)) {
      if(!ports.contains(port)) {
	ports.push_back(port);
      }
    }
    else {
      if(bad!=NULL) {
	bad->push_back(f0.at(i).trimmed());
      }
    }
  }
  std::sort(ports.begin(),ports.end());
  return ports;
}


QString RDMixMinus::portName(const QString &kind,int port)
{
  return kind+QString::asprintf(" %d",port+1);
}


QStringList RDMixMinus::conflicts(const QString &station,int card,int output)
{
  //
  // What else is set to play on a mix-minus output.  The output carries
  // only its mix-minus, so none of these would be heard there.
  //
  QStringList ret;
  QString where="(`STATION_NAME`='"+RDEscapeString(station)+"')&&"+
    QString::asprintf("(`CARD`=%d)&&(`PORT`=%d)",card,output);
  int n=RDSqlQuery::rows("select `INSTANCE` from `RDAIRPLAY_CHANNELS` where "+
			 where);
  if(n>0) {
    ret.push_back(QString("RDAirPlay (")+
		  ((n==1)?QObject::tr("1 channel"):
		   QString::asprintf("%d ",n)+QObject::tr("channels"))+")");
  }
  n=RDSqlQuery::rows("select `INSTANCE` from `RDPANEL_CHANNELS` where "+where);
  if(n>0) {
    ret.push_back(QString("RDPanel (")+
		  ((n==1)?QObject::tr("1 channel"):
		   QString::asprintf("%d ",n)+QObject::tr("channels"))+")");
  }
  RDSqlQuery *q=NULL;
  QString lib_where="(`STATION`='"+RDEscapeString(station)+"')&&"+
    QString::asprintf("(`OUTPUT_CARD`=%d)&&(`OUTPUT_PORT`=%d)",card,output);
  if(RDSqlQuery::rows("select `ID` from `RDLIBRARY` where "+lib_where)>0) {
    ret.push_back(QObject::tr("RDLibrary's play output"));
  }
  if(RDSqlQuery::rows("select `ID` from `RDLOGEDIT` where "+lib_where)>0) {
    ret.push_back(QObject::tr("RDLogEdit's play output"));
  }
  QString sql=QString("select `NAME` from `STATIONS` where ")+
    "(`NAME`='"+RDEscapeString(station)+"')&&"+
    QString::asprintf("(`CUE_CARD`=%d)&&(`CUE_PORT`=%d)",card,output);
  if(RDSqlQuery::rows(sql)>0) {
    ret.push_back(QObject::tr("the cue output"));
  }
  sql=QString("select `CHANNEL`,`PORT_NUMBER`,`MON_PORT_NUMBER` from `DECKS` ")+
    "where (`STATION_NAME`='"+RDEscapeString(station)+"')&&"+
    QString::asprintf("(`CARD_NUMBER`=%d)",card)+" order by `CHANNEL`";
  q=new RDSqlQuery(sql);
  while(q->next()) {
    int chan=q->value(0).toInt();
    if((chan>128)&&(q->value(1).toInt()==output)) {
      ret.push_back(QObject::tr("RDCatch play deck")+
		    QString::asprintf(" %d",chan-128));
    }
    if((chan<=128)&&(q->value(2).toInt()==output)) {
      ret.push_back(QObject::tr("the monitor of RDCatch record deck")+
		    QString::asprintf(" %d",chan));
    }
  }
  delete q;
  return ret;
}
