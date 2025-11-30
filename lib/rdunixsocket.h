// rdunixsocket.h
//
// UNIX Socket
//
//   (C) Copyright 2018 Fred Gleason <fredg@paravelsystems.com>
//
//   This class works much the same way as QTcpSocket, only it supports
//   SOCK_STREAM connections via UNIX sockets rather than TCP/IP.
//
//   See the unix(7) man page for a description of the difference between
//   'pathname' and 'abstract' socket addresses.
//
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

#ifndef RDUNIXSOCKET_H
#define RDUNIXSOCKET_H

#include <qtcpsocket.h>
#include <qsocketnotifier.h>

class RDUnixSocket : public QTcpSocket
{
  Q_OBJECT
 public:
  RDUnixSocket(QObject *parent=0);
  bool connectToPathname(const QString &pathname,OpenMode mode=ReadWrite);
  bool connectToAbstract(const QString &addr,OpenMode mode=ReadWrite);
  // Forcefully unregister the socket from Qt's event loop and close the FD
  void closeAndUnregister();
  // Helper to detect EOF/spurious readiness on SEQPACKET sockets
  bool isEofOrSpuriousReady() const;
  qint64 writeWithErrorCheck(const QByteArray &data);

 private slots:
  void notifierReady();

 private:
  QSocketNotifier *m_notifier=nullptr;
};


#endif  // RDUNIXSOCKET_H
