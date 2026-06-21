// rdplay_deck.cpp
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
// THEORY OF OPERATION - Playback Deck and Segue Timer Management
// ============================================================================
//
// RDPlayDeck provides a high-level abstraction for audio playback, managing
// the interaction between rdairplay (the UI/log engine) and CAE (the audio
// engine). It handles segue timing, fade transitions, and proper cleanup.
//
// TIMER DEFERRAL FOR NETWORK FILESYSTEMS:
// ------------------------------------------
// When play() is called, there may be latency between the request and when
// audio actually starts (especially with network-mounted audio files).
// To ensure accurate segue timing:
//
//   1. play() records play_start_time for logging purposes
//   2. play() sets play_pending_timers=true instead of calling StartTimers()
//   3. When CAE confirms playback via the "playing" signal, playingData() fires
//   4. playingData() calls StartTimers() to start segue/hook/talk timers
//
// This ensures segue points are timed from actual audio output, not from
// when playback was requested.
//
// BUFFER DRAINAGE (handled by audio drivers):
// -------------------------------------------
// Each audio driver (JACK, ALSA, HPI) is responsible for ensuring its
// buffers are fully drained before signaling playStopped. This allows
// RDPlayDeck to call unloadPlay() immediately without introducing delays
// that would affect segue precision.
//
// - JACK: Uses jack_drain_complete flag and SEGUE_DRAIN_SAFETY_MS timer
// - ALSA: Drains buffer in audio callback before signaling EOF
// - HPI: Hardware handles buffering internally
//
// CONTINUOUS SEGUE TIMER CORRECTION (VM Clock Drift Compensation):
// ----------------------------------------------------------------
// On virtual machines, the guest clock can drift relative to actual audio
// playback (audio is driven by the host's real-time clock via JACK/ALSA).
// To ensure precise segue timing regardless of VM clock drift:
//
//   1. CAE reports actual playback position via playPositionChanged signal
//   2. positionTimerData() fires every 100ms and compares:
//      - Expected timer remaining (based on CAE actual audio position)
//      - Actual timer remaining (QTimer::remainingTime())
//   3. If drift exceeds 50ms threshold, timer is restarted with corrected value
//
// This creates a closed-loop feedback system that continuously tracks actual
// audio output position and adjusts the segue timer accordingly.
//
// SEGUE STATE MACHINE:
// --------------------
//   Stopped -> Playing (via play())
//   Playing -> Stopping (via stop() or segue timer)
//   Stopping -> Stopped (via playStoppedData from CAE)
//   Playing -> Paused (via pause())
//   Paused -> Playing (via play())
//
// SEGUE POINT TIMING:
// -------------------
// Segue points are defined by segue_start and segue_end in the cut metadata.
// The "segue tail" is the time from segue_start to audio end.
//
//   Audio Start                    Segue Start    Audio End
//       |--------------------------|--------------|
//                                  |<- segue tail->|
//
// When the segue timer fires (pointTimerData with point=Segue):
//   1. If not yet in segue state, emits segueStart signal
//   2. RDLogPlay receives signal, starts next track with crossfade
//   3. stop(interval) is called with fade duration
//   4. When segue end reached, emits segueEnd signal
//
// ============================================================================

#include <QSignalMapper>

#include "rdapplication.h"
#include "rdplay_deck.h"

//
// Uncomment to enable detailed segue debug logging to syslog
//
#define SEGUE_DEBUG

RDPlayDeck::RDPlayDeck(RDCae *cae,int id,QObject *parent)
  : QObject(parent)
{
  play_id=id;
  play_state=RDPlayDeck::Stopped;
  play_start_time=QTime();
  play_owner=-1;
  play_last_start_position=0;
  play_serial=0;
  play_audio_length=0;
  play_channel=-1;
  play_hook_mode=false;
  play_cae_position=-1;  // -1 means no CAE position received yet
  play_pending_timers=false;
  play_pending_offset=0;

  play_cut_gain=0;
  play_duck_level=0;
  play_duck_gain[0]=0;
  play_duck_gain[1]=0;
  play_duck_up=RDPLAYDECK_DUCKUP_LENGTH;
  play_duck_down=RDPLAYDECK_DUCKDOWN_LENGTH;
  play_duck_up_point=0;
  play_duck_down_state=false;
  play_fade_down_state=false;

  //
  // CAE Connection
  //
  play_cae=cae;
  connect(play_cae,SIGNAL(playing(unsigned)),this,SLOT(playingData(unsigned)));
  connect(play_cae,SIGNAL(playStopped(unsigned)),
	  this,SLOT(playStoppedData(unsigned)));
  connect(play_cae,SIGNAL(playPositionChanged(unsigned,unsigned)),
	  this,SLOT(caePositionChangedData(unsigned,unsigned)));
  connect(play_cae,SIGNAL(playLoadFailed(unsigned)),
	  this,SLOT(playLoadFailedData(unsigned)));
  play_cart=NULL;
  play_cut=NULL;
  play_card=-1;
  play_serial=0;

  //
  // Timers
  //
  QSignalMapper *mapper=new QSignalMapper(this);
  connect(mapper,SIGNAL(mapped(int)),this,SLOT(pointTimerData(int)));
  for(int i=0;i<3;i++) {
    play_point_timer[i]=new QTimer(this);
    play_point_timer[i]->setSingleShot(true);
    connect(play_point_timer[i],SIGNAL(timeout()),mapper,SLOT(map()));
    mapper->setMapping(play_point_timer[i],i);
  }

  play_position_timer=new QTimer(this);
  connect(play_position_timer,SIGNAL(timeout()),
	  this,SLOT(positionTimerData()));
  play_fade_timer=new QTimer(this);
  play_fade_timer->setSingleShot(true);
  connect(play_fade_timer,SIGNAL(timeout()),this,SLOT(fadeTimerData()));
  play_stop_timer=new QTimer(this);
  play_stop_timer->setSingleShot(true);
  connect(play_stop_timer,SIGNAL(timeout()),this,SLOT(stop()));
  play_duck_timer=new QTimer(this);
  play_duck_timer->setSingleShot(true);
  connect(play_duck_timer,SIGNAL(timeout()),this,SLOT(duckTimerData()));
}


RDPlayDeck::~RDPlayDeck()
{
  if(play_state!=RDPlayDeck::Stopped) {
    play_cae->stopPlay(play_serial);
    play_cae->unloadPlay(play_serial);
  }
}


int RDPlayDeck::id() const
{
  return play_id;
}


void RDPlayDeck::setId(int id)
{
  play_id=id;
}


int RDPlayDeck::owner() const
{
  return play_owner;
}


void RDPlayDeck::setOwner(int owner)
{
  play_owner=owner;
}


