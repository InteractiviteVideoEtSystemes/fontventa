include ./Makeinclude

all: app_mp4.so  app_rtsp.so astlog 
	echo "Fontventa compile"

libmedikit:
	cd libmedikit; make

app_mp4.so:
	cd app_mp4; make

app_transcoder.so:
	cd app_transcoder; make

app_rtsp.so: 
	cd app_rtsp ; make

astlog:
	cd astlog ; make

install: all
	cd app_mp4; make install
	cd app_rtsp ; make install
	cd tools ; make install
	cd astlog ; make install
	cd libmedikit; make install

clean:
	cd app_mp4; make clean
	cd app_rtsp; make clean
	cd astlog ; make clean
	cd libmedikit; make clean
