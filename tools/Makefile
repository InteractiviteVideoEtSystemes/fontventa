
INSTALL_MODULES_DIR := $(DESTDIR)/usr/bin

SHELLSCRIPTS = mp4tool IVES_convert.ksh

INSTALL = install

LDFLAGS= -lmp4 -lmp4v2
CFLAGS =  

%.o: %.cpp
	g++ $(CFLAGS) -c -o $@ $<

all: pcm2mp4 mp4band

pcm2mp4: pcm2mp4.o
	g++ -o pcm2mp4 pcm2mp4.o $(LDFLAGS) 

mp4band: mp4band.o
	g++ -o mp4band mp4band.o $(LDFLAGS)

clean:
	rm -f pcm2mp4 *.o

install: all
	mkdir -p $(INSTALL_MODULES_DIR)
	for x in $(SHELLSCRIPTS); do $(INSTALL) -m 755 $$x $(INSTALL_MODULES_DIR) ; done
	cp pcm2mp4 $(INSTALL_MODULES_DIR) 
	cp mp4band $(INSTALL_MODULES_DIR)