RDCart *RDPlayDeck::cart() const
{
  return play_cart;
}


//
// RDPlayDeck::setCart()
//
// Prepares a cart/cut for playback on this play deck. This is called when
// an item moves from the log into the active play deck area (the green/red
// buttons on the left side of RDAirPlay).
//
// PARAMETERS:
//   logline - The log line containing cart/cut information and any log-level
//             timing overrides (from voicetracker, etc.)
//   rotate  - If true, forces reload even if same cart number (for cut rotation)
//
// RETURNS:
//   true  - Cart/cut loaded successfully and ready for playback
//   false - Cart/cut not found or invalid
//
// CUT VALIDITY RE-CHECK:
// ----------------------
// Between when the cut was selected (in setEvent) and when this function is
// called, the originally selected cut may have become invalid due to:
//   - Daypart window ending (e.g., cut valid until 3:00pm, now 3:01pm)
//   - Day of week changing (midnight crossing)
//   - End datetime passing
//   - Cut being replaced or deleted
//
// This function checks if the cut is still valid. If not, it attempts to
// select a new valid cut from the cart. This handles edge cases like
// automation crossing midnight or tight daypart boundaries.
//
// FRESH CUT DETECTION:
// --------------------
// This function implements "fresh cut detection" to handle the case where
// a cart/cut has been updated after the log was generated. This commonly
// happens with voicetracks delivered via Dropbox or similar mechanisms.
//
// Example scenario:
//   1. Log is generated at 6am with Cart 11545 (voicetrack placeholder)
//   2. Cart 11545 initially has a 10-second placeholder cut
//   3. At 2pm, provider delivers a 60-second voicetrack
//   4. The Dropbox importer updates Cart 11545 with the new audio
//   5. At 3pm, the cart reaches the play deck
//
// Without fresh cut detection, the segue timer would fire at 10 seconds
// (the old timing from the log), causing the next track to start while
// the 60-second voicetrack is still playing - resulting in overlapping audio.
//
// The solution:
//   1. Query fresh timing from the database when the cart enters the play deck
//   2. Compare database values with what's stored in the log (CartPointer values)
//   3. If they differ, the cut was updated - use fresh database values
//   4. If they match, respect any log-level overrides from voicetracker
//
// TIMING HIERARCHY:
// -----------------
// For audio start/end points:
//   - If cut was updated: Use fresh database values (ignore log overrides)
//   - If LogPointer override exists: Use log-level timing (voicetracker edits)
//   - Otherwise: Use CartPointer values from the library
//
// For segue points:
//   - If cut was updated: Use fresh database values
//   - If AutoPointer has valid segue: Use log-level segue (voicetracker)
//   - Otherwise: Use library segue points
//
// Hook and Talk points are always queried fresh from the database.
// Fade points respect log-level overrides if they exist.
//
bool RDPlayDeck::setCart(RDLogLine *logline,bool rotate)
{
  //
  // Initialize timescaling state from logline
  //
  play_timescale_active=logline->timescalingActive();

  //
  // Clean up existing cart/cut if switching to a different cart
  // or if rotation is requested (for carts with multiple cuts)
  //
  if((play_cart!=NULL)&&(rotate||play_cart->number()!=logline->cartNumber())) {
    delete play_cart;
    delete play_cut;
    play_cart=NULL;
    play_cut=NULL;
  }

  //
  // Load the cart and cut from the database
  //
  if(play_cart==NULL) {
    StopTimers();
    play_cart=new RDCart(logline->cartNumber());
    if(!play_cart->exists()) {
      delete play_cart;
      play_cart=NULL;
      return false;
    }

    QString cutname=logline->cutName();
    
    //
    // Calculate the effective play time for daypart cut selection.
    //
    // We use the LATER of the scheduled time and the actual current time:
    //   - Pre-loading (loaded early): scheduled_time > current → use scheduled_time
    //     so a cart loaded at 9:59 for a 10:10am event gets the 10:10am cut.
    //   - Running late (station behind schedule): current > scheduled_time → use
    //     current_time so a time-check cart playing at 6:11 gets the 6:11 cut,
    //     not the 6:04 scheduled cut.
    //
    QTime scheduled_time = logline->startTime(RDLogLine::Logged);
    if(!scheduled_time.isValid()) {
      scheduled_time = logline->startTime(RDLogLine::Predicted);
    }
    QTime current_time = QTime::currentTime();
    QTime effective_time = (scheduled_time.isValid() && scheduled_time > current_time)
                           ? scheduled_time
                           : current_time;
#ifdef SEGUE_DEBUG
    rda->syslog(LOG_DEBUG,
                "SEGUE-DEBUG [rdplay_deck] setCart: cart %u scheduled_time=%s "
                "current=%s effective=%s",
                logline->cartNumber(),
                scheduled_time.toString("hh:mm:ss").toUtf8().constData(),
                current_time.toString("hh:mm:ss").toUtf8().constData(),
                effective_time.toString("hh:mm:ss").toUtf8().constData());
#endif

    //
    // Handle "cut no longer valid" case
    //
    // Between when the cut was selected (in setEvent) and now, the cut may
    // have become invalid due to:
    //   - Daypart window ending (e.g., cut valid until 3:00pm, now 3:01pm)
    //   - Day of week changing (midnight crossing)
    //   - End datetime passing
    //   - Cut being replaced or deleted
    //
    // If the originally selected cut is no longer valid, re-select using
    // effective_time (the later of scheduled and actual time).
    //
    if(!cutname.isEmpty()) {
      RDCut *check_cut=new RDCut(cutname);
      if(!check_cut->exists() || !check_cut->isValid(effective_time)) {
        // Cut no longer valid - try to select a new one using effective time
#ifdef SEGUE_DEBUG
        rda->syslog(LOG_DEBUG,
                    "SEGUE-DEBUG [rdplay_deck] setCart: cut '%s' no longer valid, "
                    "attempting re-selection for cart %u at effective_time=%s",
                    cutname.toUtf8().constData(), logline->cartNumber(),
                    effective_time.toString("hh:mm:ss").toUtf8().constData());
#endif
        QString new_cutname;
        if(play_cart->selectCut(&new_cutname, effective_time) && !new_cutname.isEmpty()) {
          cutname=new_cutname;
          logline->setCutName(cutname);
          logline->setCutNumber(cutname.right(3).toInt());
#ifdef SEGUE_DEBUG
          rda->syslog(LOG_DEBUG,
                      "SEGUE-DEBUG [rdplay_deck] setCart: re-selected cut '%s'",
                      cutname.toUtf8().constData());
#endif
        }
        else {
          // No valid cuts available
          delete check_cut;
          delete play_cart;
          play_cart=NULL;
#ifdef SEGUE_DEBUG
          rda->syslog(LOG_DEBUG,
                      "SEGUE-DEBUG [rdplay_deck] setCart: no valid cuts for cart %u",
                      logline->cartNumber());
#endif
          return false;
        }
      }
      delete check_cut;
    }
    
    if(cutname.isEmpty()) {
      delete play_cart;
      play_cart=NULL;
      return false;
    }
    play_cut=new RDCut(cutname);
    if(!play_cut->exists()) {
      delete play_cut;
      play_cut=NULL;
      delete play_cart;
      play_cart=NULL;
      return false;
    }
  }

  //
  // =========================================================================
  // FRESH CUT TIMING DETECTION
  // =========================================================================
  // Query current timing from the database and compare with what was stored
  // in the log when it was created. If the values differ, the cut has been
  // updated (e.g., new voicetrack delivered) and we should use fresh values.
  //
  
  // Get current timing directly from the database (via play_cut)
  int db_start_point=play_cut->startPoint(RDLogLine::CartPointer);
  int db_end_point=play_cut->endPoint();
  int db_segue_start=play_cut->segueStartPoint();
  int db_segue_end=play_cut->segueEndPoint();
  unsigned db_length=play_cut->length();

  // Get the timing that was captured when the log was generated/loaded
  // These CartPointer values represent what the cut looked like at that time
  int log_cart_start=logline->startPoint(RDLogLine::CartPointer);
  int log_cart_end=logline->endPoint(RDLogLine::CartPointer);
  int log_cart_segue_start=logline->segueStartPoint(RDLogLine::CartPointer);
  int log_cart_segue_end=logline->segueEndPoint(RDLogLine::CartPointer);
  // effectiveLength() returns the CUTS.LENGTH value recorded at log-load time.
  // Comparing against db_length catches weather/voicetrack replacements where
  // only the audio file duration changed but edit points were not re-saved
  // (START_POINT/END_POINT still -1), which the point comparison misses.
  unsigned log_cart_length=(unsigned)logline->effectiveLength();

  // Detect if the cut has been updated since the log was created.
  // Include LENGTH so that audio-only replacements (weather carts) are caught
  // even when the edit points remain at their default -1 values.
  bool cut_was_updated=false;
  if((db_start_point!=log_cart_start) ||
     (db_end_point!=log_cart_end) ||
     (db_segue_start!=log_cart_segue_start) ||
     (db_segue_end!=log_cart_segue_end) ||
     (db_length!=log_cart_length)) {
    cut_was_updated=true;
#ifdef SEGUE_DEBUG
    rda->syslog(LOG_DEBUG,
                "SEGUE-DEBUG [rdplay_deck] setCart: cut updated detected for cart %u "
                "db=[%d-%d segue %d-%d len %u] vs log=[%d-%d segue %d-%d len %u]",
                logline->cartNumber(),
                db_start_point, db_end_point, db_segue_start, db_segue_end, db_length,
                log_cart_start, log_cart_end, log_cart_segue_start, log_cart_segue_end,
                log_cart_length);
#endif
  }

  //
  // =========================================================================
  // SET AUDIO START/END POINTS
  // =========================================================================
  // Determines the portion of the audio file to play.
  // Priority: Fresh DB values (if cut updated) > LogPointer > CartPointer
  //
  if(cut_was_updated) {
    // Cut has changed since log was created - use fresh values from database.
    // Use calculated endpoints (resolves -1 to 0/length()) so that
    // play_forced_length is correct even when edit points are unset.
    int calc_start=play_cut->startPoint(true);
    int calc_end=play_cut->endPoint(true);
    play_forced_length=calc_end-calc_start;
    play_audio_point[0]=db_start_point;
    play_audio_point[1]=db_end_point;

    // Update the logline's CartPointer values so that rdlogplay's
    // segue calculations use the correct (fresh) endpoints.
    // Without this, logline->endPoint() returns stale values causing
    // premature track termination when cuts have been re-edited.
    logline->setStartPoint(db_start_point, RDLogLine::CartPointer);
    logline->setEndPoint(db_end_point, RDLogLine::CartPointer);
    logline->setSegueStartPoint(db_segue_start, RDLogLine::CartPointer);
    logline->setSegueEndPoint(db_segue_end, RDLogLine::CartPointer);
    
#ifdef SEGUE_DEBUG
    rda->syslog(LOG_DEBUG,
                "SEGUE-DEBUG [rdplay_deck] setCart: using FRESH timing audio=[%d-%d]",
                play_audio_point[0], play_audio_point[1]);
#endif
  }
  else if(logline->startPoint(RDLogLine::LogPointer)<0) {
    // No log-level override exists - use values from the library
    play_forced_length=logline->forcedLength();
    play_audio_point[0]=db_start_point;
    play_audio_point[1]=db_end_point;
  }
  else {
    // Log-level override exists (from voicetracker) and cut hasn't changed
    // Respect the custom timing set by the operator
    play_forced_length=logline->effectiveLength();
    play_audio_point[0]=logline->startPoint(RDLogLine::LogPointer);
    play_audio_point[1]=logline->endPoint();
  }
  
  // Handle case where only end point has log-level override
  if(!cut_was_updated && logline->endPoint(RDLogLine::LogPointer)>=0) {
    play_forced_length=logline->effectiveLength();
    play_audio_point[0]=logline->startPoint();
    play_audio_point[1]=logline->endPoint(RDLogLine::LogPointer);
  }

  //
  // =========================================================================
  // CALCULATE TIMESCALING
  // =========================================================================
  // If timescaling is active, calculate the speed adjustment needed to fit
  // the audio into the forced length. Disable if outside acceptable range.
  //
  if(play_timescale_active) {
    play_timescale_speed=
      (int)(RD_TIMESCALE_DIVISOR*(double)(play_audio_point[1]-
    					  play_audio_point[0])/
	    (double)play_forced_length);
    if((((double)play_timescale_speed)<
	(RD_TIMESCALE_DIVISOR*RD_TIMESCALE_MIN))||
       (((double)play_timescale_speed)>
	(RD_TIMESCALE_DIVISOR*RD_TIMESCALE_MAX))) {
      play_timescale_speed=(int)RD_TIMESCALE_DIVISOR;
      play_timescale_active=false;
    }
  }
  else {
    play_timescale_speed=(int)RD_TIMESCALE_DIVISOR;
  }
  play_audio_length=play_audio_point[1]-play_audio_point[0];

  //
  // =========================================================================
  // SET SEGUE POINTS
  // =========================================================================
  // Segue points determine when the next track starts (segue start) and
  // when this track fades out (segue end). Critical for smooth transitions.
  // Priority: Fresh DB values (if cut updated) > AutoPointer > CartPointer
  //
  if(cut_was_updated) {
    // Cut has changed - use fresh values from database
    // Prevents overlapping audio when new voicetrack is longer/shorter
    play_point_value[RDPlayDeck::Segue][0]=db_segue_start;
    play_point_value[RDPlayDeck::Segue][1]=db_segue_end;
#ifdef SEGUE_DEBUG
    rda->syslog(LOG_DEBUG,
                "SEGUE-DEBUG [rdplay_deck] setCart: using FRESH segue=[%d-%d]",
                play_point_value[RDPlayDeck::Segue][0],
                play_point_value[RDPlayDeck::Segue][1]);
#endif
  }
  else if(logline->segueStartPoint(RDLogLine::AutoPointer)<0) {
    // No log-level segue override - use library values
    play_point_value[RDPlayDeck::Segue][0]=db_segue_start;
    play_point_value[RDPlayDeck::Segue][1]=db_segue_end;
  }
  else {
    // Log-level segue override exists (from voicetracker) and cut unchanged
    // Respect the custom segue timing set by the operator
    play_point_value[RDPlayDeck::Segue][0]=
      logline->segueStartPoint(RDLogLine::AutoPointer);
    play_point_value[RDPlayDeck::Segue][1]=
      logline->segueEndPoint(RDLogLine::AutoPointer);
#ifdef SEGUE_DEBUG
    rda->syslog(LOG_DEBUG,
                "SEGUE-DEBUG [rdplay_deck] setCart: using LOG segue=[%d-%d]",
                play_point_value[RDPlayDeck::Segue][0],
                play_point_value[RDPlayDeck::Segue][1]);
#endif
  }
  play_point_gain=logline->segueGain();

  //
  // =========================================================================
  // SET HOOK POINTS
  // =========================================================================
  // Hook points define a preview segment (typically the "hook" of a song).
  // Always use fresh values from the database.
  //
  play_point_value[RDPlayDeck::Hook][0]=
    (int)((double)play_cut->hookStartPoint());
  play_point_value[RDPlayDeck::Hook][1]=
    (int)((double)play_cut->hookEndPoint());
  logline->setHookStartPoint(play_point_value[RDPlayDeck::Hook][0]);
  logline->setHookEndPoint(play_point_value[RDPlayDeck::Hook][1]);

  //
  // =========================================================================
  // SET TALK POINTS
  // =========================================================================
  // Talk points define the intro segment where a DJ can talk over the music.
  // Always use fresh values from the database, adjusted for timescaling.
  //
  play_point_value[RDPlayDeck::Talk][0]=
    (int)((double)play_cut->talkStartPoint()*
	  (RD_TIMESCALE_DIVISOR/(double)play_timescale_speed));
  play_point_value[RDPlayDeck::Talk][1]=
    (int)((double)play_cut->talkEndPoint()*
	  (RD_TIMESCALE_DIVISOR/(double)play_timescale_speed));
  logline->setTalkStartPoint(play_point_value[RDPlayDeck::Talk][0]);
  logline->setTalkEndPoint(play_point_value[RDPlayDeck::Talk][1]);

  //
  // =========================================================================
  // SET FADE POINTS
  // =========================================================================
  // Fade points control automatic volume fades at start and end of playback.
  // Respects log-level overrides if they exist.
  //
  if(logline->fadeupPoint(RDLogLine::LogPointer)<0) {
    play_fade_point[0]=play_cut->fadeupPoint();
    play_fade_gain[0]=RD_FADE_DEPTH;
  }
  else {
    play_fade_point[0]=logline->fadeupPoint(RDLogLine::LogPointer);
    play_fade_gain[0]=logline->fadeupGain();
  }
  if(logline->fadedownPoint(RDLogLine::LogPointer)<0) {
    play_fade_point[1]=play_cut->fadedownPoint();
    play_fade_gain[1]=RD_FADE_DEPTH;
  }
  else {
    play_fade_point[1]=logline->fadedownPoint(RDLogLine::LogPointer);
    play_fade_gain[1]=logline->fadedownGain();
  }

  //
  // =========================================================================
  // SET DUCK GAINS
  // =========================================================================
  // Duck gains control volume ducking for voiceovers.
  //
  play_duck_gain[0]=logline->duckUpGain();
  play_duck_gain[1]=logline->duckDownGain();

  //
  // =========================================================================
  // LOAD AUDIO INTO CAE
  // =========================================================================
  // Request CAE (Core Audio Engine) to load the audio file for playback.
  // Skip if deck is paused (audio is already loaded).
  //
  if(play_state!=RDPlayDeck::Paused) {
    play_serial=play_cae->loadPlay(play_card,play_port,play_cut->cutName());
  }
  play_state=RDPlayDeck::Stopped;
  return true;
}


