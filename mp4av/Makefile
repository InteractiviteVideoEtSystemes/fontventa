# Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
# 2003, 2004, 2005  Free Software Foundation, Inc.
# This Makefile.in is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

OBJS=aac.o ac3.o adts.o amr.o audio.o audio_hinters.o g711.o h264.o href.o l16.o mbs.o mp3.o mpeg3.o mpeg4.o rfc2250.o rfc2429.o rfc3016.o rfc3119.o rfc3267.o rfccrypto.o rfch264.o rfcisma.o mpeg2ps.o mpeg2ps_util.o

CXXFLAGS=-I.
CFLAGS=-I.

all: libmp4av.a


%.o: %.c mp4av.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp mp4av.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

libmp4av.a: $(OBJS)
	ar r $@ $(OBJS)

clean:
	rm -f libmp4av.a $(OBJS)

