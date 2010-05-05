#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for astlog
#


.EXPORT_ALL_VARIABLES:

#
# app_mp4 defines which can be passed on the command-line
#

INSTALL_PREFIX := /usr
INSTALL_MODULES_DIR := $(INSTALL_PREFIX)/sbin

OBJS = astlog.o
BIN = astlog

#
# standard compile settings
#

PROC = $(shell uname -m)
INSTALL = install
CC = gcc 

LIBS = -lmp4 -lmp4v2 -lmp4av
DEBUG := -g 

#
# targets
#

all: $(BIN) 

clean:
	rm -f $(OBJS) $(BIN)

$(BIN) : $(OBJS:.o=.c)
	$(CC) $? -o $@

install: all
	cp $(BIN) $(INSTALL_MODULES_DIR)


