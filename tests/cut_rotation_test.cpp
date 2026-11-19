// cut_rotation_test.cpp
//
// Unit test for RDCutCache rotation logic (weighting vs specific order).
//
//   (C) Copyright 2025 Fred Gleason <fredg@paravelsystems.com>
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
//   You should have received a copy of the GNU General Public License
//   along with this program; if not, write to the Free Software
//   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <QCoreApplication>
#include <QDateTime>
#include <QRandomGenerator>
#include <QMap>
#include <QThread>

#include <cstdio>
#include <cstdlib>

#include <rdcut_cache.h>

#include "cut_rotation_test.h"

static int g_tests_run=0;
static int g_cuts_seen=0;
static const QTime kTestTime(10,0,0);
static const quint32 kRandomSeed=12345;

// Simple test helper to inject fictitious data into the cache without hitting DB
class RDCutCacheTestHelper
{
 public:
  static void Inject(RDCutCache *cache,unsigned cart,const QVector<RDCutData> &cuts)
  {
    cache->cuts_by_cart[cart]=cuts;
    for(const auto &c:cuts) {
      cache->cuts_by_name[c.cut_name]=c;
    }
  }
};

static void Fail(const QString &msg)
{
  fprintf(stderr,"FAIL: %s\n",msg.toUtf8().constData());
  exit(1);
}

static void Announce(const char *test_name)
{
  printf("TEST %s\n",test_name);
}

static QString DowString(const RDCutData &c)
{
  return QString::asprintf("dow:%d%d%d%d%d%d%d",
                           c.mon,c.tue,c.wed,c.thu,c.fri,c.sat,c.sun);
}

static void DumpCut(const RDCutData &c)
{
  printf("  cut=%s order=%d weight=%d evergreen=%d daypart=%s-%s %s\n",
         c.cut_name.toUtf8().constData(),
         c.play_order,
         c.weight,
         c.evergreen,
         c.start_daypart.toString("hh:mm:ss").toUtf8().constData(),
         c.end_daypart.toString("hh:mm:ss").toUtf8().constData(),
         DowString(c).toUtf8().constData());
}

static void DumpCuts(const QVector<RDCutData> &cuts)
{
  for(const auto &c:cuts) {
    DumpCut(c);
  }
}
static void RegisterCuts(const QVector<RDCutData> &cuts)
{
  g_cuts_seen+=cuts.size();
}

static void ExpectEq(const QString &label,const QString &lhs,const QString &rhs)
{
  if(lhs!=rhs) {
    Fail(QStringLiteral("%1 expected '%2' got '%3'").arg(label,lhs,rhs));
  }
}

static void TestSequentialOrder()
{
  RDCutCache cache;
  QVector<RDCutData> cuts;

  Announce("SequentialOrder");

  for(int i=0;i<5;i++) {
    RDCutData c;
    c.cut_name=QString::asprintf("000100_00%d",i+1);
    c.length=100;
    c.play_order=i+1;
    c.mon=c.tue=c.wed=c.thu=c.fri=c.sat=c.sun=true;
    cuts << c;
  }
  RDCutCacheTestHelper::Inject(&cache,100,cuts);
  RegisterCuts(cuts);
  DumpCuts(cuts);

  QStringList expected;
  expected << "000100_001" << "000100_002" << "000100_003" << "000100_004" << "000100_005";
  RDCutPlan plan;
  for(int i=0;i<expected.size();++i) {
    QString pick=cache.selectCut(100,RDCart::Sequence,false,kTestTime,&plan);
    plan.add(100,pick);
    printf("  pick[%d]=%s\n",i,pick.toUtf8().constData());
    ExpectEq(QStringLiteral("Sequential pick %1").arg(i+1),expected.at(i),pick);
    if(i+1<expected.size()) {
      QThread::msleep(5);  // ensure last_play_datetime advances between picks
    }
  }
  g_tests_run++;
}

