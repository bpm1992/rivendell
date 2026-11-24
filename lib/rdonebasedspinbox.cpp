// rdonebasedspinbox.cpp
//
// A spin box that shows port and card numbers counting from 1
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

#include "rdonebasedspinbox.h"

RDOneBasedSpinBox::RDOneBasedSpinBox(QWidget *parent)
  : QSpinBox(parent)
{
}


QString RDOneBasedSpinBox::textFromValue(int value) const
{
  return QString::asprintf("%d",value+1);
}


int RDOneBasedSpinBox::valueFromText(const QString &text) const
{
  if((!specialValueText().isEmpty())&&(text==specialValueText())) {
    return minimum();
  }
  return text.trimmed().toInt()-1;
}


QValidator::State RDOneBasedSpinBox::validate(QString &input,int &pos) const
{
  if((!specialValueText().isEmpty())&&
     specialValueText().startsWith(input,Qt::CaseInsensitive)) {
    return (input.length()==specialValueText().length())?
      QValidator::Acceptable:QValidator::Intermediate;
  }
  if(input.trimmed().isEmpty()) {
    return QValidator::Intermediate;
  }
  bool ok=false;
  int value=input.trimmed().toInt(&ok)-1;
  if(!ok) {
    return QValidator::Invalid;
  }
  if((value>=minimum())&&(value<=maximum())) {
    return QValidator::Acceptable;
  }
  return QValidator::Intermediate;
}
