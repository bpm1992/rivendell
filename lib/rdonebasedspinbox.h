// rdonebasedspinbox.h
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

#ifndef RDONEBASEDSPINBOX_H
#define RDONEBASEDSPINBOX_H

#include <QSpinBox>

//
// Card and port numbers are stored counting from 0; people see them
// counting from 1.  The value is the stored number; the text is one
// more.  A special value text (such as "None") still shows at the
// minimum.
//
class RDOneBasedSpinBox : public QSpinBox
{
 public:
  RDOneBasedSpinBox(QWidget *parent=0);

 protected:
  QString textFromValue(int value) const;
  int valueFromText(const QString &text) const;
  QValidator::State validate(QString &input,int &pos) const;
};


#endif  // RDONEBASEDSPINBOX_H
