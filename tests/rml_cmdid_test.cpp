// rml_cmdid_test.cpp
//
// Unit tests for the line-ID RML commands and command IDs (fork addition):
// parsing, round trips, and that older commands and free text are
// untouched.  No database or daemons needed.
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

#include <QCoreApplication>

#include <rdmacro.h>

static int g_checks=0;
static int g_failures=0;

static void Check(bool cond,const QString &what)
{
  g_checks++;
  if(!cond) {
    g_failures++;
    printf("FAIL: %s\n",what.toUtf8().constData());
  }
}


static void Parses(const QString &str,RDMacro::Command cmd,int args,
		   const QString &id,const QString &back)
{
  RDMacro m=RDMacro::fromString(str);
  Check(m.command()==cmd,str+": command");
  Check(m.argQuantity()==args,
	str+QString::asprintf(": %d arguments, expected %d",
			      m.argQuantity(),args));
  Check(m.commandId()==id,str+": command ID '"+m.commandId()+"'");
  Check(m.toString()==back,str+": round trip gave '"+m.toString()+"'");
}


int main(int argc,char *argv[])
{
  QCoreApplication a(argc,argv);

  //
  // The new verbs, with and without IDs
  //
  Parses("LP 1 1187!",RDMacro::LP,2,"","LP 1 1187!");
  Parses("LP 1 1187 #c7f3a1!",RDMacro::LP,2,"c7f3a1","LP 1 1187 #c7f3a1!");
  Parses("LN 2 55 #a-b_C9!",RDMacro::LN,2,"a-b_C9","LN 2 55 #a-b_C9!");
  Parses("LI 1 -1 65003 segue #x1!",RDMacro::LI,4,"x1",
	 "LI 1 -1 65003 segue #x1!");
  Parses("LI 101 7 65003!",RDMacro::LI,3,"","LI 101 7 65003!");
  Parses("LV 1 10 20 #mv!",RDMacro::LV,3,"mv","LV 1 10 20 #mv!");
  Parses("LD 3 99!",RDMacro::LD,2,"","LD 3 99!");
  Parses("PA 1 #p!",RDMacro::PA,1,"p","PA 1 #p!");
  Parses("PR 1!",RDMacro::PR,1,"","PR 1!");

  //
  // Older log commands take an ID too; without one they are
  // exactly as before
  //
  Parses("LL 1 TEST_LOG 3 #id9!",RDMacro::LL,3,"id9","LL 1 TEST_LOG 3 #id9!");
  Parses("LL 1 TEST_LOG 3!",RDMacro::LL,3,"","LL 1 TEST_LOG 3!");
  Parses("PN 1 #n!",RDMacro::PN,1,"n","PN 1 #n!");
  Parses("PM 2 1 #m!",RDMacro::PM,2,"m","PM 2 1 #m!");
  Parses("PX 1 65003 0 play #q!",RDMacro::PX,4,"q","PX 1 65003 0 play #q!");

  //
  // Commands that take free text never lose a word that looks like an ID
  //
  Parses("LB Hello #5!",RDMacro::LB,2,"","LB Hello #5!");
  Parses("RN echo #abc!",RDMacro::RN,2,"","RN echo #abc!");

  //
  // Not an ID: stays an argument, so the command's own checks refuse it
  //
  Parses("LN 1 55 #!",RDMacro::LN,3,"","LN 1 55 #!");
  Parses("LN 1 55 #bad!id!",RDMacro::LN,3,"","LN 1 55 #bad!id!");
  Parses("LN 1 55 #has.dot!",RDMacro::LN,3,"","LN 1 55 #has.dot!");
  Parses("LN 1 55 #123456789012345678901234567890123!",RDMacro::LN,3,"",
	 "LN 1 55 #123456789012345678901234567890123!");
  Parses("LN 1 55 #12345678901234567890123456789012!",RDMacro::LN,2,
	 "12345678901234567890123456789012",
	 "LN 1 55 #12345678901234567890123456789012!");

  //
  // A lone ID with no other arguments is an argument, not an ID
  //
  Parses("PR #only!",RDMacro::PR,1,"","PR #only!");

  //
  // Replies keep '+' or '-' last
  //
  RDMacro m=RDMacro::fromString("LN 1 55 #r1!");
  m.acknowledge(true);
  Check(m.toString()=="LN 1 55 + #r1!"||m.toString()=="LN 1 55 #r1 +!",
	"acknowledged: '"+m.toString()+"'");

  //
  // Verb codes
  //
  Check(RDMacro::LP==0x4C50&&RDMacro::LN==0x4C4E&&RDMacro::LI==0x4C49&&
	RDMacro::LV==0x4C56&&RDMacro::LD==0x4C44&&RDMacro::PA==0x5041&&
	RDMacro::PR==0x5052,"verb codes");
  Check(RDMacro::takesCommandId(RDMacro::LP)&&
	(!RDMacro::takesCommandId(RDMacro::LB))&&
	(!RDMacro::takesCommandId(RDMacro::RN)),"takesCommandId()");

  printf("rml_cmdid_test: %d checks, %d failures\n",g_checks,g_failures);
  printf("rml_cmdid_test: %s\n",(g_failures==0)?"PASS":"FAIL");
  return (g_failures==0)?0:1;
}
