clientbuffer.so : clientbuffer.cpp
	znc-buildmod clientbuffer.cpp

install: clientbuffer.so
	install clientbuffer.so /usr/lib/znc/