static void TestWeightedRotation()
{
  RDCutCache cache;
  QVector<RDCutData> cuts;

  Announce("WeightedRotation");

  // Higher weights should be preferred when ratios are considered.
  QVector<int> weights{1,2,3,4,5};
  QVector<int> counters{10,4,3,2,0};  // Seed ratios so heavier cuts win
  for(int i=0;i<weights.size();++i) {
    RDCutData c;
    c.cut_name=QString::asprintf("000200_00%d",i+1);
    c.length=100;
    c.weight=weights[i];
    c.local_counter=counters[i];
    c.mon=c.tue=c.wed=c.thu=c.fri=c.sat=c.sun=true;
    cuts << c;
  }
  RDCutCacheTestHelper::Inject(&cache,200,cuts);
  RegisterCuts(cuts);
  DumpCuts(cuts);

  QVector<int> hit(weights.size(),0);
  const int draws=20;
  RDCutPlan plan;
  for(int i=0;i<draws;++i) {
    QString pick=cache.selectCut(200,RDCart::Sequence,true,kTestTime,&plan);
    plan.add(200,pick);
    printf("  draw[%d]=%s\n",i,pick.toUtf8().constData());
    int idx=pick.right(3).toInt()-1;  // 000200_00X -> X-1
    if(idx>=0 && idx<hit.size()) {
      hit[idx]++;
    }
    QThread::msleep(5);
  }

  printf("  hit counts (1..5 weights): %d %d %d %d %d\n",
         hit[0],hit[1],hit[2],hit[3],hit[4]);

  // Expect monotonic non-decreasing counts aligned with weights
  if(!((hit[4]>=hit[3])&&(hit[3]>=hit[2])&&(hit[2]>=hit[1])&&(hit[1]>=hit[0]))) {
    Fail(QStringLiteral("Weighted counts not ordered as expected: %1 %2 %3 %4 %5")
         .arg(hit[0]).arg(hit[1]).arg(hit[2]).arg(hit[3]).arg(hit[4]));
  }
  g_tests_run++;
}

static void TestEvergreenFallback()
{
  RDCutCache cache;
  QVector<RDCutData> cuts;

  Announce("EvergreenFallback");

  RDCutData invalid_dow; invalid_dow.cut_name="000400_001"; invalid_dow.length=100; invalid_dow.mon=false; // invalid DOW
  invalid_dow.evergreen=false;
  RDCutData invalid_daypart; invalid_daypart.cut_name="000400_002"; invalid_daypart.length=100; invalid_daypart.evergreen=false;
  invalid_daypart.mon=invalid_daypart.tue=invalid_daypart.wed=invalid_daypart.thu=invalid_daypart.fri=invalid_daypart.sat=invalid_daypart.sun=true;
  invalid_daypart.start_daypart=QTime(22,0,0); invalid_daypart.end_daypart=QTime(23,0,0);
  RDCutData evergreen; evergreen.cut_name="000400_003"; evergreen.length=100; evergreen.evergreen=true;
  evergreen.mon=evergreen.tue=evergreen.wed=evergreen.thu=evergreen.fri=evergreen.sat=evergreen.sun=true;
  RDCutData invalid_range; invalid_range.cut_name="000400_004"; invalid_range.length=100; invalid_range.mon=invalid_range.tue=invalid_range.wed=invalid_range.thu=invalid_range.fri=invalid_range.sat=invalid_range.sun=true;
  invalid_range.start_datetime=QDateTime::currentDateTime().addDays(1);
  invalid_range.end_datetime=QDateTime::currentDateTime().addDays(2);

  cuts << invalid_dow << invalid_daypart << evergreen << invalid_range;
  RDCutCacheTestHelper::Inject(&cache,400,cuts);
  RegisterCuts(cuts);
  DumpCuts(cuts);

  QString pick=cache.selectCut(400,RDCart::Sequence,false,kTestTime);
  printf("  selected=%s\n",pick.toUtf8().constData());
  ExpectEq("Evergreen fallback","000400_003",pick);
  g_tests_run++;
}

static void TestDaypartPick()
{
  RDCutCache cache;
  QVector<RDCutData> cuts;
  QTime now=kTestTime;

  Announce("DaypartPick");

  RDCutData in_window; in_window.cut_name="000500_001"; in_window.length=100;
  in_window.start_daypart=QTime(8,0,0); in_window.end_daypart=QTime(12,0,0);
  in_window.mon=in_window.tue=in_window.wed=in_window.thu=in_window.fri=in_window.sat=in_window.sun=true;

  RDCutData out_window; out_window.cut_name="000500_002"; out_window.length=100;
  out_window.start_daypart=QTime(12,0,0); out_window.end_daypart=QTime(13,0,0);
  out_window.mon=out_window.tue=out_window.wed=out_window.thu=out_window.fri=out_window.sat=out_window.sun=true;

  RDCutData boundary_low; boundary_low.cut_name="000500_003"; boundary_low.length=100;
  boundary_low.start_daypart=QTime(6,0,0); boundary_low.end_daypart=QTime(7,0,0);
  boundary_low.mon=boundary_low.tue=boundary_low.wed=boundary_low.thu=boundary_low.fri=boundary_low.sat=boundary_low.sun=true;

  RDCutData boundary_high; boundary_high.cut_name="000500_004"; boundary_high.length=100;
  boundary_high.start_daypart=QTime(18,0,0); boundary_high.end_daypart=QTime(19,0,0);
  boundary_high.mon=boundary_high.tue=boundary_high.wed=boundary_high.thu=boundary_high.fri=boundary_high.sat=boundary_high.sun=true;

  cuts << in_window << out_window << boundary_low << boundary_high;
  RDCutCacheTestHelper::Inject(&cache,500,cuts);
  RegisterCuts(cuts);
  DumpCuts(cuts);
  printf("  now=%s\n",now.toString("hh:mm:ss").toUtf8().constData());

  QString pick=cache.selectCut(500,RDCart::Sequence,false,now);
  printf("  selected=%s\n",pick.toUtf8().constData());
  ExpectEq("Daypart pick","000500_001",pick);
  g_tests_run++;
}

