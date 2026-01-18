// mp2_wav_test.h
//
// Test MP2 WAV file handling, particularly MEXT chunk and LEVL chunk
// interaction that can cause playback failures in CAED.
//
// This test was created to validate fixes for MP2 WAV files generated
// by FFmpeg with peak envelope data. The issue was that setting the
// mext_left_energy/mext_right_energy flags in the MEXT chunk tells
// Rivendell to look for per-frame ancillary energy data, but FFmpeg
// writes peak data to a separate LEVL chunk instead.
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

#ifndef MP2_WAV_TEST_H
#define MP2_WAV_TEST_H

#include <QObject>

#define MP2_WAV_TEST_USAGE "[options]\n\n" \
  "Test MP2 WAV file handling for Rivendell compatibility.\n\n" \
  "This test validates WAV files containing MP2 audio with peak envelope\n" \
  "data, checking for issues that can cause CAED playback failures.\n\n" \
  "Options are:\n" \
  "--filename=<wav-file>\n" \
  "     WAV file to test.\n\n" \
  "--verbose\n" \
  "     Print detailed chunk information.\n\n" \
  "--check-energy\n" \
  "     Attempt to load energy data (may trigger the MEXT bug).\n\n"

class MainObject : public QObject
{
  Q_OBJECT
 public:
  MainObject(QObject *parent=0);

 private:
  bool d_verbose;
  bool d_check_energy;
};

#endif  // MP2_WAV_TEST_H
