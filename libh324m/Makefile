CXXFLAGS=-g -O3 -fPIC -DPIC -D_REENTRANT -fno-exceptions 
LDFLAGS=-lpthread -lSDL -lresolv -Wl,-Bstatic -lpt_linux_x86_r_s  -Wl,-Bdynamic -fPIC

all: libh324m test

libh324m: h324m.o
	make -C src/ all
	g++ -o libh324m.so -shared h324m.o src/libh324m.a $(LDFLAGS)

test:	test.o	
	gcc -o test test.o -lh324m -L./ -lh324m

clean:
	make -C src/ clean
	rm -f h324m.o test.o libh324m.so

install:
	mkdir -p $(DESTDIR)/usr/include ; cp include/h324m.h $(DESTDIR)/usr/include/
	@if [ "`uname -m`" == "x86_64" ] ; then make install64 ; else make install32 ; fi

install32:
	mkdir -p $(DESTDIR)/usr/lib
	cp libh324m.so $(DESTDIR)/usr/lib
	echo "32 bits install"

install64:
	mkdir -p $(DESTDIR)/usr/lib64
	cp libh324m.so $(DESTDIR)/usr/lib64
	echo "64 bits install"

uninstall:
	rm -f $(DESTDIR)/usr/include/h324m.h
	rm -f $(DESTDIR)/usr/lib*/libh324m.so 	
