// rdmacro.cpp
//
// A container class for a Rivendell Macro Language Command
//
//   (C) Copyright 2002-2021 Fred Gleason <fredg@paravelsystems.com>
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

#include <ctype.h>
#include <stdint.h>

#include "rdmacro.h"

RDMacro::RDMacro()
{
  clear();
}


RDMacro::Role RDMacro::role() const
{
  return rml_role;
}


void RDMacro::setRole(RDMacro::Role role)
{
  rml_role=role;
}


RDMacro::Command RDMacro::command() const
{
  return rml_cmd;
}


void RDMacro::setCommand(RDMacro::Command cmd)
{
  rml_cmd=cmd;
}


void RDMacro::setCommand(const QString &str)
{
  RDMacro::Command cmd=RDMacro::NN;
  cmd=(RDMacro::Command)((str.at(0).toLatin1()<<8)+str.at(1).toLatin1());
  switch(cmd) {
  case RDMacro::AG:
  case RDMacro::AL:
  case RDMacro::BO:
  case RDMacro::CC:
  case RDMacro::CE:
  case RDMacro::CL:
  case RDMacro::CP:
  case RDMacro::DL:
  case RDMacro::DP:
  case RDMacro::DS:
  case RDMacro::DX:
  case RDMacro::EX:
  case RDMacro::FS:
  case RDMacro::GE:
  case RDMacro::GI:
  case RDMacro::GO:
  case RDMacro::JC:
  case RDMacro::JD:
  case RDMacro::JZ:
  case RDMacro::LB:
  case RDMacro::LC:
  case RDMacro::LL:
  case RDMacro::LM:
  case RDMacro::LO:
  case RDMacro::MB:
  case RDMacro::MD:
  case RDMacro::MN:
  case RDMacro::MT:
  case RDMacro::MX:
  case RDMacro::NN:
  case RDMacro::PB:
  case RDMacro::PC:
  case RDMacro::PE:
  case RDMacro::PL:
  case RDMacro::PM:
  case RDMacro::PN:
  case RDMacro::PP:
  case RDMacro::PS:
  case RDMacro::PT:
  case RDMacro::PU:
  case RDMacro::PW:
  case RDMacro::PX:
  case RDMacro::RL:
  case RDMacro::RS:
  case RDMacro::RR:
  case RDMacro::RN:
  case RDMacro::SN:
  case RDMacro::ST:
  case RDMacro::SA:
  case RDMacro::SC:
  case RDMacro::SD:
  case RDMacro::SG:
  case RDMacro::SI:
  case RDMacro::SO:
  case RDMacro::SP:
  case RDMacro::SR:
  case RDMacro::SL:
  case RDMacro::SX:
  case RDMacro::SY:
  case RDMacro::SZ:
  case RDMacro::TA:
  case RDMacro::UO:
  case RDMacro::PD:
  case RDMacro::LD:
  case RDMacro::LI:
  case RDMacro::LN:
  case RDMacro::LP:
  case RDMacro::LV:
  case RDMacro::PA:
  case RDMacro::PR:
    setCommand(cmd);
    break;

  default:
    setCommand(RDMacro::NN);
    break;
  }
}


QHostAddress RDMacro::address() const
{
  return rml_addr;
}


void RDMacro::setAddress(QHostAddress addr)
{
  rml_addr=addr;
}


uint16_t RDMacro::port() const
{
  return rml_port;
}


void RDMacro::setPort(uint16_t port)
{
  rml_port=port;
}


bool RDMacro::echoRequested() const
{
  return rml_echo_requested;
}


void RDMacro::setEchoRequested(bool state)
{
  rml_echo_requested=state;
}


QString RDMacro::commandId() const
{
  return rml_command_id;
}


void RDMacro::setCommandId(const QString &id)
{
  rml_command_id=id;
}


QString RDMacro::arg(int n) const
{
  return rml_args[n];
}


int RDMacro::addArg(const QString &arg)
{
  rml_args.push_back(arg);
  return rml_args.size()-1;
}


int RDMacro::addArg(int arg)
{
  rml_args.push_back(QString::asprintf("%d",arg));
  return rml_args.size()-1;
}


int RDMacro::addArg(unsigned arg)
{
  rml_args.push_back(QString::asprintf("%u",arg));
  return rml_args.size()-1;
}


void RDMacro::setArg(int n,const QString &arg)
{
  rml_args[n]=arg;
}


void RDMacro::setArg(int n,int arg)
{
  rml_args[n]=QString::asprintf("%d",arg);
}


void RDMacro::setArg(int n,unsigned arg)
{
  rml_args[n]=QString::asprintf("%u",arg);
}


int RDMacro::argQuantity() const
{
  return rml_args.size();
}


void RDMacro::acknowledge(bool state)
{
  rml_role=RDMacro::Reply;
  if(state) {
    rml_args.push_back("+");
  }
  else {
    rml_args.push_back("-");
  }
}


QString RDMacro::rollupArgs(int n)
{
  return rml_args.join(" ");
}


QString RDMacro::toString() const
{
  QString ret=QChar(((uint16_t)rml_cmd)>>8);
  ret+=QChar(0xFF&((uint16_t)rml_cmd));
  ret+=" ";
  ret+=rml_args.join(" ");
  if(!rml_command_id.isEmpty()) {
    ret+=" #"+rml_command_id;
  }
  ret+="!";

  return ret;
}


