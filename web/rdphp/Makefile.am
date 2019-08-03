## automake.am
##
## Automake.am for rivendell/web/rdphp
##
##   (C) Copyright 2019 Genesee Media Corporation <bmcglynn@geneseemedia.net>
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
##

install-exec-am:	
	mkdir -p $(DESTDIR)@libexecdir@/rdweb
	mkdir -p $(DESTDIR)@libexecdir@/rdweb/config
	mkdir -p $(DESTDIR)@libexecdir@/rdweb/css
	mkdir -p $(DESTDIR)@libexecdir@/rdweb/includes
	mkdir -p $(DESTDIR)@libexecdir@/rdweb/js
	cp footer.php $(DESTDIR)@libexecdir@/rdweb
	cp header.php $(DESTDIR)@libexecdir@/rdweb
	cp index.php $(DESTDIR)@libexecdir@/rdweb
	cp logout.php $(DESTDIR)@libexecdir@/rdweb
	cp rdlogedit.php $(DESTDIR)@libexecdir@/rdweb
	cp voicetrack.php $(DESTDIR)@libexecdir@/rdweb
	cp welcome.php $(DESTDIR)@libexecdir@/rdweb
	cp config/database.php $(DESTDIR)@libexecdir@/rdweb/config
	cp css/voicetracker.css $(DESTDIR)@libexecdir@/rdweb/css
	cp includes/cart_import.php $(DESTDIR)@libexecdir@/rdweb/includes
	cp includes/dbfunctions.php $(DESTDIR)@libexecdir@/rdweb/includes
	cp includes/functions.php $(DESTDIR)@libexecdir@/rdweb/includes
	cp js/jquery-2.2.2.min.js $(DESTDIR)@libexecdir@/rdweb/js
	cp js/jscolor.min.js $(DESTDIR)@libexecdir@/rdweb/js
	cp js/Mp3LameEncoder.min.js.mem $(DESTDIR)@libexecdir@/rdweb/js
	cp js/OggVorbisEncoder.min.js.mem $(DESTDIR)@libexecdir@/rdweb/js
	cp js/RDRecorder.js $(DESTDIR)@libexecdir@/rdweb/js
	cp js/WebAudioRecorder.min.js $(DESTDIR)@libexecdir@/rdweb/js
	cp js/WebAudioRecorderMp3.min.js $(DESTDIR)@libexecdir@/rdweb/js
	cp js/WebAudioRecorderOgg.min.js $(DESTDIR)@libexecdir@/rdweb/js
	cp js/WebAudioRecorderWav.min.js $(DESTDIR)@libexecdir@/rdweb/js

uninstall-local:	
	rm -f $(DESTDIR)@libexecdir@/rdweb/index.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/footer.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/header.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/logout.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/rdlogedit.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/voicetrack.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/welcome.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/config/database.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/css/voicetracker.css
	rm -f $(DESTDIR)@libexecdir@/rdweb/includes/cart_import.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/includes/dbfunctions.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/includes/functions.php
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/jquery-2.2.2.min.js 
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/jscolor.min.js
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/Mp3LameEncoder.min.js.mem
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/OggVorbisEncoder.min.js.mem
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/RDRecorder.js
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/WebAudioRecorder.min.js
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/WebAudioRecorderMp3.min.js
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/WebAudioRecorderOgg.min.js
	rm -f $(DESTDIR)@libexecdir@/rdweb/js/WebAudioRecorderWav.min.js

EXTRA_DIST = index.php\
             footer.php\
             header.php\
             logout.php\
             rdlogedit.php\
             voicetrack.php\
             welcome.php\
             config/database.php\
             css/voicetracker.css\
             includes/cart_import.php\
             includes/dbfunctions.php\
             includes/functions.php\
             js/jquery-2.2.2.min.js\
             js/jscolor.min.js\
             js/Mp3LameEncoder.min.js.mem\
             js/OggVorbisEncoder.min.js.mem\
             js/RDRecorder.js\
             js/WebAudioRecorder.min.js\
             js/WebAudioRecorderMp3.min.js\
             js/WebAudioRecorderOgg.min.js\
             js/WebAudioRecorderWav.min.js
 
CLEANFILES = *~
MAINTAINERCLEANFILES = *~\
                       Makefile.in
