BOOSTDIR=/usr/local
findstr: findstr.o 

%: %.o
	$(CXX) -g -o $@ $^ -O3 -L$(BOOSTDIR)/lib -lboost_regex-mt

INCS=-I ../../cpputils -I ../../hexdumper -I $(BOOSTDIR)/include
CFLAGS+=-Wall -std=c++17 -g $(if $(D),-O0,-O3)  -DUSE_BOOST_REGEX

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $^ $(INCS)

clean:
	$(RM) findstr $(wildcard *.o)

installbin:
	cp findstr ~/bin/
