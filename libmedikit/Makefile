###########################################
# Makefile
###########################################
MACHINE := $(shell uname -m)

#CUSTOM_ASTPATH=/cygdrive/d/svn/asterisk-1.4.x-videocaps/include

ifeq ($(MACHINE), x86_64)
libdir = /usr/lib64
endif
ifeq ($(MACHINE), i686)
libdir = /usr/lib
endif

LOG             = yes
DEBUG           = yes
# ASTERISK=yes : compile les objets couplés à Asterisk (transcoder, mp4format,
# framebuffer, frameutils, astlog), qui exigent les en-têtes <asterisk/...>
# (CUSTOM_ASTPATH). Mettre ASTERISK=no pour bâtir libmedkit.a sans Asterisk.
ASTERISK        = yes


INCLUDE=-I. -I/usr/include/ffmpeg
# En-têtes des dépendances statiques (mp4v2, srtp...) bâties par install.ksh.
INCLUDE+= -I../../../staticdeps/include
INCLUDE+= -I$(CUSTOM_ASTPATH)

#DEBUG

ifeq ($(DEBUG),yes)
	TAG=debug
#	OPTS+= -g
else
	OPTS+= -O3
	TAG=release
endif

#LOG
ifeq ($(LOG),yes)
	OPTS+= -DLOG_
endif


############################################
#Modules
############################################
G711DIR=g711
G711OBJ=g711.o pcmucodec.o pcmacodec.o

H263DIR=h263
H263OBJ=h263packet.o h263codec.o mpeg4codec.o

ifeq ($(FLV1PARSER),yes)
	FLV1DIR=flv1
	FLV1OBJ=flv1codec.o flv1Parser.o
	OPTS+= -DFLV1PARSER
else
	FLV1DIR=flv1
	FLV1OBJ=flv1codec.o
endif

#VP6DIR=vp6
#VP6OBJ=vp6decoder.o

H264DIR=h264
H264OBJ=h264encoder.o h264decoder.o h264depacketizer.o

# VP8 : décodeur natif ffmpeg + encodeur via le wrapper libvpx de ffmpeg
# (ffmpeg n'a pas d'encodeur VP8 natif, mais expose le wrapper libvpx).
VP8DIR=vp8
VP8OBJ=vp8decoder.o vp8encoder.o

GSMDIR=gsm
GSMOBJ=gsmcodec.o

SPEEXDIR=speex
SPEEXOBJ=speexcodec.o

#NELLYDIR=nelly
#NELLYOBJ=NellyCodec.o

OPUSDIR=opus
OPUSOBJ=opuscodec.o

G722DIR=g722
G722OBJ=g722codec.o g7221codec.o

AACDIR=aac
AACOBJ=aacencoder.o

AMRDIR=amr
AMROBJ=amrcodec.o

NELLYDIR=nelly
NELLYOBJ=nellycodec.o

OBJS=audio.o video.o framescaler.o utf8parser.o  avcdescriptor.o red.o textencoder.o log.o media.o
OBJS+=audiosilence.o $(H263OBJ) $(H264OBJ) $(G711OBJ) $(G722OBJ) $(AACOBJ) $(AMROBJ) $(NELLYOBJ) $(GSMOBJ) $(OPUSOBJ) $(SPEEXOBJ) $(VP8OBJ) ffvideocodec.o ffaudiocodec.o
OBJS+=mp4track.o logo.o  picturestreamer.o mp4recorder.o mp4reader.o

# Objets dépendant des en-têtes Asterisk, exclus si ASTERISK=no.
ASTOBJ=transcoder.o mp4format.o framebuffer.o frameutils.o astlog.o
ifeq ($(ASTERISK),yes)
OBJS+= $(ASTOBJ)
endif

VPATH =  %.cpp $(H263DIR)
VPATH += %.cpp $(H264DIR)
VPATH += %.cpp $(G711DIR)
VPATH += %.c $(G711DIR)
VPATH += %.cpp $(G722DIR)
VPATH += %.cpp $(AACDIR)
VPATH += %.cpp $(AMRDIR)
VPATH += %.cpp $(NELLYDIR)
VPATH += %.cpp $(GSMDIR)
VPATH += %.cpp $(SPEEXDIR)
VPATH += %.cpp $(OPUSDIR)
VPATH += %.cpp $(VP8DIR)
LDFLAGS = -lavcodec -lswscale -lavformat -lavutil -lswresample -lx264  -lbz2 -lssl -lcrypto -lpthread

#For abyss
OPTS 	+= -D_UNIX -D__STDC_CONSTANT_MACROS -fPIC -g -Wno-narrowing
#OPTS 	+= -D_UNIX -D__STDC_CONSTANT_MACROS
CFLAGS  += $(INCLUDE) $(OPTS)
#CXXFLAGS+= $(INCLUDE) $(OPTS) -std=c++0x
CXXFLAGS+= $(INCLUDE) $(OPTS)

all: libmedkit.a

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

framebuffer.o: framebuffer.cpp
	$(CXX) $(CXXFLAGS) -std=c++0x -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

############################################
#Targets
############################################

libmedkit.a: $(OBJS)
	ar r $@ $(OBJS)

mp4recorder.o: medkit/mp4recorder.h medkit/media.h
mp4reader.o: medkit/mp4reader.h medkit/media.h
mp4format.o: astmedkit/mp4format.h medkit/mp4recorder.h medkit/mp4reader.h medkit/media.h

clean:
	rm -f $(OBJS) libmedkit.a


install32:
	install -d $(DESTDIR)/opt/ives/lib
	install libmedkit.a $(DESTDIR)/opt/ives/lib
	echo "32 bits install"

install64:
	install -d $(DESTDIR)/opt/ives/lib64
	install libmedkit.a $(DESTDIR)/opt/ives/lib64
	echo "64 bits install"


install: libmedkit.a
	echo LIBMEDKIT installer
	rm -rf $(DESTDIR)/opt/ives/include/medkit $(DESTDIR)/opt/ives/include/astmedkit
	install -d $(DESTDIR)/opt/ives/include
	cp -rp medkit $(DESTDIR)/opt/ives/include
	cp -rp astmedkit $(DESTDIR)/opt/ives/include
	@if [ "`uname -m`" == "x86_64" ] ; then make install64 ; else make install32 ; fi

uninstall:
	rm -rf $(DESTDIR)/opt/ives/include/medkit $(DESTDIR)/opt/ives/include/astmedkit
	rm -f $(DESTDIR)/opt/ives/lib/libmedkit.a -f $(DESTDIR)/opt/ives/lib64/libmedkit.a

app_conference:
	sudo make install
	cd ../../app_conference; make
	sudo /usr/sbin/asterisk -rx 'restart now'

testsps.o: medkit/bitstream.h

#testsps: testsps.o libmedkit.a
#	g++ -o testsps testsps.o libmedkit.a -l mp4v2