static void TestExpiredDateRange()
{
  RDCutCache cache;
  QVector<RDCutData> cuts;
  QDateTime now=QDateTime::currentDateTime();

  Announce("ExpiredDateRange");

  RDCutData expired1; expired1.cut_name="000600_001"; expired1.length=100;
  expired1.start_datetime=now.addDays(-10);
  expired1.end_datetime=now.addDays(-5);
  expired1.mon=expired1.tue=expired1.wed=expired1.thu=expired1.fri=expired1.sat=expired1.sun=true;

  RDCutData expired2; expired2.cut_name="000600_002"; expired2.length=100;
  expired2.start_datetime=now.addDays(-20);
  expired2.end_datetime=now.addDays(-15);
  expired2.mon=expired2.tue=expired2.wed=expired2.thu=expired2.fri=expired2.sat=expired2.sun=true;

  RDCutData future; future.cut_name="000600_003"; future.length=100;
  future.start_datetime=now.addDays(5);
  future.end_datetime=now.addDays(10);
  future.mon=future.tue=future.wed=future.thu=future.fri=future.sat=future.sun=true;

  RDCutData active; active.cut_name="000600_004"; active.length=100;
  active.start_datetime=now.addDays(-1);
  active.end_datetime=now.addDays(5);
  active.mon=active.tue=active.wed=active.thu=active.fri=active.sat=active.sun=true;

  cuts << expired1 << expired2 << future << active;
  RDCutCacheTestHelper::Inject(&cache,600,cuts);
  RegisterCuts(cuts);
  DumpCuts(cuts);
  printf("  now=%s\n",now.toString("yyyy-MM-dd hh:mm:ss").toUtf8().constData());

  QString pick=cache.selectCut(600,RDCart::Sequence,false,kTestTime);
  printf("  selected=%s\n",pick.toUtf8().constData());
  ExpectEq("Expired cut bypassed","000600_004",pick);
  g_tests_run++;
}

static void TestDayOfWeekSelection()
{
  RDCutCache cache;
  QVector<RDCutData> cuts;
  int dow=QDate::currentDate().dayOfWeek();

  Announce("DayOfWeekSelection");

  RDCutData good; good.cut_name="000700_001"; good.length=100;
  good.mon=good.tue=good.wed=good.thu=good.fri=good.sat=good.sun=false;
  switch(dow) {
    case 1: good.mon=true; break;
    case 2: good.tue=true; break;
    case 3: good.wed=true; break;
    case 4: good.thu=true; break;
    case 5: good.fri=true; break;
    case 6: good.sat=true; break;
    case 7: good.sun=true; break;
  }

  RDCutData bad; bad.cut_name="000700_002"; bad.length=100;
  bad.mon=bad.tue=bad.wed=bad.thu=bad.fri=bad.sat=bad.sun=false;

  RDCutData bad2; bad2.cut_name="000700_003"; bad2.length=100;
  bad2.mon=bad2.tue=bad2.wed=bad2.thu=bad2.fri=bad2.sat=bad2.sun=false;

  RDCutData evergreen; evergreen.cut_name="000700_004"; evergreen.length=100; evergreen.evergreen=true;
  evergreen.mon=evergreen.tue=evergreen.wed=evergreen.thu=evergreen.fri=evergreen.sat=evergreen.sun=true;

  cuts << good << bad << bad2 << evergreen;
  RDCutCacheTestHelper::Inject(&cache,700,cuts);
  RegisterCuts(cuts);
  DumpCuts(cuts);
  printf("  today_dow=%d\n",dow);

  QString pick=cache.selectCut(700,RDCart::Sequence,false,kTestTime);
  printf("  selected=%s\n",pick.toUtf8().constData());
  ExpectEq("Day of week selection","000700_001",pick);
  g_tests_run++;
}

