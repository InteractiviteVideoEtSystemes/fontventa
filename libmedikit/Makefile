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


# ffmpeg : flags demandés à pkg-config, comme le dépôt le fait déjà pour
# Magick++, webrtc-audio-processing et gtest. Un ffmpeg installé hors préfixe
# système se trouve alors par PKG_CONFIG_PATH, sans toucher à ce Makefile —
# c'est précisément ce dont le portage ffmpeg 9 a besoin (voir, à la racine du
# dépôt mediaserver, ffmpeg9_migration_plan.md).
FFMPEG_MODULES = libavcodec libswscale libavformat libavutil libswresample
FFMPEG_CFLAGS := $(shell pkg-config --cflags $(FFMPEG_MODULES) 2>/dev/null)
FFMPEG_LIBS   := $(shell pkg-config --libs   $(FFMPEG_MODULES) 2>/dev/null)

# Ce qu'annoncera libmedkit.pc (cf. la cible plus bas) : ffmpeg est une
# dépendance PUBLIQUE — ffvideocodec.h inclut <libavcodec/avcodec.h> — donc
# Requires:, et non Requires.private:.
PC_REQUIRES    = $(FFMPEG_MODULES)
PC_CFLAGS_XTRA =
PC_LIBS_XTRA   =

# Repli sur les chemins historiques si pkg-config ignore ffmpeg : des flags
# vides donneraient une erreur d'en-tête introuvable, illisible et très loin de
# sa cause. Le .pc porte alors les flags en clair plutôt qu'un Requires: vers
# des modules absents.
ifeq ($(strip $(FFMPEG_LIBS)),)
$(warning pkg-config ne connait pas ffmpeg [$(FFMPEG_MODULES)] : repli sur /usr/include/ffmpeg. Renseigner PKG_CONFIG_PATH pour un ffmpeg hors prefixe systeme.)
FFMPEG_CFLAGS := -I/usr/include/ffmpeg
FFMPEG_LIBS   := -lavcodec -lswscale -lavformat -lavutil -lswresample
PC_REQUIRES    =
PC_CFLAGS_XTRA = $(FFMPEG_CFLAGS)
PC_LIBS_XTRA   = $(FFMPEG_LIBS)
endif

# Surcharges explicites, portées par install.ksh jusqu'ici — il écrasait INCLUDE
# en ligne de commande, ce qui rendait ces chemins invisibles depuis ce fichier
# et les perdait sur un `make` direct. Elles vivent désormais ici, et
# s'utilisent aussi bien en variable d'environnement qu'en argument de make.
#   FFMPEGINC : court-circuite pkg-config pour les EN-TÊTES ffmpeg seulement
#               (les bibliothèques restent celles de pkg-config, comme avant) ;
#   MP4V2INC  : en-têtes des dépendances statiques (mp4v2, srtp...) bâties par
#               install.ksh dans staticdeps/. Le défaut relatif est équivalent
#               au chemin absolu qu'injectait install.ksh, make tournant en -C.
FFMPEGINC ?=
MP4V2INC  ?= -I../../../staticdeps/include

INCLUDE=-I.
ifneq ($(strip $(FFMPEGINC)),)
INCLUDE+= $(FFMPEGINC)
else
INCLUDE+= $(FFMPEG_CFLAGS)
endif
INCLUDE+= $(MP4V2INC)
# CUSTOM_ASTPATH n'est renseigné qu'en build Asterisk. Sans cette garde, un
# `-I` SANS ARGUMENT restait dans la ligne de commande, et gcc prend alors
# l'option suivante comme répertoire : `-I -DLOG_` déclarait un include nommé
# "-DLOG_" et LOG_ n'était jamais défini. Invisible jusqu'ici parce
# qu'install.ksh écrasait INCLUDE — il ne le fait plus.
ifneq ($(strip $(CUSTOM_ASTPATH)),)
INCLUDE+= -I$(CUSTOM_ASTPATH)
endif

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
G711OBJ=g711codec.o

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
VP8OBJ=vp8decoder.o vp8encoder.o vp8depacketizer.o vp8frameheader.o

