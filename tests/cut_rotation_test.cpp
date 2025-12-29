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
  for(int i=0;i<expected.size();++i) {
    QString pick=cache.selectCut(100,RDCart::Sequence,false,kTestTime);
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
  for(int i=0;i<draws;++i) {
    QString pick=cache.selectCut(200,RDCart::Sequence,true,kTestTime);
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

int main(int argc,char *argv[])
{
  QCoreApplication a(argc,argv);

  TestSequentialOrder();
  TestWeightedRotation();
  // RandomOrder skipped; core supports sequential/weighted rotations
  TestEvergreenFallback();
  TestDaypartPick();
  TestExpiredDateRange();
  TestDayOfWeekSelection();

  printf("Summary: tests=%d cuts_examined=%d\n",g_tests_run,g_cuts_seen);
  printf("cut_rotation_test: PASS\n");
  return 0;
}
