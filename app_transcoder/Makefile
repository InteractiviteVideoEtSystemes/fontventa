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
include ../Makeinclude

#
# app_transcoder defines which can be passed on the command-line
#

INSTALL_PREFIX := /usr
INSTALL_MODULES_DIR := $(INSTALL_PREFIX)/lib/asterisk/modules

ASTERISK_INCLUDE_DIR := $(ASTERISKDIR)/include
MACHINE := $(shell uname -m)
ifeq ($(MACHINE), x86_64)
libdir = /usr/lib64
endif
ifeq ($(MACHINE), i686)
libdir = /usr/lib
endif


#
# app_transcoder objects to build
#

OBJS = app_transcoder.o
SHAREDOS = app_transcoder.so

#
# standard compile settings
#

PROC = $(shell uname -m)
INSTALL = install
CC = gcc 

INCLUDE = 
LIBS = -L$(libdir)  -Wl,-Bstatic -lswscale  -lavcodec -lavutil   -Wl,-Bdynamic
DEBUG := -g 

CFLAGS = -fPIC -fpic  -DAST_MODULE=\"app_transcoder\" -pipe -Wall $(DEBUG) -DVIDEOCAPS

#
# targets
#

all: $(SHAREDOS) 

clean:
	rm -f *.so *.o $(OBJS)

app_transcoder.so : $(OBJS)
	$(CC)  -g -shared -Xlinker -x  -o $@ $(OBJS)  $(LIBS) -fPIC 


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

