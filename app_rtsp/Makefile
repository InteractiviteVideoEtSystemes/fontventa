# $Id: Makefile,v 1.1 2008-06-12 20:55:17 borja.sixto Exp $

#
# Makefile, based on the Asterisk Makefile, Coypright (C) 1999, Mark Spencer
#
# Copyright (C) 2007 i6net
#
# i6net support@i6net.com
#
# This program is free software and may be modified and 
# distributed under the terms of the GNU Public License.
#

.EXPORT_ALL_VARIABLES:

#
# app_mp4 defines which can be passed on the command-line
#

INSTALL_PREFIX := /usr
INSTALL_MODULES_DIR := $(INSTALL_PREFIX)/lib/asterisk/modules

ASTERISK_INCLUDE_DIR := $(ASTERISKDIR)/include


#
# app_mp4 objects to build
#

OBJS = app_rtsp.o
SHAREDOS = app_rtsp.so

#
# standard compile settings
#

PROC = $(shell uname -m)
INSTALL = install
CC = gcc

INCLUDE = -I$(ASTERISK_INCLUDE_DIR)
LIBS = 
DEBUG := -g 

CFLAGS = -DAST_MODULE=\"app_rtsp\" -pipe -Wall -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE -fPIC

#
# targets
#

all: $(SHAREDOS) 

clean:
	rm -f *.so *.o $(OBJS)

app_rtsp.so : $(OBJS)
	$(CC) -pg -shared -Xlinker -x -o $@ $(OBJS) $(LIBS) 

install: all
	@if [ "`uname -m`" == "x86_64" ] ; then make install64 ; else make install32 ; fi

install32:
	mkdir -p $(DESTDIR)/usr/lib/asterisk/modules
	cp $(SHAREDOS) $(DESTDIR)/usr/lib/asterisk/modules
	echo "32 bits install"

install64:
	mkdir -p $(DESTDIR)/usr/lib64/asterisk/modules
	cp $(SHAREDOS) $(DESTDIR)/usr/lib64/asterisk/modules
	echo "64 bits install"

