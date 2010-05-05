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


ASTERISK_INCLUDE_DIR := $(ASTERISKDIR)/include
LIBH324M_DIR := ../libh324m

RPATH := -Wl,-rpath=/usr/lib


#
# app_mp4 objects to build
#

OBJS = app_h324m.o
SHAREDOS = app_h324m.so

#
# standard compile settings
#

PROC = $(shell uname -m)
INSTALL = install
CC = gcc
LIBH324M_DIR=../libh324m
INCLUDE = -I$(LIBH324M_DIR)/include
LIBS = -L$(LIBH324M_DIR) -lh324m
DEBUG := -g 

CFLAGS = -DAST_MODULE=\"app_h324m\" -pipe -Wall -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE -fPIC

#
# targets
#

all: $(SHAREDOS) 

clean:
	rm -f *.so *.o $(OBJS)

app_h324m.so : $(OBJS)
	$(CC) -pg -shared -Xlinker -x -o $@ $(OBJS) $(LIBS) $(RPATH)

install: all
	@if [ ! -r $(DESTDIR)/etc/asterisk/h324m.conf ]; then mkdir -p $(DESTDIR)/etc/asterisk ; cp h324m.conf $(DESTDIR)/etc/asterisk ; fi
	@if [ "`uname -m`" == "x86_64" ] ; then make install64 ; else make install32 ; fi

install32:
	mkdir -p $(DESTDIR)/usr/lib/asterisk/modules
	cp $(SHAREDOS) $(DESTDIR)/usr/lib/asterisk/modules
	echo "32 bits install"

install64:
	mkdir -p $(DESTDIR)/usr/lib64/asterisk/modules
	cp $(SHAREDOS) $(DESTDIR)/usr/lib64/asterisk/modules
	echo "64 bits install"

