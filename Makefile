BOOSTDIR=/usr/local
findstr: findstr.o  machmemory.o

%: %.o
	$(CXX) -g -o $@ $^ -O3 -L$(BOOSTDIR)/lib -lboost_regex-mt -framework Security

INCS=-I ../../cpputils -I ../../hexdumper -I $(BOOSTDIR)/include
CFLAGS+=-Wall -std=c++17 -g $(if $(D),-O0,-O3)
CFLAGS+=-DUSE_BOOST_REGEX
CFLAGS+=-DWITH_MEMSEARCH

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $^ $(INCS)

clean:
	$(RM) findstr $(wildcard *.o)

installbin:
	cp findstr ~/bin/