RDCut *RDPlayDeck::cut() const
{
  return play_cut;
}


bool RDPlayDeck::playable() const
{
  if(play_serial==0) {
    return false;
  }
  return true;
}


int RDPlayDeck::card() const
{
  return play_card;
}


void RDPlayDeck::setCard(int card_num)
{
  play_card=card_num;
}


unsigned RDPlayDeck::serial() const
{
  return play_serial;
}


int RDPlayDeck::port() const
{
  return play_port;
}


void RDPlayDeck::setPort(int port_num)
{
  play_port=port_num;
}


int RDPlayDeck::channel() const
{
  return play_channel;
}


void RDPlayDeck::setChannel(int chan)
{
  play_channel=chan;
}


RDPlayDeck::State RDPlayDeck::state() const
{
  return play_state;
}


QTime RDPlayDeck::startTime() const
{
  return play_start_time;
}


int RDPlayDeck::currentPosition() const
{
  switch(play_state) {
      case RDPlayDeck::Playing:
	{
	  // Use wall-clock estimate for smooth UI updates
	  int pos = play_start_position +
	    play_start_time.msecsTo(QTime::currentTime());
	  // Handle midnight wraparound: if playback started before midnight
	  // and current time is after midnight, msecsTo() returns negative.
	  // Add 24 hours (86400000ms) to correct.
	  if(pos < 0) {
	    pos += 86400000;
	  }
	  return pos;
	}

      case RDPlayDeck::Paused:
	return play_current_position+POSITION_INTERVAL;

      default:
	return play_start_position;
  }
  return 0;
}


