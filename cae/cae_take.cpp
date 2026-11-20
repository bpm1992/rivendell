// cae_take.cpp
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

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "cae_take.h"

CaeTake::CaeTake(int owner,const QString &name,RDTempDirectory *dir,
		 const QString &wav_path,unsigned len_ms)
{
  take_owner=owner;
  take_name=name;
  take_dir=dir;
  take_wav_path=wav_path;
  take_len_ms=len_ms;
}


CaeTake::CaeTake(int owner,const QString &name,const QString &stale_dir,
		 const QString &wav_path)
{
  //
  // A take found on startup in the directory of an earlier caed
  //
  take_owner=owner;
  take_name=name;
  take_dir=NULL;
  take_stale_dir=stale_dir;
  take_wav_path=wav_path;
  take_len_ms=0;
}


CaeTake::~CaeTake()
{
  //
  // RDTempDirectory's destructor removes the take and its directory
  //
  delete take_dir;
  if(!take_stale_dir.isEmpty()) {
    QDir dir(take_stale_dir);
    QStringList files=dir.entryList(QDir::Files|QDir::Hidden);
    for(int i=0;i<files.size();i++) {
      dir.remove(files.at(i));
    }
    dir.rmdir(take_stale_dir);
  }
}


int CaeTake::owner() const
{
  return take_owner;
}


void CaeTake::setOwner(int owner)
{
  take_owner=owner;
}


QString CaeTake::name() const
{
  return take_name;
}


QString CaeTake::wavPath() const
{
  return take_wav_path;
}


unsigned CaeTake::lengthMs() const
{
  return take_len_ms;
}


QString CaeTake::journalPath() const
{
  return journalPath(QFileInfo(take_wav_path).absolutePath());
}


QString CaeTake::journalPath(const QString &dir)
{
  return dir+"/finalize";
}


CaeFinalizer::CaeFinalizer(int requester,CaeTake *take,const QString &dest,
			   QObject *parent)
  : QThread(parent)
{
  fin_requester=requester;
  fin_take=take;
  fin_dest=dest;
  fin_succeeded=false;
}


CaeFinalizer::~CaeFinalizer()
{
  delete fin_take;
}


int CaeFinalizer::requester() const
{
  return fin_requester;
}


void CaeFinalizer::setRequester(int requester)
{
  fin_requester=requester;
}


CaeTake *CaeFinalizer::take() const
{
  return fin_take;
}


CaeTake *CaeFinalizer::releaseTake()
{
  CaeTake *ret=fin_take;
  fin_take=NULL;
  return ret;
}


QString CaeFinalizer::destination() const
{
  return fin_dest;
}


bool CaeFinalizer::succeeded() const
{
  return fin_succeeded;
}


QString CaeFinalizer::errorString() const
{
  return fin_error;
}


void CaeFinalizer::run()
{
  //
  // The journal: take name, destination, take file, one per line.  The
  // next caed does the copy again if this one doesn't finish it.
  //
  QFile journal(fin_take->journalPath());
  if(journal.open(QIODevice::WriteOnly|QIODevice::Truncate)) {
    journal.write((fin_take->name()+"\n"+fin_dest+"\n"+
		   fin_take->wavPath()+"\n").toUtf8());
    journal.close();
  }

  QFile src(fin_take->wavPath());
  if(!src.open(QIODevice::ReadOnly)) {
    fin_error="unable to open "+fin_take->wavPath()+": "+src.errorString();
    return;
  }
  QFile dst(fin_dest);
  if(!dst.open(QIODevice::WriteOnly|QIODevice::Truncate)) {
    fin_error="unable to open "+fin_dest+": "+dst.errorString();
    return;
  }
  QByteArray buf;
  while(!(buf=src.read(65536)).isEmpty()) {
    if(dst.write(buf)!=buf.size()) {
      fin_error="unable to write "+fin_dest+": "+dst.errorString();
      return;
    }
  }
  if(src.error()!=QFileDevice::NoError) {
    fin_error="unable to read "+fin_take->wavPath()+": "+src.errorString();
    return;
  }
  if(!dst.flush()) {
    fin_error="unable to write "+fin_dest+": "+dst.errorString();
    return;
  }
  dst.close();
  if(dst.error()!=QFileDevice::NoError) {
    fin_error="unable to close "+fin_dest+": "+dst.errorString();
    return;
  }
  QFile::remove(fin_take->journalPath());
  fin_succeeded=true;
}
