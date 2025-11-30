// rdpadd.cpp
//
// Rivendell PAD Data Repeater
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

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include <QHostAddress>

#include <rd.h>

#include "repeater.h"

Repeater::Repeater(const QString &src_unix_addr,uint16_t serv_port,
		   QObject *parent)
  : QObject(parent)
{
  pad_source_unix_address=src_unix_addr;
  pad_server_port=serv_port;

  //
  // Client Server
  //

  pad_client_server=new QTcpServer(this);
  connect(pad_client_server,SIGNAL(newConnection()),
	  this,SLOT(newClientConnectionData()));
  if(!pad_client_server->listen(QHostAddress::Any,pad_server_port)) {
    syslog(LOG_ERR,"rdpadd: unable to bind client port %d",pad_server_port);
    exit(1);
  }
  syslog(LOG_INFO,"rdpadd: listening on TCP port %d for clients",pad_server_port);

  //
  // Source Server
  //

  pad_source_server=new RDUnixServer(this);
  connect(pad_source_server,SIGNAL(newConnection()),
	  this,SLOT(newSourceConnectionData()));
  if(!pad_source_server->listenToAbstract(pad_source_unix_address)) {
    syslog(LOG_ERR,"rdpadd: unable to bind source socket [%s]",
	    pad_source_server->errorString().toUtf8().constData());
    exit(1);
  }
  syslog(LOG_INFO,"rdpadd: listening on UNIX socket [%s]",
         pad_source_unix_address.toUtf8().constData());

  //
  // Idle Connection Timer
  //
  pad_idle_timer=new QTimer(this);
  connect(pad_idle_timer,SIGNAL(timeout()),this,SLOT(checkIdleConnections()));
  pad_idle_timer->start(60000);  // Check every minute
}


QString Repeater::sourceUnixAddress() const
{
  return pad_source_unix_address;
}


uint16_t Repeater::serverPort() const
{
  return pad_server_port;
}


void Repeater::newClientConnectionData()
{
  // Enforce connection limit to prevent resource exhaustion
  if(pad_client_sockets.size()>=MAX_CLIENT_CONNECTIONS) {
    QTcpSocket *sock=pad_client_server->nextPendingConnection();
    syslog(LOG_WARNING,"rdpadd: client connection limit (%d) reached, rejecting connection",
            MAX_CLIENT_CONNECTIONS);
    sock->write("ERROR: Too many connections\n");
    sock->disconnectFromHost();
    sock->deleteLater();
    return;
  }
  
  QTcpSocket *sock=pad_client_server->nextPendingConnection();
  int sock_id=sock->socketDescriptor();
  
  syslog(LOG_INFO,"rdpadd: new CLIENT connected, fd=%d",sock_id);
  
  // Use Qt5 lambda for disconnect signal - more efficient than QSignalMapper
  connect(sock,&QTcpSocket::disconnected,this,[this,sock_id]() {
    clientDisconnected(sock_id);
  });
  
  // Track client activity with lambda
  connect(sock,&QTcpSocket::readyRead,this,[this,sock_id]() {
    clientReadyReadData(sock_id);
  });
  
  pad_client_sockets[sock_id]=sock;
  pad_client_activity[sock_id]=QDateTime::currentDateTime();

  for(QMap<int,RDJsonFramer *>::const_iterator it=pad_framers.begin();
      it!=pad_framers.end();it++) {
    sock->write(it.value()->currentDocument());
  }
}


void Repeater::clientDisconnected(int id)
{
  QTcpSocket *sock=NULL;

  if((sock=pad_client_sockets.value(id))!=NULL) {
    sock->deleteLater();
    pad_client_sockets.remove(id);
    pad_client_activity.remove(id);
    pad_client_errors.remove(id);
  }
  else {
    syslog(LOG_WARNING,"rdpadd: unknown client connection %d attempted to close",id);
  }
}


void Repeater::newSourceConnectionData()
{
  // Enforce source connection limit
  if(pad_framers.size()>=MAX_SOURCE_CONNECTIONS) {
    QTcpSocket *sock=pad_source_server->nextPendingConnection();
    syslog(LOG_WARNING,"rdpadd: source connection limit (%d) reached, rejecting connection",
            MAX_SOURCE_CONNECTIONS);
    if(sock!=NULL) {
      sock->disconnectFromHost();
      sock->deleteLater();
    }
    return;
  }
  
  QTcpSocket *sock=pad_source_server->nextPendingConnection();
  if(sock==NULL) {
    syslog(LOG_ERR,"rdpadd: UNIX socket error [%s]",
	    (const char *)pad_source_server->errorString().toUtf8());
    exit(1);
  }
  
  int sock_id=sock->socketDescriptor();
  
  syslog(LOG_INFO,"rdpadd: new SOURCE connected, fd=%d",sock_id);
  
  // Use Qt5 lambda for disconnect signal
  connect(sock,&QTcpSocket::disconnected,this,[this,sock_id]() {
    sourceDisconnected(sock_id);
  });

  RDJsonFramer *framer=new RDJsonFramer(sock,this);
  connect(framer,SIGNAL(documentReceived(const QByteArray &)),
	  this,SLOT(sendUpdate(const QByteArray &)));
  pad_framers[sock->socketDescriptor()]=framer;
}


