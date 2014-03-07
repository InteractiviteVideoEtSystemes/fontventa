# $Id: Makefile,v 1.4 2008-12-08 16:19:51 borja.sixto Exp $

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
# app_mp4 defines which can be passed on the command-line
#

INSTALL_PREFIX := /usr
INSTALL_MODULES_DIR := $(INSTALL_PREFIX)/lib/asterisk/modules

ASTERISK_INCLUDE_DIR := $(ASTERISKDIR)/include




#
# app_mp4 objects to build
#

OBJS = app_mp4.o
SHAREDOS = app_mp4.so

#
# standard compile settings
#

PROC = $(shell uname -m)
INSTALL = install
CC = gcc  

LIBS = -L../libmedikit/lib -Wl,-Bstatic -lmedkit -lmp4v2 -lavcodec -lavformat -lswscale -lx264  -Wl,-Bdynamic -lspeexdsp
DEBUG := -g 

CFLAGS = -I../libmedikit/include -fPIC  -DAST_MODULE=\"app_mp4\" -pipe -Wall -Wmissing-prototypes -Wmissing-declarations $(DEBUG) -D_REENTRANT -DVIDEOCAPS

#
# targets
#

all: $(SHAREDOS) 

clean:
	rm -f *.so *.o $(OBJS)

app_mp4.so : $(OBJS)
	$(CC) -pg -shared -Xlinker -x -o $@ $(OBJS) $(LIBS) $(RPATH)

install: all
	@if [ "`uname -m`" == "x86_64" ] ; then make install64 ; else make install32 ; fi

install32:
	install -d $(DESTDIR)/usr/lib/asterisk/modules
	install $(SHAREDOS) $(DESTDIR)/usr/lib/asterisk/modules
	echo "32 bits install"

install64:
	install -d $(DESTDIR)/usr/lib64/asterisk/modules
	install $(SHAREDOS) $(DESTDIR)/usr/lib64/asterisk/modules
	echo "64 bits install"

