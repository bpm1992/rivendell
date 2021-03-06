// getpids_test.h
//
// Test the Rivendell RDGetPids() function.
//
//   (C) Copyright 2018 Fred Gleason <fredg@paravelsystems.com>
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

#ifndef GETPIDS_TEST_H
#define GETPIDS_TEST_H

#include <qobject.h>

#define GETPIDS_TEST_USAGE "[options]\n\nTest the Rivendell RDGetPids() function\n\nOptions are:\n--program=<pathname>\n     Full path to the program to look for\n\n"

class MainObject : public QObject
{
 public:
  MainObject(QObject *parent=0);
};


#endif  // GETPIDS_TEST_H
