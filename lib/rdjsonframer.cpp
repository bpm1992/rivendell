//   rdjsonframer.cpp
//
//   Frame an unsynchronized stream of JSON messages
//
//   (C) Copyright 2025 Fred Gleason <fredg@paravelsystems.com>
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU Library General Public License 
//   version 2 as published by the Free Software Foundation.
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

#include <unistd.h>

#include "rdjsonframer.h"

RDJsonFramer::RDJsonFramer(QTcpSocket *in_sock,QObject *parent)
  : QObject(parent)
{
  d_socket=in_sock;
  d_empty_ready_count=0;
  connect(d_socket,SIGNAL(readyRead()),this,SLOT(readyReadData()));
  connect(d_socket,SIGNAL(disconnected()),this,SLOT(disconnectedData()));
}


RDJsonFramer::RDJsonFramer(QObject *parent)
  : QObject(parent)
{
  d_socket=NULL;
}


RDJsonFramer::~RDJsonFramer()
{
  if(d_socket!=NULL) {
    delete d_socket;
  }
}


QByteArray RDJsonFramer::currentDocument() const
{
  return d_current_document;
}


QIODevice *RDJsonFramer::ioDevice() const
{
  return d_socket;
}


void RDJsonFramer::write(const QByteArray &data)
{
  d_current_document=data;
  emit documentReceived(d_current_document);
}


void RDJsonFramer::reset()
{
  emit documentReset();  
}


void RDJsonFramer::readyReadData()
{
  // Check if data is actually available first (most common spin loop cause)
  if(d_socket->bytesAvailable()==0) {
    d_empty_ready_count++;
    if(d_empty_ready_count>=10) {
      // Spin loop detected - IMMEDIATELY disconnect the signal to stop CPU spin
      disconnect(d_socket,SIGNAL(readyRead()),this,SLOT(readyReadData()));
      // Force close the socket descriptor to stop QSocketNotifier from firing
      int fd=d_socket->socketDescriptor();
      if(fd>=0) {
        d_socket->setSocketDescriptor(-1);  // Unregister from Qt
        ::close(fd);  // Close the actual file descriptor
      }
      d_socket->abort();  // Clean up socket state
    }
    return;
  }
  
  // Reset counter on data available
  d_empty_ready_count=0;
  
  // Validate socket state
  if(d_socket->state()!=QAbstractSocket::ConnectedState) {
    // Not connected - disconnect signal and close FD to prevent further events
    disconnect(d_socket,SIGNAL(readyRead()),this,SLOT(readyReadData()));
    int fd=d_socket->socketDescriptor();
    if(fd>=0) {
      d_socket->setSocketDescriptor(-1);
      ::close(fd);
    }
    d_socket->abort();
    return;
  }
  
  // Check for socket errors
  if(d_socket->error()!=QAbstractSocket::UnknownSocketError) {
    // Socket has an error - disconnect signal and close FD immediately
    disconnect(d_socket,SIGNAL(readyRead()),this,SLOT(readyReadData()));
    int fd=d_socket->socketDescriptor();
    if(fd>=0) {
      d_socket->setSocketDescriptor(-1);
      ::close(fd);
    }
    d_socket->abort();
    return;
  }
  
  write(d_socket->readAll());
}

void RDJsonFramer::disconnectedData()
{
  // Socket disconnected - disconnect readyRead signal and close FD to prevent any further events
  disconnect(d_socket,SIGNAL(readyRead()),this,SLOT(readyReadData()));
  // Close the file descriptor to stop QSocketNotifier
  int fd=d_socket->socketDescriptor();
  if(fd>=0) {
    d_socket->setSocketDescriptor(-1);
    ::close(fd);
  }
  d_empty_ready_count=0;
}