int RDPlayDeck::lastStartPosition() const
{
  return play_last_start_position;
}


void RDPlayDeck::clear()
{
  StopTimers();
  switch(play_state) {
      case RDPlayDeck::Playing:
      case RDPlayDeck::Stopping:
	stop();
	break;

      case RDPlayDeck::Paused:
	play_cae->unloadPlay(play_serial);
	emit stateChanged(play_id,RDPlayDeck::Stopped);
	break;

      default:
	emit stateChanged(play_id,RDPlayDeck::Stopped);
	break;
  }
}


void RDPlayDeck::reset()
{
  StopTimers();
  switch(play_state) {
      case RDPlayDeck::Playing:
      case RDPlayDeck::Stopping:
	play_cae->stopPlay(play_serial);

      case RDPlayDeck::Paused:
	play_cae->unloadPlay(play_serial);
	break;

      default:
	break;
  }
  play_state=RDPlayDeck::Stopped;
}


QString RDPlayDeck::dumpCutPoints() const
{
  QString ret;

  ret=QString::asprintf("play_audio_point: start: %d  end: %d ",
			play_audio_point[0],play_audio_point[1]);
  if(play_stop_timer->isActive()) {
    ret+=QString::asprintf("play_stop_timer: %d",play_stop_timer->interval()); 
  }
  else {
    ret+="play_stop_timer: inactive";
  }
  ret+="\n";

  ret+=QString::asprintf("play_point_value[SEGUE]: start: %d  end: %d ",
			 play_point_value[0][0],play_point_value[0][1]);
  if(play_point_timer[0]->isActive()) {
    ret+=QString::asprintf("play_point_timer[SEGUE]: %d",
			   play_point_timer[0]->interval());
  }
  else {
    ret+="play_point_timer[SEGUE]: inactive";
  }
  ret+="\n";

  ret+=QString::asprintf("play_point_value[TALK]: start: %d  end: %d ",
			 play_point_value[1][0],play_point_value[1][1]);
  if(play_point_timer[1]->isActive()) {
    ret+=QString::asprintf("play_point_timer[TALK]: %d",
			   play_point_timer[1]->interval());
  }
  else {
    ret+="play_point_timer[TALK]: inactive";
  }
  ret+="\n";

  ret+=QString::asprintf("play_point_value[HOOK]: start: %d  end: %d ",
			 play_point_value[2][0],play_point_value[2][1]);
  if(play_point_timer[2]->isActive()) {
    ret+=QString::asprintf("play_point_timer[HOOK]: %d",
			   play_point_timer[2]->interval());
  }
  else {
    ret+="play_point_timer[HOOK]: inactive";
  }
  ret+="\n";

  return ret;
}


