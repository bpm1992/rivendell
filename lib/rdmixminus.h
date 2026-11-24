// rdmixminus.h
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

#ifndef RDMIXMINUS_H
#define RDMIXMINUS_H

#include <QList>
#include <QString>
#include <QStringList>

//
// A mix-minus output carries the full sum of another output (every
// stream and passthrough mixed into it) less the contributions of
// chosen inputs, at its own level.  Each contributor hears everything
// but themselves.  Rows live in the MIX_MINUS table; ports there count
// from 0, and every name shown to people counts from 1.
//
class RDMixMinus
{
 public:
  RDMixMinus();
  int id() const;
  QString stationName() const;
  void setStationName(const QString &str);
  int card() const;
  void setCard(int card);
  int output() const;
  void setOutput(int port);
  int source() const;
  void setSource(int port);
  QList<int> excluded() const;
  QStringList ignoredExcluded() const;
  void setExcluded(const QList<int> &ports);
  int level() const;
  void setLevel(int level);
  QString description() const;
  void setDescription(const QString &str);
  bool isEnabled() const;
  void setEnabled(bool state);
  QString outputName() const;
  QString sourceName() const;
  QString excludedNames() const;
  QString levelText() const;
  bool save(QString *err_msg);
  QString validate(const QList<RDMixMinus> &others,int inputs,
		   int outputs) const;
  static bool tableExists();
  static QList<RDMixMinus> list(const QString &station,int card=-1);
  static void remove(int id);
  static void removeStation(const QString &station);
  static void copyStation(const QString &exemplar,const QString &station);
  static QString excludedText(const QList<int> &ports);
  static QList<int> parseExcluded(const QString &str,QStringList *bad=NULL);
  static QString portName(const QString &kind,int port);
  static QStringList conflicts(const QString &station,int card,int output);

 private:
  int mm_id;
  QString mm_station_name;
  int mm_card;
  int mm_output;
  int mm_source;
  QList<int> mm_excluded;
  QStringList mm_ignored;
  int mm_level;
  QString mm_description;
  bool mm_enabled;
};


#endif  // RDMIXMINUS_H
