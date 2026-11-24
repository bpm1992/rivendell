// mixminus_test.cpp
//
// Test the mix-minus output abstraction
//
//   (C) Copyright 2026 Fred Gleason <fredg@paravelsystems.com>
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

#include <QApplication>
#include <QLineEdit>

#include <rd.h>
#include <rdmixminus.h>
#include <rdonebasedspinbox.h>

#include "mixminus_test.h"

static int checks=0;
static int failures=0;

static void Check(bool ok,const QString &what)
{
  checks++;
  if(!ok) {
    failures++;
  }
  printf("%s %s\n",ok?"ok  ":"FAIL",what.toUtf8().constData());
}


static void CheckText(const QString &got,const QString &want,
		      const QString &what)
{
  Check(got==want,what+": \""+got+"\""+(got==want?"":(" (want \""+want+"\")")));
}


static RDMixMinus Make(int output,int source,QList<int> excluded,int level=0,
		       bool enabled=true)
{
  RDMixMinus mm;
  mm.setStationName("test");
  mm.setCard(0);
  mm.setOutput(output);
  mm.setSource(source);
  mm.setExcluded(excluded);
  mm.setLevel(level);
  mm.setEnabled(enabled);
  return mm;
}


static class SpinBoxProbe : public RDOneBasedSpinBox
{
 public:
  SpinBoxProbe() : RDOneBasedSpinBox(NULL) {}
  void Type(const QString &str)
  {
    lineEdit()->setText(str);
    interpretText();
  }
  QValidator::State Validate(const QString &str) const
  {
    QString s=str;
    int pos=0;
    return validate(s,pos);
  }
} *spin=NULL;


int main(int argc,char *argv[])
{
  setenv("QT_QPA_PLATFORM","offscreen",0);
  QApplication a(argc,argv);

  //
  // Stored form of the excluded inputs
  //
  CheckText(RDMixMinus::excludedText(QList<int>()<<3<<1<<1<<-1<<RD_MAX_PORTS),
	    "1,3","excludedText sorts, drops repeats and ports out of range");
  CheckText(RDMixMinus::excludedText(QList<int>()),"","excludedText of none");
  QStringList bad;
  QList<int> ports=RDMixMinus::parseExcluded(" 1 , x,3,3,99,",&bad);
  Check(ports==(QList<int>()<<1<<3),"parseExcluded reads \" 1 , x,3,3,99,\" as 1,3");
  Check(bad==(QStringList()<<"x"<<"99"),"parseExcluded reports x and 99");
  Check(RDMixMinus::parseExcluded("").size()==0,"parseExcluded of nothing");

  //
  // Names count from 1
  //
  RDMixMinus mm=Make(7,0,QList<int>()<<2<<1,-600);
  CheckText(mm.outputName(),"Output 8","Output 8 is port 7");
  CheckText(mm.sourceName(),"Full summed Output 1","the source of port 0");
  CheckText(mm.excludedNames(),"Input 2, Input 3","excluded ports 1 and 2");
  CheckText(mm.levelText(),"-6.0 dB","level -600");
  mm.setLevel(0);
  CheckText(mm.levelText(),"0.0 dB","level 0");
  mm.setLevel(250);
  CheckText(mm.levelText(),"+2.5 dB","level 250");
  mm.setLevel(RD_MUTE_DEPTH);
  CheckText(mm.levelText(),"Muted","level -10000");

  //
  // Validation
  //
  QList<RDMixMinus> none;
  Check(Make(7,0,QList<int>()<<1).validate(none,8,8).isEmpty(),
	"Output 8, Output 1 less Input 2: valid");
  Check(!Make(8,0,QList<int>()<<1).validate(none,8,8).isEmpty(),
	"Output 9 on an 8 output card: refused");
  Check(!Make(7,7,QList<int>()<<1).validate(none,8,8).isEmpty(),
	"its own sum: refused");
  Check(!Make(7,0,QList<int>()).validate(none,8,8).isEmpty(),
	"enabled, excluding nothing: refused");
  Check(Make(7,0,QList<int>(),0,false).validate(none,8,8).isEmpty(),
	"disabled, excluding nothing: valid");
  Check(!Make(7,0,QList<int>()<<8).validate(none,8,8).isEmpty(),
	"Input 9 on an 8 input card: refused");
  Check(!Make(7,0,QList<int>()<<1,1100).validate(none,8,8).isEmpty(),
	"+11 dB: refused");
  Check(Make(7,0,QList<int>()<<1,1000).validate(none,8,8).isEmpty(),
	"+10 dB: valid");
  QList<RDMixMinus> others;
  others.push_back(Make(6,0,QList<int>()<<2));
  Check(Make(7,0,QList<int>()<<1).validate(others,8,8).isEmpty(),
	"two mix-minus outputs on one source: valid");
  Check(!Make(6,0,QList<int>()<<1).validate(others,8,8).isEmpty(),
	"the same output twice: refused");
  Check(!Make(7,6,QList<int>()<<1).validate(others,8,8).isEmpty(),
	"a mix-minus of a mix-minus: refused");
  Check(Make(7,6,QList<int>()<<1,0,false).validate(others,8,8).isEmpty(),
	"disabled, sourced from a mix-minus: valid");
  others.clear();
  others.push_back(Make(6,7,QList<int>()<<2));
  Check(!Make(7,0,QList<int>()<<1).validate(others,8,8).isEmpty(),
	"an output that is another's source: refused");
  QString err=Make(7,7,QList<int>()<<1).validate(none,8,8);
  Check(!err.contains("7 ")&&!err.contains(" 7."),
	"messages never name a port counting from 0: \""+err+"\"");

  //
  // The spin box shows numbers counting from 1
  //
  spin=new SpinBoxProbe();
  spin->setRange(-1,7);
  spin->setSpecialValueText("None");
  spin->setValue(7);
  CheckText(spin->text(),"8","value 7 shows 8");
  spin->setValue(0);
  CheckText(spin->text(),"1","value 0 shows 1");
  spin->setValue(-1);
  CheckText(spin->text(),"None","value -1 shows None");
  spin->Type("8");
  Check(spin->value()==7,QString::asprintf("typing 8 stores 7 (%d)",spin->value()));
  Check(spin->Validate("8")==QValidator::Acceptable,"8 is acceptable");
  Check(spin->Validate("9")!=QValidator::Acceptable,"9 is not, with 8 ports");
  Check(spin->Validate("x")==QValidator::Invalid,"x is invalid");
  Check(spin->Validate("No")==QValidator::Intermediate,"No is on the way to None");
  delete spin;

  printf("%d checks, %d failed\n",checks,failures);
  return failures?1:0;
}
