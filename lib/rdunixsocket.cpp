// rdunixsocket.cpp
//
// UNIX Socket
//
//   (C) Copyright 2018-2025 Fred Gleason <fredg@paravelsystems.com>
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

#include <linux/un.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "rdunixsocket.h"

RDUnixSocket::RDUnixSocket(QObject *parent)
  : QTcpSocket(parent)
{
  m_notifier=nullptr;
}


bool RDUnixSocket::connectToPathname(const QString &pathname,
				     QAbstractSocket::OpenMode mode)
{
  return false;
}


bool RDUnixSocket::connectToAbstract(const QString &addr,
				     QAbstractSocket::OpenMode mode)
{
  int sock;
  struct sockaddr_un sa;

  if((sock=::socket(AF_UNIX,SOCK_SEQPACKET,0))<0) {
    return false;
  }
  memset(&sa,0,sizeof(sa));
  sa.sun_family=AF_UNIX;
  strncpy(sa.sun_path+1,addr.toUtf8(),UNIX_PATH_MAX-2);
  if(::connect(sock,(struct sockaddr *)(&sa),sizeof(sa))<0) {
    ::close(sock);  // Clean up the socket on connection failure
    return false;
  }
  setSocketDescriptor(sock,QAbstractSocket::ConnectedState,mode);

  // Install a QSocketNotifier to detect spurious readiness/EOF immediately
  if(m_notifier!=nullptr) {
    delete m_notifier;
    m_notifier=nullptr;
  }
  m_notifier=new QSocketNotifier(socketDescriptor(),QSocketNotifier::Read,this);
  connect(m_notifier,&QSocketNotifier::activated,this,&RDUnixSocket::notifierReady);

  return true;
}


void RDUnixSocket::closeAndUnregister()
{
  // Unregister from Qt event loop and close the FD to stop spurious events
  int fd=socketDescriptor();
  if(fd>=0) {
    setSocketDescriptor(-1);
    ::close(fd);
  }
  abort();
}


bool RDUnixSocket::isEofOrSpuriousReady() const
{
  // bytesAvailable()==0 combined with any non-UnknownSocketError or
  // a ConnectedState often indicates EOF/spurious readiness on SEQPACKET
  if(bytesAvailable()==0) {
    return true;
  }
  return false;
}

void RDUnixSocket::notifierReady()
{
  // If no data available, treat as EOF; disable notifier and unregister
  if(isEofOrSpuriousReady()) {
    if(m_notifier) {
      m_notifier->setEnabled(false);
    }
    closeAndUnregister();
    return;
  }
}


qint64 RDUnixSocket::writeWithErrorCheck(const QByteArray &data)
{
  // Check socket state before writing to prevent CPU spinning on broken connections
  if(state()!=QAbstractSocket::ConnectedState) {
    return -1;
  }
  
  // Check for socket errors
  if(error()!=QAbstractSocket::UnknownSocketError) {
    return -1;
  }
  
  // Check if socket is writable
  if(!isWritable()) {
    return -1;
  }
  
  // Attempt the write
  qint64 written=write(data);
  
  // If write failed or was partial, check error state
  if(written<data.size()) {
    if(error()!=QAbstractSocket::UnknownSocketError) {
      return -1;
    }
  }
  
  return written;
}