unsigned RDMacro::length() const
{
  if((rml_cmd==RDMacro::SP)&&(rml_args.size()==1)) {
    return rml_args[0].toUInt();
  }
  return 0;
}


bool RDMacro::isNull() const
{
  return rml_cmd==RDMacro::NN;
}


void RDMacro::clear()
{
  rml_role=RDMacro::Invalid;
  rml_addr=QHostAddress();
  rml_port=RD_RML_NOECHO_PORT;
  rml_echo_requested=false;
  rml_command_id="";
  rml_cmd=RDMacro::NN;
  rml_args.clear();
}


RDMacro RDMacro::fromString(const QString &str,RDMacro::Role role)
{
  RDMacro ret;
  RDMacro::Command cmd=RDMacro::NN;

  ret.setRole(role);

  //
  // Check for bang
  //
  QString str2=str.trimmed();
  if(str2.right(1)!="!") {
    ret.setCommand(RDMacro::NN);
    return ret;
  }

  //
  // Get Command
  //
  QStringList f0=str2.left(str2.length()-1).trimmed().split(" ");
  if(f0[0].length()!=2) {
    ret.setCommand(RDMacro::NN);
    return ret;
  }
  cmd=(RDMacro::Command)((f0[0].at(0).toLatin1()<<8)+f0[0].at(1).toLatin1());
  switch(cmd) {
  case RDMacro::AG:
  case RDMacro::AL:
  case RDMacro::BO:
  case RDMacro::CC:
  case RDMacro::CE:
  case RDMacro::CL:
  case RDMacro::CP:
  case RDMacro::DL:
  case RDMacro::DP:
  case RDMacro::DS:
  case RDMacro::DX:
  case RDMacro::EX:
  case RDMacro::FS:
  case RDMacro::GE:
  case RDMacro::GI:
  case RDMacro::GO:
  case RDMacro::JC:
  case RDMacro::JD:
  case RDMacro::JZ:
  case RDMacro::LB:
  case RDMacro::LC:
  case RDMacro::LL:
  case RDMacro::LM:
  case RDMacro::LO:
  case RDMacro::MB:
  case RDMacro::MD:
  case RDMacro::MN:
  case RDMacro::MT:
  case RDMacro::MX:
  case RDMacro::NN:
  case RDMacro::PB:
  case RDMacro::PC:
  case RDMacro::PE:
  case RDMacro::PL:
  case RDMacro::PM:
  case RDMacro::PN:
  case RDMacro::PP:
  case RDMacro::PS:
  case RDMacro::PT:
  case RDMacro::PU:
  case RDMacro::PW:
  case RDMacro::PX:
  case RDMacro::RL:
  case RDMacro::RS:
  case RDMacro::RR:
  case RDMacro::RN:
  case RDMacro::SN:
  case RDMacro::ST:
  case RDMacro::SA:
  case RDMacro::SC:
  case RDMacro::SD:
  case RDMacro::SG:
  case RDMacro::SI:
  case RDMacro::SO:
  case RDMacro::SP:
  case RDMacro::SR:
  case RDMacro::SL:
  case RDMacro::SX:
  case RDMacro::SY:
  case RDMacro::SZ:
  case RDMacro::TA:
  case RDMacro::UO:
  case RDMacro::PD:
  case RDMacro::LD:
  case RDMacro::LI:
  case RDMacro::LN:
  case RDMacro::LP:
  case RDMacro::LV:
  case RDMacro::PA:
  case RDMacro::PR:
    ret.setCommand(cmd);
    break;
	
  default:
    ret.setCommand(RDMacro::NN);
    return ret;
  }

  //
  // Get Arguments
  //
  // A trailing '#<id>' is a command ID (fork addition), for the commands
  // that take one: it is kept apart from the arguments, so the command's
  // own argument checks are unchanged.  Commands that take free text
  // (labels, command lines) never have one.
  //
  int argc=f0.size();
  if((argc>2)&&takesCommandId(ret.command())&&
     f0.last().startsWith("#")&&isValidCommandId(f0.last().mid(1))) {
    ret.setCommandId(f0.last().mid(1));
    argc--;
  }
  for(int i=1;i<argc;i++) {
    ret.addArg(f0[i]);
  }

  return ret;
}


bool RDMacro::takesCommandId(RDMacro::Command cmd)
{
  switch(cmd) {
  case RDMacro::LD:
  case RDMacro::LI:
  case RDMacro::LN:
  case RDMacro::LP:
  case RDMacro::LV:
  case RDMacro::PA:
  case RDMacro::PR:
  case RDMacro::LL:
  case RDMacro::AL:
  case RDMacro::MN:
  case RDMacro::PL:
  case RDMacro::PM:
  case RDMacro::PN:
  case RDMacro::PS:
  case RDMacro::PX:
  case RDMacro::RL:
  case RDMacro::MD:
    return true;

  default:
    break;
  }
  return false;
}


bool RDMacro::isValidCommandId(const QString &id)
{
  if((id.length()<1)||(id.length()>32)) {
    return false;
  }
  for(int i=0;i<id.length();i++) {
    QChar c=id.at(i);
    if(!(((c>='a')&&(c<='z'))||((c>='A')&&(c<='Z'))||((c>='0')&&(c<='9'))||
	 (c=='-')||(c=='_'))) {
      return false;
    }
  }
  return true;
}
