// repeater.h
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

#ifndef REPEATER_H
#define REPEATER_H

#include <QMap>
#include <QObject>
#include <QSignalMapper>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>

#include <rdjsonframer.h>
#include <rdunixserver.h>

#include "repeater.h"

class Repeater : public QObject
{
  Q_OBJECT
 public:
  Repeater(const QString &src_unix_addr,uint16_t serv_port,QObject *parent=0);
  QString sourceUnixAddress() const;
  uint16_t serverPort() const;
  static const int MAX_CLIENT_CONNECTIONS=100;
  static const int MAX_SOURCE_CONNECTIONS=50;  // Support rdvairplayd (20 machines) + rdairplay (3 machines) + headroom
  static const int IDLE_TIMEOUT_MS=300000;  // 5 minutes
  static const qint64 MAX_WRITE_BUFFER_SIZE=1048576;  // 1MB per client
  static const int CIRCUIT_BREAKER_ERROR_THRESHOLD=5;  // Errors before disconnect
  static const int CIRCUIT_BREAKER_RESET_MS=60000;  // 1 minute cooldown

 private slots:
  void newClientConnectionData();
  void newSourceConnectionData();
  void sendUpdate(const QByteArray &jdoc);
  void checkIdleConnections();

 private:
  struct ClientErrorTracking {
    int error_count;
    QDateTime last_error_time;
    ClientErrorTracking() : error_count(0) {}
  };
  void clientDisconnected(int id);
  void clientReadyReadData(int id);
  void sourceDisconnected(int id);
  void trackClientError(int id);
  bool isClientInErrorState(int id);
  uint16_t pad_server_port;
  QString pad_source_unix_address;
  QTcpServer *pad_client_server;
  QMap<int,QTcpSocket *> pad_client_sockets;
  QMap<int,QDateTime> pad_client_activity;
  QMap<int,ClientErrorTracking> pad_client_errors;
  RDUnixServer *pad_source_server;
  QMap<int,RDJsonFramer *> pad_framers;
  QTimer *pad_idle_timer;
};


#endif  // REPEATER_H
