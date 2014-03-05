###########################################
# Makefile
###########################################
MACHINE := $(shell uname -m)

CUSTOM_ASTPATH=/cygdrive/d/svn/asterisk-1.4.x-videocaps/include

ifeq ($(MACHINE), x86_64)
libdir = /usr/lib64
endif
ifeq ($(MACHINE), i686)
libdir = /usr/lib
endif

LOG             = yes
DEBUG           = yes


INCLUDE=-I.
INCLUDE+= -I$(CUSTOM_ASTPATH)

#DEBUG

ifeq ($(DEBUG),yes)
	TAG=debug
	OPTS+= -g -O0
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
#G711DIR=g711
#G711OBJ=g711.o pcmucodec.o pcmacodec.o

H263DIR=h263
H263OBJ=h263packet.o h263codec.o mpeg4codec.o h263-1996codec.o

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

#VP8DIR=vp8
#VP8OBJ=vp8encoder.o vp8decoder.o

#GSMDIR=gsm
#GSMOBJ=gsmcodec.o

#SPEEXDIR=speex
#SPEEXOBJ=speexcodec.o

#NELLYDIR=nelly
#NELLYOBJ=NellyCodec.o

#G722DIR=g722
#G722OBJ=g722codec.o


OBJS=audio.o video.o transcoder.o framescaler.o utf8parser.o  avcdescriptor.o red.o textencoder.o log.o
OBJS+=$(H263OBJ) $(H264OBJ)
OBJS+=mp4format.o framebuffer.o

VPATH =  %.cpp $(H263DIR)
VPATH += %.cpp $(H264DIR)


LDFLAGS = -lavcodec -lswscale -lavformat -lavutil -lx264  -lbz2 -lssl -lcrypto -lpthread  

#For abyss
#OPTS 	+= -D_UNIX -D__STDC_CONSTANT_MACROS -fPIC
OPTS 	+= -D_UNIX -D__STDC_CONSTANT_MACROS 
CFLAGS  += $(INCLUDE) $(OPTS)
CXXFLAGS+= $(INCLUDE) $(OPTS)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

############################################
#Targets
############################################
all: libmedkit.a

libmedkit.a: $(OBJS)
	ar r $@ $(OBJS)

clean:
	rm -f $(OBJS) libmedkit.a

install: libmedkit.a
	rm -rf $(INSTALLPREFIX)/opt/ives/include
	install -d $(INSTALLPREFIX)/opt/ives/lib
	install -d $(INSTALLPREFIX)/opt/ives/include
	install libmedkit.a $(INSTALLPREFIX)/opt/ives/lib
	cp -rp medkit $(INSTALLPREFIX)/opt/ives/include
	cp -rp astmedkit $(INSTALLPREFIX)/opt/ives/include
	echo LIBMEDKIT installer