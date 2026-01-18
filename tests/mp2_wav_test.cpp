// mp2_wav_test.cpp
//
// Test MP2 WAV file handling for Rivendell compatibility.
//
// This test validates MP2 WAV files for two critical issues that can
// cause CAED playback failures:
//
// 1. MEXT Energy Flags Issue:
//    Setting mext_left_energy/mext_right_energy flags in the MEXT chunk
//    tells Rivendell to look for per-frame ancillary energy data. But if
//    peak data is in a separate LEVL chunk (as FFmpeg does with -write_peak),
//    LoadEnergy() reads garbage from incorrect offsets, crashing CAED.
//    FIX: Don't set MEXT ancillary_data_def flags when using LEVL chunk.
//
// 2. Block Align Mismatch Issue:
//    The WAV file's block_align field must match what the MAD decoder
//    calculates for MPEG frame size: 144 * bitrate / samplerate.
//    Some encoders incorrectly use a rounding-up formula which produces
//    block_align off by 1 byte, causing frame sync failures.
//    FIX: Use truncating division for block_align calculation.
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

#include <QCoreApplication>

#include <rdcmd_switch.h>
#include <rdwavedata.h>
#include <rdwavefile.h>

#include "mp2_wav_test.h"

MainObject::MainObject(QObject *parent)
  : QObject(parent)
{
  QString filename;
  d_verbose = false;
  d_check_energy = false;
  int errors = 0;
  int warnings = 0;

  //
  // Read Command Options
  //
  RDCmdSwitch *cmd = new RDCmdSwitch("mp2_wav_test", MP2_WAV_TEST_USAGE);
  for (unsigned i = 0; i < cmd->keys(); i++) {
    if (cmd->key(i) == "--filename") {
      filename = cmd->value(i);
      cmd->setProcessed(i, true);
    }
    if (cmd->key(i) == "--verbose") {
      d_verbose = true;
      cmd->setProcessed(i, true);
    }
    if (cmd->key(i) == "--check-energy") {
      d_check_energy = true;
      cmd->setProcessed(i, true);
    }
    if (!cmd->processed(i)) {
      fprintf(stderr, "mp2_wav_test: unknown option \"%s\"\n",
              cmd->key(i).toUtf8().constData());
      exit(1);
    }
  }

  if (filename.isEmpty()) {
    fprintf(stderr, "mp2_wav_test: you must supply --filename=<name>\n");
    exit(1);
  }

  printf("Testing: %s\n", filename.toUtf8().constData());
  printf("=========================================\n\n");

  //
  // Open the wave file
  //
  RDWaveFile *wave = new RDWaveFile(filename);
  RDWaveData *wavedata = new RDWaveData();

  if (!wave->openWave(wavedata)) {
    fprintf(stderr, "ERROR: Unable to open file\n");
    exit(1);
  }

  //
  // Check format
  //
  printf("Format Information:\n");
  printf("  Format Tag: 0x%04X", wave->getFormatTag());
  switch (wave->getFormatTag()) {
    case WAVE_FORMAT_PCM:
      printf(" (PCM)\n");
      break;
    case WAVE_FORMAT_MPEG:
      printf(" (MPEG)\n");
      break;
    case WAVE_FORMAT_VORBIS:
      printf(" (Vorbis)\n");
      break;
    default:
      printf(" (Unknown)\n");
      break;
  }
  printf("  Channels: %d\n", wave->getChannels());
  printf("  Sample Rate: %d Hz\n", wave->getSamplesPerSec());
  printf("  Bits Per Sample: %d\n", wave->getBitsPerSample());
  printf("  Block Align: %d\n", wave->getBlockAlign());
  printf("  Sample Length: %u samples\n", wave->getSampleLength());
  printf("  Time Length: %u ms\n", wave->getTimeLength());

  if (wave->getFormatTag() != WAVE_FORMAT_MPEG) {
    printf("\nNOTE: This is not an MPEG file, MEXT chunk tests not applicable.\n");
    wave->closeWave();
    delete wave;
    delete wavedata;
    exit(0);
  }

  //
  // Check MPEG-specific info
  //
  printf("\nMPEG Information:\n");
  printf("  Head Layer: %d\n", wave->getHeadLayer());
  printf("  Head Bit Rate: %d bps\n", wave->getHeadBitRate());
  printf("  Head Mode: %d\n", wave->getHeadMode());

  //
  // Check Block Align vs expected MPEG frame size
  // The MAD decoder calculates frame size as: 144 * bitrate / samplerate
  // If the WAV file's block_align doesn't match, playback will fail.
  //
  if (wave->getHeadBitRate() > 0 && wave->getSamplesPerSec() > 0) {
    int expected_frame_size = 144 * wave->getHeadBitRate() / wave->getSamplesPerSec();
    int actual_block_align = wave->getBlockAlign();
    
    printf("\nBlock Align Analysis:\n");
    printf("  Actual Block Align: %d\n", actual_block_align);
    printf("  Expected (144 * %d / %d): %d\n", 
           wave->getHeadBitRate(), wave->getSamplesPerSec(), expected_frame_size);
    
    if (actual_block_align != expected_frame_size) {
      printf("\n  *** CRITICAL ERROR ***\n");
      printf("  Block align mismatch! The file has block_align=%d but\n", actual_block_align);
      printf("  the MAD decoder expects frame_size=%d.\n", expected_frame_size);
      printf("  This causes frame synchronization failures during MP2 decoding.\n");
      printf("\n");
      printf("  COMMON CAUSE: Some encoders use rounding-up formula:\n");
      printf("    (144 * bitrate - 1) / samplerate + 1 = %d\n",
             (144 * wave->getHeadBitRate() - 1) / wave->getSamplesPerSec() + 1);
      printf("  But Rivendell's MAD decoder uses truncating division:\n");
      printf("    144 * bitrate / samplerate = %d\n", expected_frame_size);
      printf("\n");
      printf("  FIX: The encoder must use truncating division for block_align.\n");
      errors++;
    } else {
      printf("  Block align OK - matches expected MPEG frame size.\n");
    }
  }

  //
  // Check MEXT chunk
  //
  printf("\nMEXT Chunk Analysis:\n");
  if (!wave->getMextChunk()) {
    printf("  WARNING: No MEXT chunk found (may cause issues with some players)\n");
    warnings++;
  } else {
    printf("  MEXT Chunk Present: YES\n");
    printf("  Homogenous: %s\n", wave->getMextHomogenous() ? "YES" : "NO");
    printf("  Padding Used: %s\n", wave->getMextPaddingUsed() ? "YES" : "NO");
    printf("  Rate Hacked: %s\n", wave->getMextHackedBitRate() ? "YES" : "NO");
    printf("  Free Format: %s\n", wave->getMextFreeFormat() ? "YES" : "NO");
    printf("  Frame Size: %d\n", wave->getMextFrameSize());
    printf("  Ancillary Length: %d\n", wave->getMextAncillaryLength());
    printf("  Left Energy Present: %s\n", 
           wave->getMextLeftEnergyPresent() ? "YES" : "NO");
    printf("  Right Energy Present: %s\n", 
           wave->getMextRightEnergyPresent() ? "YES" : "NO");
    printf("  Ancillary Private: %s\n", 
           wave->getMextPrivateDataPresent() ? "YES" : "NO");

    //
    // Critical check: MEXT energy flags without per-frame data
    //
    if (wave->getMextLeftEnergyPresent() || wave->getMextRightEnergyPresent()) {
      printf("\n  *** CRITICAL WARNING ***\n");
      printf("  The MEXT chunk has energy flags set (left=%s, right=%s).\n",
             wave->getMextLeftEnergyPresent() ? "yes" : "no",
             wave->getMextRightEnergyPresent() ? "yes" : "no");
      printf("  This indicates per-frame ancillary energy data should be present.\n");
      printf("  If this file was created by FFmpeg with -write_peak, the energy\n");
      printf("  data is in a LEVL chunk instead, NOT in per-frame ancillary data.\n");
      printf("  This WILL cause LoadEnergy() to read garbage and may crash CAED!\n");
      printf("\n");
      printf("  FIX: The encoder should NOT set MEXT ancillary_data_def flags\n");
      printf("       when using a LEVL chunk for peak envelope data.\n");
      errors++;
    }
  }

  //
  // Check LEVL chunk
  //
  printf("\nLEVL Chunk Analysis:\n");
  if (wave->hasEnergy()) {
    printf("  Has Energy Data: YES\n");
    printf("  Energy Size: %u frames\n", wave->energySize());
    
    if (d_verbose && wave->energySize() > 0) {
      printf("  First 5 frames:\n");
      for (unsigned i = 0; i < 5 && i < wave->energySize(); i++) {
        if (wave->getChannels() == 1) {
          printf("    Frame %u: %d\n", i, 0xFFFF & wave->energy(i));
        } else {
          printf("    Frame %u: L=%d R=%d\n", i,
                 0xFFFF & wave->energy(2*i),
                 0xFFFF & wave->energy(2*i + 1));
        }
      }
    }
  } else {
    printf("  Has Energy Data: NO\n");
    printf("  (This is normal if no peak envelope was requested)\n");
  }

  //
  // Check fact chunk
  //
  printf("\nFact Chunk Analysis:\n");
  if (wave->getSampleLength() > 0) {
    printf("  Sample Length: %u\n", wave->getSampleLength());
    
    // Sanity check: compare sample length with time length
    unsigned expected_samples = (unsigned)((double)wave->getTimeLength() * 
                                           wave->getSamplesPerSec() / 1000.0);
    int diff = abs((int)wave->getSampleLength() - (int)expected_samples);
    if (diff > wave->getSamplesPerSec()) {  // More than 1 second difference
      printf("  WARNING: Sample length (%u) differs significantly from\n",
             wave->getSampleLength());
      printf("           expected based on time (%u)\n", expected_samples);
      warnings++;
    }
  }

  //
  // Optional: Try to trigger LoadEnergy path
  //
  if (d_check_energy) {
    printf("\nEnergy Load Test:\n");
    printf("  Attempting to access energy data via LoadEnergy path...\n");
    
    // This should trigger GetEnergy() -> LoadEnergy() if not already loaded
    // For MPEG with mext_left_energy set, this will read from wrong offsets
    if (wave->hasEnergy()) {
      unsigned size = wave->energySize();
      printf("  Successfully loaded %u energy frames\n", size);
      
      // Sanity check the values
      bool suspicious = false;
      for (unsigned i = 0; i < size && i < 100; i++) {
        int val = wave->energy(i);
        if (val < 0 || val > 65535) {
          suspicious = true;
          break;
        }
      }
      if (suspicious) {
        printf("  WARNING: Energy values appear suspicious (possible garbage)\n");
        warnings++;
      }
    } else {
      printf("  No energy data available\n");
    }
  }

  //
  // Summary
  //
  printf("\n=========================================\n");
  printf("Summary:\n");
  printf("  Errors: %d\n", errors);
  printf("  Warnings: %d\n", warnings);
  
  if (errors > 0) {
    printf("\n  RESULT: FAIL - This file may cause CAED playback issues!\n");
  } else if (warnings > 0) {
    printf("\n  RESULT: PASS with warnings\n");
  } else {
    printf("\n  RESULT: PASS - File appears compatible with Rivendell\n");
  }

  wave->closeWave();
  delete wave;
  delete wavedata;

  exit(errors > 0 ? 1 : 0);
}


int main(int argc, char *argv[])
{
  QCoreApplication a(argc, argv);
  new MainObject();
  return a.exec();
}
