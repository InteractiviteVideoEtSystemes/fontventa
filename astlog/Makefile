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

LIBS = 
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
	mkdir -p $(DESTDIR)/$(INSTALL_MODULES_DIR)
	cp $(BIN) $(DESTDIR)/$(INSTALL_MODULES_DIR)


