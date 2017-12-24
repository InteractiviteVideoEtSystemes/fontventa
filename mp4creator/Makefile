# Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
# 2003, 2004, 2005  Free Software Foundation, Inc.
# This Makefile.in is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

INSTALL_PREFIX := /usr
OBJS=aac.o amr.o h263.o h264.o mp3.o mp4v.o mpeg.o mp4creator.o

ifeq ($(MACHINE), x86_64)
libdir=lib64
endif
ifeq ($(MACHINE), i686)
libdir=lib
endif

CXXFLAGS=-I. -I../mp4av
LDFLAGS=../mp4av/libmp4av.a -lmp4v2

all: mp4creator


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

mp4creator: $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f mp4creator $(OBJS)

install: mp4creator
	cp mp4creator $(DESTDIR)/usr/bin
	
uninstall:
	rm -f  $(DESTDIR)/usr/bin/mp4creator
