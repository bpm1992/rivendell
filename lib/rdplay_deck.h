// rdplay_deck.h
//
// Abstract a Rivendell Playback Deck
//
//   (C) Copyright 2003-2026 Fred Gleason <fredg@paravelsystems.com>
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
// ============================================================================
// CLASS OVERVIEW
// ============================================================================
//
// RDPlayDeck provides a high-level playback abstraction layer between
// rdairplay (log/UI) and CAE (audio engine). Key responsibilities:
//
//   - Managing playback state (Stopped/Playing/Paused/Stopping/Finished)
//   - Timing segue points for seamless transitions
//   - Handling hook/talk point markers
//   - Coordinating fade transitions
//
// See rdplay_deck.cpp for detailed theory of operation including:
//   - Timer deferral for network filesystem latency
//   - Delayed unload to prevent audio cutoff during buffer drainage
//   - Race condition prevention when reusing decks rapidly
// ============================================================================

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <rdcae.h>
#include <rdcart.h>
#include <rdcut.h>
#include <rdlog_line.h>

#ifndef RDPLAY_DECK_H
#define RDPLAY_DECK_H

#define POSITION_INTERVAL 100
#define RDPLAYDECK_AUDITION_ID 2147483647
#define RDPLAYDECK_DUCKDOWN_LENGTH 750
#define RDPLAYDECK_DUCKUP_LENGTH 1500

class RDPlayDeck : public QObject
{
 Q_OBJECT

 public:
  enum State {Stopped=0,Stopping=1,Playing=2,Paused=3,Finished=4};
  RDPlayDeck(RDCae *cae,int id,QObject *parent=0);
  ~RDPlayDeck();
  int id() const;
  void setId(int id);
  int owner() const;
  void setOwner(int owner);
  RDCart *cart() const;
  bool setCart(RDLogLine *logline,bool rotate);
  RDCut *cut() const;
  bool playable() const;
  int card() const;
  void setCard(int card_num);
  unsigned serial() const;
  int port() const;
  void setPort(int port_num);
  int channel() const;
  void setChannel(int chan);
  RDPlayDeck::State state() const;
  QTime startTime() const;
  int currentPosition() const;
  int lastStartPosition() const;
  void clear();
  void reset();
  QString dumpCutPoints() const;

 public slots:
  void play(unsigned pos,int segue_start=-1,int segue_end=-1,int duck_up_end=0);
  void playHook();
  void pause();
  void stop();
  void stop(int interval,int gain=-10000);
  void duckDown(int interval);
  void duckVolume(int level,int fade);

 signals:
  void stateChanged(int id,RDPlayDeck::State);
  void position(int id,int msecs);
  void segueStart(int id);
  void segueEnd(int id);
  void playFailed(int id);
  void hookStart(int id);
  void hookEnd(int id);
  void talkStart(int id);
  void talkEnd(int id);

 private slots:
  void playingData(unsigned serial);
  void playStoppedData(unsigned serial); 
  void playLoadFailedData(unsigned serial);
  void caePositionChangedData(unsigned serial,unsigned pos);
  void pointTimerData(int);
  void positionTimerData();
  void fadeTimerData();
  void duckTimerData();

 private:
  enum Point {Segue=0,Hook=1,Talk=2,SizeOf=3};
  void StartTimers(int offset);
  void StopTimers();
  
  // Core playback state
  QTimer *play_position_timer;    // Fires every POSITION_INTERVAL ms to emit position()
  RDCart *play_cart;              // Currently loaded cart metadata
  RDCut *play_cut;                // Currently loaded cut metadata
  RDCae *play_cae;                // Interface to CAE audio engine
  
  // Segue/Hook/Talk point timers - fire when each point is reached
  QTimer *play_point_timer[3];    // [Segue, Hook, Talk] timers
  QTimer *play_stop_timer;        // Timer for delayed stop (fade out)
  QTimer *play_fade_timer;        // Timer for fade transitions
  QTimer *play_duck_timer;        // Timer for ducking transitions
  
  bool play_duck_down_state;
  bool play_fade_down_state;
  int play_segue_interval;
  bool play_point_state[3];       // Has each point signal been emitted?
  int play_point_value[RDPlayDeck::SizeOf][2]; // [point_type][start/end] times
  int play_point_gain;
  int play_audio_point[2];        // [start, end] of audio region
  int play_audio_length;
  int play_fade_point[2];
  int play_fade_gain[2];
  int play_fade_down;
  int play_cut_gain;
  int play_duck_level;
  int play_duck_gain[2];
  int play_duck_up;
  int play_duck_down;
  int play_ducked;
  int play_duck_up_point;
  
  // Card/port/channel for CAE communication
  int play_card;
  unsigned play_serial;           // CAE playback handle - unique per load
  int play_port;
  int play_channel;
  unsigned play_forced_length;
  bool play_hook_mode;
  
  // Playback state tracking
  QTime play_start_time;          // Wall clock when play() called (for logging)
  RDPlayDeck::State play_state;
  bool stop_called;
  bool pause_called;
  int play_id;
  int play_owner;
  
  // Position tracking
  unsigned play_start_position;   // Where playback started from
  int play_last_start_position;   // Previous start position (for restart)
  int play_current_position;      // Timer-interpolated position
  int play_cae_position;          // Actual CAE-reported position
  
  // Timescaling (speed adjustment)
  bool play_timescale_active;
  int play_timescale_speed;
  
  // Timer deferral for network filesystem latency
  // When true, StartTimers() is deferred until playingData() callback
  bool play_pending_timers;
  int play_pending_offset;        // Offset to use when deferred timers start
};


#endif  // RDPLAY_DECK_H