void RDPlayDeck::play(unsigned pos,int segue_start,int segue_end,
		      int duck_up_end)
{
  int fadeup;
  play_hook_mode=false;
  play_cut_gain=play_cut->playGain();
  
  play_ducked=0;
  if(duck_up_end==-1) { //ducked until stop (for recording in voice tracker)
    play_ducked=play_duck_gain[0];
    play_duck_up_point=0;
  }
  else {
    play_duck_up_point=duck_up_end-play_duck_up;
  }
  if(play_duck_up_point<0)
    play_duck_up_point=0;
  else
    play_ducked=play_duck_gain[0];

  if(play_serial==0) {
    return;
  }
  play_start_position=pos;
  play_current_position=pos;
  play_last_start_position=play_start_position;
  stop_called=false;
  pause_called=false;
  play_cae->positionPlay(play_serial,play_audio_point[0]+pos);
  if((play_fade_point[0]==-1)||(play_fade_point[0]==play_audio_point[0])||
     ((fadeup=play_fade_point[0]-play_audio_point[0]-pos)<=0)||
     (play_state==RDPlayDeck::Paused)) {
    if((play_fade_point[1]==-1)||((fadeup=pos-play_fade_point[1])<=0)||
       (play_state==RDPlayDeck::Paused)) {
      play_cae->
	setOutputVolume(play_serial,play_ducked+play_cut_gain+play_duck_level);
      play_cae->fadeOutputVolume(play_serial,
				 play_ducked+play_cut_gain+play_duck_level,10);
    }
    else {  // Fadedown event in progress, interpolate the gain accordingly
      int level=play_fade_gain[1]*((int)pos-play_fade_point[1])/
			(play_audio_point[1]-play_fade_point[1]);
      play_cae->
	setOutputVolume(play_serial,level+play_cut_gain+play_duck_level);
      play_cae->fadeOutputVolume(play_serial,
				 play_fade_gain[1]+play_cut_gain+
				 play_duck_level,
				 play_audio_point[1]-(int)pos);
    }
  }
  else {  // FadeUp event in progress, interpolate the gain accordingly
    int level=(play_fade_gain[0]*fadeup/
		      (play_fade_point[0]-play_audio_point[0]));
    if (level>play_ducked) {
    play_cae->
      setOutputVolume(play_serial,play_ducked+play_cut_gain+play_duck_level);
    play_cae->fadeOutputVolume(play_serial,
			       play_ducked+play_cut_gain+play_duck_level,
			       fadeup);
    }
    else {
    play_cae->
      setOutputVolume(play_serial,level+play_cut_gain+play_duck_level);
    play_cae->fadeOutputVolume(play_serial,
			       play_ducked+play_cut_gain+play_duck_level,
			       fadeup);
    }
  }
  play_cae->
    play(play_serial,
         (int)(100000.0*(double)(play_audio_point[1]-play_audio_point[0]-pos)/
	       (double)play_timescale_speed),
         play_timescale_speed,false);
  //
  // Set start time now so rdlogplay can capture it for logging.
  // But defer timer start until CAE confirms playback has actually started.
  // This is critical for network filesystems where there may be read
  // latency between when we request play and when audio actually outputs.
  // The timers will be started in playingData() when CAE sends the "playing" signal.
  //
  play_start_time=QTime::currentTime();
  play_pending_timers=true;
  play_pending_offset=pos;
  play_state=RDPlayDeck::Playing;
}


void RDPlayDeck::playHook()
{
  play(play_point_value[RDPlayDeck::Hook][0]-play_audio_point[0]);
  play_hook_mode=true;
}


void RDPlayDeck::pause()
{
  pause_called=true;
  play_state=RDPlayDeck::Stopping;  // Intermediate state while draining
  emit stateChanged(play_id,RDPlayDeck::Stopping);  // Immediate UI feedback
  play_cae->stopPlay(play_serial);
}


void RDPlayDeck::stop()
{
  if((play_state!=RDPlayDeck::Playing)&&(play_state!=RDPlayDeck::Stopping)) {
    return;
  }
#ifdef SEGUE_DEBUG
  rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] stop(): id=%d serial=%u pos=%d",
              play_id, play_serial, currentPosition());
#endif
  if(pause_called) {
    play_state=RDPlayDeck::Stopped;
  }
  else {
    stop_called=true;
    play_state=RDPlayDeck::Stopping;
    emit stateChanged(play_id,RDPlayDeck::Stopping);  // Immediate UI feedback
    play_cae->stopPlay(play_serial);
  }
}