# AV1 : encodeur via le wrapper libsvtav1 (temps-réel), décodeur via libdav1d, et
# les deux sens du transport RTP maison (av1obu.o : leb128 + parcours OBU partagé ;
# av1depacketizer.o : RTP -> OBU ; av1packetizer.o : OBU -> RTP), sans dépendance
# ffmpeg et testés seuls.
AV1DIR=av1
AV1OBJ=av1codec.o av1obu.o av1depacketizer.o av1packetizer.o

GSMDIR=gsm
GSMOBJ=gsmcodec.o

SPEEXDIR=speex
SPEEXOBJ=speexcodec.o

#NELLYDIR=nelly
#NELLYOBJ=NellyCodec.o

OPUSDIR=opus
OPUSOBJ=opuscodec.o

G722DIR=g722
G722OBJ=g722codec.o

AACDIR=aac
AACOBJ=aacencoder.o aacdecoder.o

AMRDIR=amr
AMROBJ=amrcodec.o

NELLYDIR=nelly
NELLYOBJ=nellycodec.o

OBJS=audio.o video.o codecs.o negotiator.o videorescaler.o utf8parser.o  avcdescriptor.o red.o text2subtitle.o log.o media.o
OBJS+=audiosilence.o $(H263OBJ) $(H264OBJ) $(G711OBJ) $(G722OBJ) $(AACOBJ) $(AMROBJ) $(NELLYOBJ) $(GSMOBJ) $(OPUSOBJ) $(SPEEXOBJ) $(VP8OBJ) $(AV1OBJ) ffvideocodec.o ffaudiocodec.o
OBJS+=mp4track.o logo.o  picturestreamer.o mp4writer.o ffmp4reader.o
# STUN : rapatrie du mediaserver (il en existait ici une copie morte et perimee,
# qui ignorait IPv6 et l'API OpenSSL 3). Une seule implementation desormais, ici,
# consommee par le mcu via medkit/stunmessage.h. crc32calc suit : le FINGERPRINT
# STUN est son unique appelant.
OBJS+=stunmessage.o crc32calc.o

# Objets dépendant des en-têtes Asterisk, exclus si ASTERISK=no.
# framescaler.o : dernier utilisateur = transcoder.cpp (BYTE*), donc réservé au
# build Asterisk ; le reste de la bibliothèque (et le mcu) passe par
# VideoRescaler (graphe avfilter). Porter transcoder.cpp sur VideoRescaler lors
# d'une campagne Asterisk, puis supprimer framescaler.{h,cpp}.
ASTOBJ=transcoder.o mp4format.o framebuffer.o frameutils.o astlog.o framescaler.o
ifeq ($(ASTERISK),yes)
OBJS+= $(ASTOBJ)
# En build Asterisk, libmedkit.a finit dans des modules chargés par dlopen().
# Or g++ émet des symboles STB_GNU_UNIQUE pour les statiques locales des
# fonctions inline/templates (ex. std::piecewise_construct dès qu'on utilise
# std::map) et la glibc marque NODELETE tout .so qui en définit : dlclose()
# rend alors 0 sans jamais décharger, et la boucle « while (!dlclose(lib)); »
# de load_dynamic_module (asterisk 1.4) tourne à l'infini au démarrage.
OPTS+= -fno-gnu-unique
endif

VPATH =  %.cpp $(H263DIR)
VPATH += %.cpp $(H264DIR)
VPATH += %.cpp $(G711DIR)
VPATH += %.cpp $(G722DIR)
VPATH += %.cpp $(AACDIR)
VPATH += %.cpp $(AMRDIR)
VPATH += %.cpp $(NELLYDIR)
VPATH += %.cpp $(GSMDIR)
VPATH += %.cpp $(SPEEXDIR)
VPATH += %.cpp $(OPUSDIR)
VPATH += %.cpp $(VP8DIR)
VPATH += %.cpp $(AV1DIR)
# Plus de -lx264 : l'encodeur H264 passe par ffmpeg (wrapper libx264 ou
# h264_vaapi), aucun appel direct à l'API x264 ne subsiste dans la lib.
LDFLAGS = $(FFMPEG_LIBS) -lbz2 -lssl -lcrypto -lpthread