//
// The repeat bug: five equal-weight cuts in one cart,
// one log with 167 lines of the cart, all planned before any plays.  Each cut
// must get its share and no cut may follow itself.
//
static QVector<RDRotationCut> EqualCuts(const QVector<int> &counters)
{
  QVector<RDRotationCut> ret;
  for(int i=0;i<counters.size();i++) {
    RDRotationCut c;
    c.cut_name=QString::asprintf("065003_%03d",i+1);
    c.cut_number=i+1;
    c.play_order=i+1;
    c.weight=1;
    c.local_counter=counters.at(i);
    ret.push_back(c);
  }
  return ret;
}

static void TestPlannedLogRotates()
{
  // Counters spread 107 plays apart, as the repeat bug leaves them.  The lagging cuts get more of the day, but every cut
  // comes round within the cart's total weight plus one picks.
  Announce("PlannedLogRotates");
  QVector<RDRotationCut> cuts=EqualCuts({34000,33945,33932,34032,34039});
  RDCutPlan plan;
  QMap<QString,int> hits;
  QMap<QString,int> seen;
  QString prev;
  int widest=0;
  for(int i=0;i<167;i++) {
    QString pick=RDCart::pickCut(cuts,RDCart::Sequence,true,&plan,65003);
    if(pick==prev) {
      Fail(QStringLiteral("line %1 repeats %2").arg(i).arg(pick));
    }
    if(seen.contains(pick)) {
      widest=qMax(widest,i-seen.value(pick));
    }
    seen[pick]=i;
    hits[pick]++;
    plan.add(65003,pick);
    prev=pick;
  }
  for(auto it=hits.begin();it!=hits.end();++it) {
    printf("  %s: %d\n",it.key().toUtf8().constData(),it.value());
  }
  printf("  widest gap between plays of one cut: %d\n",widest);
  if(hits.size()!=5) {
    Fail(QStringLiteral("only %1 cuts used").arg(hits.size()));
  }
  if(widest>6) {
    Fail(QStringLiteral("a cut waited %1 picks").arg(widest));
  }
  if(hits.value("065003_003")<=hits.value("065003_005")) {
    Fail("the cut furthest behind got no extra plays");
  }
  g_tests_run++;
}

static void TestOnAirRotates()
{
  // On air each pick plays: its counter and last play move, and the plan
  // is rebuilt from the log's lines so far, as RepickCuts() does.
  Announce("OnAirRotates");
  QVector<RDRotationCut> cuts=EqualCuts({34000,33945,33932,34032,34039});
  QStringList history;
  QMap<QString,int> hits;
  QMap<QString,int> seen;
  QDateTime t(QDate(2026,11,18),QTime(0,0,0));
  int widest=0;
  for(int i=0;i<167;i++) {
    RDCutPlan plan;
    for(int j=0;j<history.size();j++) {
      plan.setLast(65003,history.at(j));
    }
    QString pick=RDCart::pickCut(cuts,RDCart::Sequence,true,&plan,65003);
    if((!history.isEmpty())&&(pick==history.last())) {
      Fail(QStringLiteral("play %1 repeats %2").arg(i).arg(pick));
    }
    if(seen.contains(pick)) {
      widest=qMax(widest,i-seen.value(pick));
    }
    seen[pick]=i;
    hits[pick]++;
    history.push_back(pick);
    t=t.addSecs(520);
    for(int j=0;j<cuts.size();j++) {
      if(cuts[j].cut_name==pick) {
	cuts[j].local_counter++;
	cuts[j].last_play_datetime=t;
      }
    }
  }
  for(auto it=hits.begin();it!=hits.end();++it) {
    printf("  %s: %d\n",it.key().toUtf8().constData(),it.value());
  }
  printf("  widest gap between plays of one cut: %d\n",widest);
  if((hits.size()!=5)||(widest>6)) {
    Fail(QStringLiteral("%1 cuts used, widest gap %2").
	 arg(hits.size()).arg(widest));
  }
  g_tests_run++;
}