void RDPlayDeck::stop(int interval,int gain)
{
  int level;
  
  if(gain>play_point_gain) {
    play_point_gain=gain;
  }
  

  if((play_state!=RDPlayDeck::Playing)&&(play_state!=RDPlayDeck::Stopping)) {
    return;
  }
  if((interval<=0)||pause_called) {
    stop();
  }
  else {
    //
    // Stop the segue point timer to prevent it from firing and
    // cutting off the audio before our timed stop completes
    //
    if(play_point_timer[RDPlayDeck::Segue]->isActive()) {
      play_point_timer[RDPlayDeck::Segue]->stop();
    }
    if(play_duck_gain[1]<0 && play_duck_down<interval && 
        (play_audio_point[1]-play_audio_point[0]-
        currentPosition())>play_duck_down) { // duck
      if(play_audio_point[0]+currentPosition()>play_fade_point[1]) {
        level=play_fade_gain[1]*((currentPosition()+play_audio_point[0])-
              play_fade_point[1])/(play_audio_point[1]-play_fade_point[1]);
      }
      else {
        level=0;
      }        
      if(level>play_duck_gain[1]){
	play_cae->fadeOutputVolume(play_serial,
				   play_duck_gain[1]+play_cut_gain+
				   play_duck_level,play_duck_down);
        play_duck_timer->start(play_duck_down);
        play_duck_down_state=true;
        play_segue_interval=interval;
      }
    }
    else {
      if(play_point_gain!=0) {
        play_cae->fadeOutputVolume(play_serial,
				   play_point_gain+play_cut_gain+
				   play_duck_level,interval);
      }
    }
    play_stop_timer->start(interval);
    stop_called=true;
    play_state=RDPlayDeck::Stopping;
  }
}




void RDPlayDeck::duckDown(int interval)
{
  if(play_duck_gain[1]<0) {
    play_cae->fadeOutputVolume(play_serial,
			       play_duck_gain[1]+play_cut_gain+play_duck_level,
			       play_duck_down);
    play_duck_timer->start(play_duck_down);
    play_duck_down_state=true;
    play_segue_interval=interval;

  }
}


void RDPlayDeck::duckVolume(int level,int fade)
{
  play_duck_level=level;
  if((state()==RDPlayDeck::Playing || state()==RDPlayDeck::Stopping) && fade>0) {
	  play_cae->fadeOutputVolume(play_serial,
				     play_cut_gain+play_duck_level,
				     fade);
  }
}


void RDPlayDeck::playingData(unsigned serial)
{
  if(serial!=play_serial) {
    return;
  }
#ifdef SEGUE_DEBUG
  rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] playingData: id=%d serial=%u audio=[%d-%d] segue=[%d-%d]",
              play_id, serial, play_audio_point[0], play_audio_point[1],
              play_point_value[RDPlayDeck::Segue][0], play_point_value[RDPlayDeck::Segue][1]);
#endif
  play_cae_position=-1;  // Reset, will be updated by CAE position reports
  play_position_timer->start(POSITION_INTERVAL);
  
  //
  // Now that CAE confirms audio is actually playing, start the timers.
  // This ensures segue timing is based on when audio actually starts,
  // not when we requested it (important for network filesystem latency).
  // Note: play_start_time was already set in play() for logging purposes,
  // but we keep the timer start deferred until now.
  //
  if(play_pending_timers) {
#ifdef SEGUE_DEBUG
    rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] playingData: id=%d starting deferred timers offset=%d",
                play_id, play_pending_offset);
#endif
    StartTimers(play_pending_offset);
    play_pending_timers=false;
  }
  
  emit stateChanged(play_id,RDPlayDeck::Playing);
}


void RDPlayDeck::playLoadFailedData(unsigned serial)
{
  if(serial!=play_serial) {
    return;
  }
  rda->syslog(LOG_WARNING,"[rdplay_deck] playLoadFailedData: id=%d serial=%u - CAE failed to load audio",
              play_id, serial);
  //
  // Transition to Finished state so the log engine knows to advance.
  // This handles cases like missing files, corrupted audio, or stream
  // allocation failures - the log should continue to the next item
  // rather than getting stuck.
  //
  play_state=RDPlayDeck::Finished;
  emit playFailed(play_id);
  emit stateChanged(play_id,RDPlayDeck::Finished);
}


void RDPlayDeck::playStoppedData(unsigned serial)
{ 
  if(serial!=play_serial) {
    return;
  }
#ifdef SEGUE_DEBUG
  rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] playStoppedData: id=%d serial=%u pos=%d stop_called=%d",
              play_id, serial, play_current_position, stop_called);
  rda->syslog(LOG_DEBUG,
              "SEGUE-DEBUG [rdplay_deck] playStoppedData: id=%d serial=%u state=%d pause_called=%d pending_timers=%d",
              play_id, serial, play_state, pause_called, play_pending_timers);
#endif
  play_position_timer->stop();
  play_start_time=QTime();
  StopTimers();

  // If playback ended naturally while a segue timer was pending,
  // clear the segue state and notify listeners so UI/log can clean up.
  if(play_point_state[RDPlayDeck::Segue]) {
#ifdef SEGUE_DEBUG
    rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] playStoppedData: id=%d emitting segueEnd (was in segue state)",
                play_id);
#endif
    play_point_state[RDPlayDeck::Segue]=false;
    if(play_point_timer[RDPlayDeck::Segue]->isActive()) {
      play_point_timer[RDPlayDeck::Segue]->stop();
    }
    emit segueEnd(play_id);
  }

  if(pause_called) {
    play_state=RDPlayDeck::Paused;
    emit stateChanged(play_id,RDPlayDeck::Paused);
  }
  else {
    //
    // Call unloadPlay immediately. Each audio driver is responsible for
    // ensuring its buffers are drained before signaling playStopped.
    // (JACK uses jack_drain_complete flag, ALSA drains in its callback, etc.)
    //
    play_cae->unloadPlay(play_serial);

    play_serial=0;
    play_state=RDPlayDeck::Stopped;
    play_current_position=0;
    play_cae_position=-1;
    play_duck_down_state=false;
    play_fade_down_state=false;
    if(stop_called) {
      emit stateChanged(play_id,RDPlayDeck::Stopped);
    }
    else {
      emit stateChanged(play_id,RDPlayDeck::Finished);
    }
  }
}


// Handle CAE position updates - these reflect actual audio output position
// which may differ from wall-clock position due to buffering/I/O delays
void RDPlayDeck::caePositionChangedData(unsigned serial,unsigned pos)
{
  if(serial!=play_serial) {
    return;  // Not our stream
  }
  if(play_state!=RDPlayDeck::Playing) {
    return;  // Not playing
  }
  
  // Store the CAE-reported position (this is in samples, convert to ms)
  // The pos parameter is in milliseconds from CAE
  play_cae_position = pos;
}


