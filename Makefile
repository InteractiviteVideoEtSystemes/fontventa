include ./Makeinclude

all: app_h324m.so app_mp4.so app_transcoder.so app_rtsp.so astlog
	echo "FOntventa compile"

libh324m.so:
	cd libh324m; make

app_h324m.so: libh324m.so
	cd app_h324m; make

app_mp4.so:
	cd app_mp4; make

app_transcoder.so:
	cd app_transcoder; make

app_rtsp.so: 
	cd app_rtsp ; make

astlog:
	cd astlog ; make

install: all
	cd libh324m; make install
	cd app_h324m; make install
	cd app_mp4; make install
	cd app_transcoder; make install
	cd app_rtsp ; make install
	cd tools ; make install
	cd astlog ; make install

clean:
	cd libh324m; make clean
	cd app_h324m; make clean
	cd app_mp4; make clean
	cd app_transcoder; make clean
	cd app_rtsp; make clean
	cd astlog ; make clean