void Repeater::sourceDisconnected(int id)
{
  if(pad_framers.value(id)!=NULL) {
    pad_framers.value(id)->deleteLater();
    pad_framers.remove(id);
  }
  else {
    syslog(LOG_WARNING,"rdpadd: unknown source connection %d attempted to close",id);
  }
}


void Repeater::sendUpdate(const QByteArray &jdoc)
{
  QList<int> failed_sockets;
  
  for(QMap<int,QTcpSocket *>::const_iterator it=pad_client_sockets.begin();
      it!=pad_client_sockets.end();it++) {
    QTcpSocket *sock=it.value();
    int sock_id=it.key();
    
    // Circuit breaker - check if client is in error state
    if(isClientInErrorState(sock_id)) {
      syslog(LOG_WARNING,"rdpadd: client %d in circuit breaker error state, disconnecting",
              sock_id);
      failed_sockets.append(sock_id);
      continue;
    }
    
    // Validate socket state before writing
    if(sock->state()!=QAbstractSocket::ConnectedState) {
      trackClientError(sock_id);
      failed_sockets.append(sock_id);
      continue;
    }
    
    // Check for socket errors
    if(sock->error()!=QAbstractSocket::UnknownSocketError) {
      syslog(LOG_WARNING,"rdpadd: client socket %d has error [%s], disconnecting",
              sock_id,sock->errorString().toUtf8().constData());
      trackClientError(sock_id);
      failed_sockets.append(sock_id);
      continue;
    }
    
    // Implement backpressure - check write buffer size
    if(sock->bytesToWrite()>MAX_WRITE_BUFFER_SIZE) {
      syslog(LOG_WARNING,"rdpadd: client %d write buffer full (%lld bytes), disconnecting slow client",
              sock_id,(long long)sock->bytesToWrite());
      trackClientError(sock_id);
      failed_sockets.append(sock_id);
      continue;
    }
    
    // Attempt write and check for errors
    qint64 written=sock->write(jdoc);
    if(written==-1) {
      syslog(LOG_ERR,"rdpadd: write error to client %d [%s]",
              sock_id,sock->errorString().toUtf8().constData());
      trackClientError(sock_id);
      failed_sockets.append(sock_id);
    }
    else if(written<jdoc.size()) {
      // Partial write - track as potential issue
      syslog(LOG_WARNING,"rdpadd: partial write to client %d (%lld of %d bytes)",
              sock_id,(long long)written,jdoc.size());
    }
  }
  
  // Clean up failed connections
  for(QList<int>::const_iterator it=failed_sockets.begin();
      it!=failed_sockets.end();it++) {
    clientDisconnected(*it);
  }
}


void Repeater::checkIdleConnections()
{
  QDateTime now=QDateTime::currentDateTime();
  QList<int> idle_sockets;
  
  for(QMap<int,QDateTime>::const_iterator it=pad_client_activity.begin();
      it!=pad_client_activity.end();it++) {
    qint64 idle_ms=it.value().msecsTo(now);
    if(idle_ms>IDLE_TIMEOUT_MS) {
      idle_sockets.append(it.key());
    }
  }
  
  // Clean up idle connections
  for(QList<int>::const_iterator it=idle_sockets.begin();
      it!=idle_sockets.end();it++) {
    QTcpSocket *sock=pad_client_sockets.value(*it);
    if(sock!=NULL) {
      sock->disconnectFromHost();
    }
  }
}


void Repeater::clientReadyReadData(int id)
{
  // Update activity timestamp when client sends data
  // (Even though we don't expect clients to send data, this tracks any activity)
  pad_client_activity[id]=QDateTime::currentDateTime();
  
  // Drain any data the client might have sent (not expected in this protocol)
  QTcpSocket *sock=pad_client_sockets.value(id);
  if(sock!=NULL) {
    sock->readAll();
  }
}


void Repeater::trackClientError(int id)
{
  QDateTime now=QDateTime::currentDateTime();
  ClientErrorTracking &tracking=pad_client_errors[id];
  
  // Reset error count if last error was more than cooldown period ago
  if(tracking.last_error_time.isValid() &&
     tracking.last_error_time.msecsTo(now)>CIRCUIT_BREAKER_RESET_MS) {
    tracking.error_count=0;
  }
  
  tracking.error_count++;
  tracking.last_error_time=now;
  
  if(tracking.error_count>=CIRCUIT_BREAKER_ERROR_THRESHOLD) {
    syslog(LOG_WARNING,"rdpadd: client %d reached error threshold (%d errors), circuit breaker activated",
            id,tracking.error_count);
  }
}


bool Repeater::isClientInErrorState(int id)
{
  ClientErrorTracking &tracking=pad_client_errors[id];
  
  if(tracking.error_count>=CIRCUIT_BREAKER_ERROR_THRESHOLD) {
    // Check if we should reset the circuit breaker
    QDateTime now=QDateTime::currentDateTime();
    if(tracking.last_error_time.isValid() &&
       tracking.last_error_time.msecsTo(now)>CIRCUIT_BREAKER_RESET_MS) {
      // Reset after cooldown period
      tracking.error_count=0;
      syslog(LOG_INFO,"rdpadd: client %d circuit breaker reset after cooldown",id);
      return false;
    }
    return true;
  }
  
  return false;
}