// Handles timed marker callbacks; for segues this fires the segueStart/End
// transitions and notifies RDLogPlay via the segueStart/segueEnd signals.
// Invoked internally when the timer set up in StartTimers() expires.
void RDPlayDeck::pointTimerData(int point)
{
  switch(point) {
      case RDPlayDeck::Segue:
	if(play_point_state[point]) {
#ifdef SEGUE_DEBUG
	  rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] pointTimerData: id=%d SEGUE_END pos=%d gain=%d",
	              play_id, currentPosition(), play_point_gain);
#endif
	  play_point_state[point]=false;
	  
	  // Respect segue gain setting:
	  // play_point_gain=0 means no fade (hard stop)
	  // play_point_gain<0 (e.g., -3000 RD_FADE_DEPTH) means fade down
	  if(play_point_gain == 0) {
	    // No fade requested - hard stop
#ifdef SEGUE_DEBUG
	    rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] pointTimerData: id=%d hard stop (no fade)", play_id);
#endif
	    rda->cae()->stopPlay(play_serial);
	  }
	  else {
	    // Apply quick fade to respect segue gain setting
	    // Use 50ms minimum fade for smooth audio
#ifdef SEGUE_DEBUG
	    rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] pointTimerData: id=%d fade stop 50ms", play_id);
#endif
	    play_cae->fadeOutputVolume(play_serial,
				       play_point_gain + play_cut_gain + play_duck_level,
				       50);
	    play_stop_timer->start(50);
	    stop_called = true;
	    play_state = RDPlayDeck::Stopping;
	  }
	  emit segueEnd(play_id);
	}
	else {
	  int segue_tail = play_point_value[point][1]-play_point_value[point][0];
	  int current_pos = currentPosition();  // Now uses CAE position if available
	  int remaining = play_audio_point[1] - play_audio_point[0] - current_pos;
	  if(remaining < 0) {
	    remaining = 0;
	  }
	  
	  play_point_state[point]=true;
	  
	  // Calculate timer based on actual remaining time
	  // currentPosition() now uses CAE-reported position when available,
	  // which reflects actual audio output rather than wall-clock estimate
	  int timer_val;
	  if(segue_tail >= 150) {  // MIN_SEGUE_TAIL_MS - normal segue with fade
	    // Use remaining time, but cap at segue_tail if remaining is longer
	    timer_val = (remaining < segue_tail) ? remaining : segue_tail;
	    if(timer_val < 50) timer_val = 50;  // Minimum 50ms for any fade
	  }
	  else {
	    // Short/zero tail - let track play to completion
	    // Add small buffer (100ms) for safety margin
	    timer_val = remaining + 100;
	  }
#ifdef SEGUE_DEBUG
	  rda->syslog(LOG_DEBUG,"SEGUE-DEBUG [rdplay_deck] pointTimerData: id=%d SEGUE_START pos=%d remaining=%d tail=%d timer=%d cae_pos=%d",
	              play_id, current_pos, remaining, segue_tail, timer_val, play_cae_position);
#endif
	  
	  play_point_timer[point]->start(timer_val);
	  
	  emit segueStart(play_id);
	}
	break;

      case RDPlayDeck::Hook:
	if(play_point_state[point]) {
	  play_point_state[point]=false;
	  emit hookEnd(play_id);
	}
	else {
	  play_point_state[point]=true;
	  play_point_timer[point]->
	    start(play_point_value[point][1]-play_point_value[point][0]);
	  emit hookStart(play_id);
	}
	break;

      case RDPlayDeck::Talk:
	if(play_point_state[point]) {
	  play_point_state[point]=false;
	  emit talkEnd(play_id);
	}
	else {
	  play_point_state[point]=true;
	  play_point_timer[point]->
	    start(play_point_value[point][1]-play_point_value[point][0]);
	  emit talkStart(play_id);
	}
	break;
  }
}


void RDPlayDeck::positionTimerData()
{
  // Calculate wall-clock position - this provides smooth UI updates
  int wall_clock_pos = play_start_position+play_start_time.msecsTo(QTime::currentTime());
  if(wall_clock_pos<0) {       // Handle crossing midnight!
    wall_clock_pos+=86400000;
  }
  
  // Use wall-clock position for UI display (smoother updates)
  // but keep CAE position for segue timer correction (more accurate)
  play_current_position = wall_clock_pos;
  
  //
  // CONTINUOUS SEGUE TIMER CORRECTION
  // On VMs, the guest clock can drift relative to actual audio playback.
  // We continuously correct the segue timer based on actual CAE position
  // to ensure precise segue timing regardless of VM clock drift.
  //
  if(play_cae_position >= 0 && play_point_timer[RDPlayDeck::Segue]->isActive()) {
    // Calculate actual remaining time based on CAE-reported position
    int actual_remaining = play_audio_point[1] - play_audio_point[0] - play_cae_position;
    if(actual_remaining < 0) {
      actual_remaining = 0;
    }
    
    // Get current timer remaining time
    int timer_remaining = play_point_timer[RDPlayDeck::Segue]->remainingTime();
    
    // Calculate expected timer value based on actual audio position
    int expected_timer;
    if(!play_point_state[RDPlayDeck::Segue]) {
      // Timer is counting down to segueStart point
      int segue_start_pos = play_point_value[RDPlayDeck::Segue][0] - play_audio_point[0];
      expected_timer = segue_start_pos - play_cae_position;
    }
    else {
      // Timer is counting down to segueEnd point (we're in segue transition)
      expected_timer = actual_remaining;
    }
    
    // If drift exceeds threshold (50ms), correct the timer
    // This creates a closed-loop feedback system
    int drift = timer_remaining - expected_timer;
    if(expected_timer > 0 && (drift > 50 || drift < -50)) {
      // Restart timer with corrected value
      // Clamp to minimum 10ms to prevent negative/zero timers
      int corrected_timer = (expected_timer > 10) ? expected_timer : 10;
      play_point_timer[RDPlayDeck::Segue]->start(corrected_timer);
    }
  }
  
  if(play_hook_mode) {
    emit position(play_id,play_current_position-(play_point_value[RDPlayDeck::Hook][0]-play_audio_point[0]));
  }
  else {
    emit position(play_id,play_current_position);
  }
}


void RDPlayDeck::fadeTimerData()
{
  if(!play_duck_down_state) {
  play_cae->
    fadeOutputVolume(play_serial,
		     play_fade_gain[1]+play_cut_gain+play_duck_level,
		     play_fade_down);
  }
  play_fade_down_state=true;
}


