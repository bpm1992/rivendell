// cae_take.h
//
// A finished recording held by caed(8) until its owner finalizes or
// discards it, and the thread that copies it to its destination.
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

#ifndef CAE_TAKE_H
#define CAE_TAKE_H

#include <QString>
#include <QThread>

#include <rdtempdirectory.h>

//
// A take: the finished WAV (with its levl chunk) in caed's temp
// directory.  Nothing is written to the audio store until the owner
// finalizes it.
//
class CaeTake
{
 public:
  CaeTake(int owner,const QString &name,RDTempDirectory *dir,
	  const QString &wav_path,unsigned len_ms);
  CaeTake(int owner,const QString &name,const QString &stale_dir,
	  const QString &wav_path);
  ~CaeTake();
  int owner() const;
  void setOwner(int owner);
  QString name() const;
  QString wavPath() const;
  unsigned lengthMs() const;
  QString journalPath() const;
  static QString journalPath(const QString &dir);

 private:
  int take_owner;
  QString take_name;
  RDTempDirectory *take_dir;
  QString take_stale_dir;
  QString take_wav_path;
  unsigned take_len_ms;
};


//
// Copies a take to its destination off caed's main loop.  The audio
// store may be slow (a network or object-storage filesystem), and caed
// must keep serving every other client meanwhile.  The copy is
// sequential, with no rename() or link(), which such filesystems may
// not support.  So that a copy cut short (caed stopped or killed) can be
// done again by the next caed, a journal naming the take and its
// destination sits in the take's directory until the copy is done.
//
class CaeFinalizer : public QThread
{
  Q_OBJECT
 public:
  CaeFinalizer(int requester,CaeTake *take,const QString &dest,
	       QObject *parent=0);
  ~CaeFinalizer();
  int requester() const;
  void setRequester(int requester);
  CaeTake *take() const;
  CaeTake *releaseTake();
  QString destination() const;
  bool succeeded() const;
  QString errorString() const;

 protected:
  void run();

 private:
  int fin_requester;
  CaeTake *fin_take;
  QString fin_dest;
  bool fin_succeeded;
  QString fin_error;
};


#endif  // CAE_TAKE_H