#For abyss
OPTS 	+= -D_UNIX -D__STDC_CONSTANT_MACROS -fPIC -g -Wno-narrowing

# ASAN=yes : AddressSanitizer, meme usage et meme piege que dans mcu/Makefile
# (objets dans le meme repertoire : rebatir sans ASAN=yes ensuite).
ifeq ($(ASAN),yes)
	OPTS    += -fsanitize=address -fno-omit-frame-pointer
	LDFLAGS += -fsanitize=address
endif
#OPTS 	+= -D_UNIX -D__STDC_CONSTANT_MACROS
CFLAGS  += $(INCLUDE) $(OPTS)
#CXXFLAGS+= $(INCLUDE) $(OPTS) -std=c++0x
CXXFLAGS+= $(INCLUDE) $(OPTS)

all: libmedkit.a libmedkit.pc

# libmedkit.pc — libmedikit n'est pas installée : elle est bâtie DANS l'arbre et
# liée par chemin complet. Le fichier porte donc des chemins absolus vers ce
# répertoire, et se consomme par PKG_CONFIG_PATH (motif « uninstalled » des
# autotools) :
#
#     PKG_CONFIG_PATH=$(CURDIR) pkg-config --cflags --libs libmedkit
#
# Il rend au consommateur les -I de ffmpeg par la chaîne Requires:, ce qui
# retire la dernière raison qu'a mcu/Makefile de coder /usr/include/ffmpeg
# en dur (deux occurrences y subsistent, cf. ffmpeg9_migration_plan.md §4).
#
# Régénéré à chaque build et NON versionné (.gitignore) : le préfixe suit le
# clone, il n'aurait aucun sens dans l'historique. Dépend du Makefile, seul
# endroit d'où sortent version, modules et flags de repli.
MEDKIT_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo 1.5.1)

libmedkit.pc: Makefile
	@printf '%s\n' \
		'prefix=$(CURDIR)' \
		'libdir=$${prefix}' \
		'includedir=$${prefix}' \
		'' \
		'Name: libmedkit' \
		'Description: IVeS media and codec library (fontventa libmedikit), built in tree' \
		'Version: $(MEDKIT_VERSION)' \
		'Requires: $(PC_REQUIRES)' \
		'Libs: -L$${libdir} -lmedkit $(PC_LIBS_XTRA)' \
		'Libs.private: -lbz2 -lssl -lcrypto -lpthread' \
		'Cflags: -I$${includedir} $(PC_CFLAGS_XTRA)' > $@
	@echo "libmedkit.pc genere -> PKG_CONFIG_PATH=$(CURDIR)"

# Suivi des dependances d'en-tetes, comme mcu/Makefile. Sans lui, ajouter un
# membre a une classe de codec ne recompilait PAS les appelants de son
# constructeur : video.cpp garde l'ancien sizeof dans son `new`, alloue trop peu,
# et le constructeur ecrit au-dela -- corruption du tas a la creation du codec,
# aleatoire et sans rapport visible avec la modification. Diagnostique le
# 2026-08-12 sur AV1Decoder (ajout du depaquetiseur), ou seuls av1codec.o et le
# nouvel objet etaient reconstruits.
DEPFLAGS = -MMD -MP

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

framebuffer.o: framebuffer.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -std=c++0x -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# wildcard plutot que $(OBJS:.o=.d) : les .d d'un build precedent avec un autre
# ASTERISK= restent valables et doivent etre pris en compte.
-include $(wildcard *.d)

############################################
#Targets
############################################

libmedkit.a: $(OBJS)
	ar r $@ $(OBJS)

mp4writer.o: medkit/mp4writer.h medkit/media.h
ffmp4reader.o: medkit/ffmp4reader.h medkit/media.h
mp4format.o: astmedkit/mp4format.h medkit/mp4writer.h medkit/ffmp4reader.h medkit/media.h