void RDPlayDeck::duckTimerData()
{
  if (!play_duck_down_state) { //duck up
    play_cae->
      fadeOutputVolume(play_serial,
		       0+play_cut_gain+play_duck_level,play_duck_up);
    play_ducked=0;
  }
  else { //duck down
    if(play_point_gain!=0) {
      play_cae->fadeOutputVolume(play_serial,
				 play_point_gain+play_cut_gain+play_duck_level,
				 play_segue_interval-play_duck_down);
    }
    else {
      if(play_fade_down_state && 
         play_fade_gain[1]<play_duck_gain[1]) { //fade down in progress
        play_cae->fadeOutputVolume(play_serial,
				   play_fade_gain[1]+play_cut_gain+
				   play_duck_level,
				   play_segue_interval-play_duck_down);
      }   
    } 
    play_duck_down_state=false;
  }
}


void RDPlayDeck::StartTimers(int offset)
{
  /*
   * Previous implementation, not currently used
   *
  int audio_point;

  for(int i=0;i<RDPlayDeck::SizeOf;i++) {
    play_point_state[i]=false;
    if((play_point_value[i][0]!=-1)&&
       (play_point_value[i][0]!=play_point_value[i][1])) {
      audio_point=(int)
	(RD_TIMESCALE_DIVISOR*(double)play_audio_point[0]/
	 (double)play_timescale_speed);
      if((play_point_value[i][0]-audio_point-offset)>=0) {
	play_point_timer[i]->
	  start(play_point_value[i][0]-audio_point-offset);
      }
      else {
	if((play_point_value[i][1]-audio_point-offset)>=0) {
	  play_point_state[i]=true;
	  play_point_timer[i]->
	    start(play_point_value[i][1]-audio_point-offset);
	}
      }
      if((i==0)&&(rda->config()->padSegueOverlaps()>0)) {
	play_point_timer[0]->stop();
	play_point_timer[0]->start(play_point_timer[0]->interval()+
				   rda->config()->padSegueOverlaps());;
      }
    }
  }
  if((play_fade_point[1]!=-1)&&(offset<play_fade_point[1])&&
     ((play_fade_down=play_audio_point[1]-play_fade_point[1])>0)) {
    play_fade_timer->start(play_fade_point[1]-play_audio_point[0]-offset);
  }
  if(offset<play_duck_up_point){
    play_duck_timer->start(play_duck_up_point-offset);
  }
  */

  //
  // Calculate Time-Scaled Points
  //
  int scaled_audio_point[2];
  int scaled_fade_point[2];
  for(int i=0;i<2;i++) {
    scaled_audio_point[i]=
      (int)(RD_TIMESCALE_DIVISOR*(double)play_audio_point[i]/
	    (double)play_timescale_speed);
    scaled_fade_point[i]=
      (int)(RD_TIMESCALE_DIVISOR*(double)play_fade_point[i]/
	    (double)play_timescale_speed);
  }

  int scaled_point_value[RDPlayDeck::SizeOf][2];
  for(int i=0;i<RDPlayDeck::SizeOf;i++) {
    for(int j=0;j<2;j++) {
      scaled_point_value[i][j]=
	(int)(RD_TIMESCALE_DIVISOR*(double)play_point_value[i][j]/
	      (double)play_timescale_speed);
    }
  }

  int scaled_duck_up_point=
    (int)(RD_TIMESCALE_DIVISOR*(double)play_duck_up_point/
	  (double)play_timescale_speed);
    
  //
  // Initialize Segue Timers
  //
  play_point_state[RDPlayDeck::Segue]=false;
  if((play_point_value[RDPlayDeck::Segue][0]>=0)&&
     (play_point_value[RDPlayDeck::Segue][1]>=0)&&
     (play_point_value[RDPlayDeck::Segue][1]>
      play_point_value[RDPlayDeck::Segue][0])) {
    // Setup Full Segue
    if((play_point_value[RDPlayDeck::Segue][0]-play_audio_point[0]-offset)>=0) {
      int timer_val = scaled_point_value[RDPlayDeck::Segue][0]-scaled_audio_point[0]-offset;
      play_point_timer[RDPlayDeck::Segue]->start(timer_val);
    }
    else {
      if((play_point_value[RDPlayDeck::Segue][1]-play_audio_point[0]-
	  offset)>=0) {
	play_point_state[RDPlayDeck::Segue]=true;
	int timer_val = scaled_point_value[RDPlayDeck::Segue][1]-scaled_audio_point[0]-offset;
	play_point_timer[RDPlayDeck::Segue]->start(timer_val);
      }
    }
    if(rda->config()->padSegueOverlaps()>0) {
      play_point_timer[RDPlayDeck::Segue]->stop();
      play_point_timer[RDPlayDeck::Segue]->
	start(play_point_timer[RDPlayDeck::Segue]->interval()+
	      rda->config()->padSegueOverlaps());;
    }
  }
  else {
    // Setup "Play Style" Segue
    int timer_val = scaled_audio_point[1]-scaled_audio_point[0]+100;
    play_point_timer[RDPlayDeck::Segue]->start(timer_val);
  }

  //
  // Initialize Hook and Talk Timers
  //
  for(int i=RDPlayDeck::Hook;i<RDPlayDeck::SizeOf;i++) {
    play_point_state[i]=false;
    if((play_point_value[i][0]!=-1)&&
	(play_point_value[i][0]!=play_point_value[i][1])) {
      if((play_point_value[i][0]-play_audio_point[0]-offset)>=0) {
	play_point_timer[i]->
	  start(scaled_point_value[i][0]-scaled_audio_point[0]-offset);
      }
      else {
	if((play_point_value[i][1]-play_audio_point[0]-offset)>=0) {
	  play_point_state[i]=true;
	  play_point_timer[i]->
	    start(scaled_point_value[i][1]-scaled_audio_point[0]-offset);
	}
      }
    }
  }

  //
  // Setup FadeUp and FadeDown Timers
  //
  if((play_fade_point[1]!=-1)&&(offset<play_fade_point[1])&&
     ((play_fade_down=play_audio_point[1]-play_fade_point[1])>0)) {
    play_fade_timer->start(scaled_fade_point[1]-scaled_audio_point[0]-offset);
  }
  if(offset<play_duck_up_point){
    play_duck_timer->start(scaled_duck_up_point-offset);
  }
}


void RDPlayDeck::StopTimers()
{
  for(int i=0;i<RDPlayDeck::SizeOf;i++) {
    if(play_point_timer[i]->isActive()) {
      play_point_timer[i]->stop();
    }
  }
  if(play_fade_timer->isActive()) {
    play_fade_timer->stop();
  }
  if(play_stop_timer->isActive()) {
    play_stop_timer->stop();
  }
  if(play_duck_timer->isActive()) {
    play_duck_timer->stop();
  }
}
