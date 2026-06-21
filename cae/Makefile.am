## Makefile.am
##
## Core Audio Engine Makefile.am for Rivendell
##
## Copyright 2002-2025 Fred Gleason <fredg@paravelsystems.com>
##
##   This program is free software; you can redistribute it and/or modify
##   it under the terms of the GNU General Public License version 2 as
##   published by the Free Software Foundation.
##
##   This program is distributed in the hope that it will be useful,
##   but WITHOUT ANY WARRANTY; without even the implied warranty of
##   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##   GNU General Public License for more details.
##
##   You should have received a copy of the GNU General Public
##   License along with this program; if not, write to the Free Software
##   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
##
## Use automake to process this into a Makefile.in

AM_CPPFLAGS = -Wall -I$(top_srcdir)/lib -Wno-strict-aliasing -std=c++11 -fPIC @QT5_CFLAGS@ @MUSICBRAINZ_CFLAGS@ @IMAGEMAGICK_CFLAGS@ @GST_CFLAGS@ @LIBJACK_CFLAGS@
LIBS = -L$(top_srcdir)/lib
MOC = @QT_MOC@

# The dependency for qt's Meta Object Compiler (moc)
moc_%.cpp:	%.h
	$(MOC) $< -o $@

sbin_PROGRAMS = caed

dist_caed_SOURCES = cae.cpp cae.h\
                    cae_server.cpp cae_server.h\
                    gst_pipeline_manager.cpp gst_pipeline_manager.h\
                    gst_playback_stream.cpp gst_playback_stream.h\
                    gst_record_stream.cpp gst_record_stream.h\
                    playsession.cpp playsession.h

nodist_caed_SOURCES = moc_cae.cpp\
                      moc_cae_server.cpp\
                      moc_gst_pipeline_manager.cpp\
                      moc_gst_playback_stream.cpp\
                      moc_gst_record_stream.cpp

caed_LDADD = @LIB_RDLIBS@\
             @GST_LIBS@\
             @LIBJACK_LIBS@\
             @LIBVORBIS@\
             @QT5_LIBS@\
             @MUSICBRAINZ_LIBS@\
             @IMAGEMAGICK_LIBS@

CLEANFILES = *~\
             moc_*

MAINTAINERCLEANFILES = *~\
                       Makefile.in