clean:
	rm -f $(OBJS) libmedkit.a libmedkit.pc
	rm -f *.d
	rm -f tests/runtests tests/*.o tests/.libmedkit.stamp


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

# Harnais de test hors-ligne pour Mp4FfReader (cf. ffmpeg_mp4reader_plan.md P0) :
# ouverture, métadonnées, lecture cadencée, seek. Ne dépend ni d'Asterisk ni du
# mediaserver. Usage : make ffmp4probe && ./ffmp4probe fichier.mp4 [seekMs]
ffmp4probe: tools/ffmp4probe.cpp libmedkit.a
	$(CXX) $(CXXFLAGS) tools/ffmp4probe.cpp libmedkit.a -o $@ \
		-L../../../staticdeps/lib $(LDFLAGS) -lmp4v2 -lgsm

# Harnais de test hors-ligne du négociateur de codecs (CodecNegotiator, cf.
# nego_fmtp.md). Vérifie le contrat de la struct fmtpByPt d'EndpointStartReceiving
# (§6.7 xmlrpc_jsr309_api.md). Ne dépend ni d'Asterisk ni du mediaserver.
# Usage : make negotest && ./negotest
negotest: tools/negotest.cpp libmedkit.a
	$(CXX) $(CXXFLAGS) tools/negotest.cpp libmedkit.a -o $@ \
		-L../../../staticdeps/lib $(LDFLAGS) -lmp4v2 -lgsm

############################################
# Tests automatises (GoogleTest, cf. libmedikit_tests_plan.md)
############################################
# Suite gtest liee contre libmedkit.a (batie ASTERISK=no) + gtest systeme.
# Usage : make check   (build + execution)   ;   make tests   (build seul)
# La fixture MP4 (tests/fixtures/record.mp4) est versionnee ; son chemin absolu
# est injecte via -DTEST_MP4_FILE (surchargeable : make check TEST_MP4=/chemin).
GTEST_CFLAGS := $(shell pkg-config --cflags gtest 2>/dev/null)
GTEST_LIBS   := $(shell pkg-config --libs gtest 2>/dev/null) -lgtest_main
TEST_SRCS    := $(wildcard tests/*.cpp)
TEST_MP4      ?= $(abspath tests/fixtures/record.mp4)
TEST_MP4_TITI ?= $(abspath tests/fixtures/titi.mp4)

# libmedkit.a n'est PAS un prerequis direct : on force sa (re)construction en
# ASTERISK=no via un sous-make (cible libstamp), sinon le defaut ASTERISK=yes
# tenterait de batir transcoder.o/mp4format.o (en-tetes <asterisk/...> absents)
# hors perimetre.
#
# Le temoin est indispensable : sans lui, runtests ne dependait que des sources
# de test et n'etait donc PAS relie quand seule la bibliotheque changeait -- les
# tests passaient sur un binaire perime, ce qui masque silencieusement une
# regression comme une correction.
tests/runtests: $(TEST_SRCS) tests/.libmedkit.stamp
	$(CXX) $(CXXFLAGS) $(GTEST_CFLAGS) \
		-DTEST_MP4_FILE='"$(TEST_MP4)"' -DTEST_MP4_TITI_FILE='"$(TEST_MP4_TITI)"' \
		$(TEST_SRCS) libmedkit.a -o $@ \
		-L../../../staticdeps/lib $(LDFLAGS) -lmp4v2 -lgsm $(GTEST_LIBS)

# Reconstruit la bibliotheque a chaque appel, mais ne touche le temoin que si
# l'archive a reellement change : le binaire de test n'est relie qu'en cas de
# besoin.
tests/.libmedkit.stamp: force-libmedkit
	@[ -f $@ ] && [ ! libmedkit.a -nt $@ ] || touch $@

force-libmedkit:
	@$(MAKE) ASTERISK=no libmedkit.a

tests: tests/runtests

check: tests/runtests
	./tests/runtests

.PHONY: tests check force-libmedkit