static void TestWeightsHold()
{
  // Weights 2, 3 and 5 from equal counters: shares stay 20/30/50%
  Announce("WeightsHold");
  QVector<RDRotationCut> cuts=EqualCuts({0,0,0});
  cuts[0].weight=2;
  cuts[1].weight=3;
  cuts[2].weight=5;
  RDCutPlan plan;
  QMap<QString,int> hits;
  for(int i=0;i<600;i++) {
    QString pick=RDCart::pickCut(cuts,RDCart::Sequence,true,&plan,65003);
    hits[pick]++;
    plan.add(65003,pick);
  }
  int expect[]={120,180,300};
  int k=0;
  for(auto it=hits.begin();it!=hits.end();++it,++k) {
    printf("  %s: %d\n",it.key().toUtf8().constData(),it.value());
    if(qAbs(it.value()-expect[k])>6) {
      Fail(QStringLiteral("%1 got %2 of 600, expected about %3").
	   arg(it.key()).arg(it.value()).arg(expect[k]));
    }
  }
  g_tests_run++;
}

static void TestEqualCountersShareEvenly()
{
  Announce("EqualCountersShareEvenly");
  QVector<RDRotationCut> cuts=EqualCuts({100,100,100,100,100});
  RDCutPlan plan;
  QMap<QString,int> hits;
  for(int i=0;i<167;i++) {
    QString pick=RDCart::pickCut(cuts,RDCart::Sequence,true,&plan,65003);
    hits[pick]++;
    plan.add(65003,pick);
  }
  for(auto it=hits.begin();it!=hits.end();++it) {
    printf("  %s: %d\n",it.key().toUtf8().constData(),it.value());
    if((it.value()<33)||(it.value()>34)) {
      Fail(QStringLiteral("%1 got %2 of 167").arg(it.key()).arg(it.value()));
    }
  }
  g_tests_run++;
}

static void TestNewCutDoesNotRepeat()
{
  // A cut added without resetting the cart's counters starts far behind:
  // it may come up often, but never twice in a row.
  Announce("NewCutDoesNotRepeat");
  QVector<RDRotationCut> cuts=EqualCuts({0,5000,5000});
  RDCutPlan plan;
  QString prev;
  for(int i=0;i<50;i++) {
    QString pick=RDCart::pickCut(cuts,RDCart::Sequence,true,&plan,65003);
    if(pick==prev) {
      Fail(QStringLiteral("pick %1 repeats %2").arg(i).arg(pick));
    }
    plan.add(65003,pick);
    prev=pick;
  }
  g_tests_run++;
}

static void TestRandomNoRepeat()
{
  Announce("RandomNoRepeat");
  QVector<RDRotationCut> cuts=EqualCuts({0,0});
  RDCutPlan plan;
  QString prev;
  for(int i=0;i<50;i++) {
    QString pick=RDCart::pickCut(cuts,RDCart::Random,false,&plan,65003);
    if(pick==prev) {
      Fail(QStringLiteral("pick %1 repeats %2").arg(i).arg(pick));
    }
    plan.add(65003,pick);
    prev=pick;
  }
  g_tests_run++;
}

static void TestSequenceCyclesFromLastPlayed()
{
  // With nothing planned, specific order moves on from the cut played last
  Announce("SequenceCyclesFromLastPlayed");
  QVector<RDRotationCut> cuts=EqualCuts({0,0,0});
  cuts[1].last_play_datetime=QDateTime::currentDateTime();
  QString pick=RDCart::pickCut(cuts,RDCart::Sequence,false,NULL,65003);
  ExpectEq("After cut 2","065003_003",pick);
  cuts[1].last_play_datetime=QDateTime();
  cuts[2].last_play_datetime=QDateTime::currentDateTime();
  pick=RDCart::pickCut(cuts,RDCart::Sequence,false,NULL,65003);
  ExpectEq("Wraps after cut 3","065003_001",pick);
  g_tests_run++;
}

int main(int argc,char *argv[])
{
  QCoreApplication a(argc,argv);

  TestSequentialOrder();
  TestWeightedRotation();
  TestPlannedLogRotates();
  TestOnAirRotates();
  TestWeightsHold();
  TestEqualCountersShareEvenly();
  TestNewCutDoesNotRepeat();
  TestRandomNoRepeat();
  TestSequenceCyclesFromLastPlayed();
  // RandomOrder skipped; core supports sequential/weighted rotations
  TestEvergreenFallback();
  TestDaypartPick();
  TestExpiredDateRange();
  TestDayOfWeekSelection();

  printf("Summary: tests=%d cuts_examined=%d\n",g_tests_run,g_cuts_seen);
  printf("cut_rotation_test: PASS\n");
  return 0;
}
