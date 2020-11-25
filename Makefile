boostasiohpp=$(firstword $(wildcard $(addsuffix /boost/asio.hpp,/usr/local /opt/local $(wildcard /usr/local/opt/boost*) /usr $(wildcard c:/local/boost*))))
dirname=$(dir $(patsubst %/,%,$1))
BOOSTDIR=$(call dirname,$(call dirname,$(boostasiohpp)))


ifneq ($(wildcard /System/Library/Extensions),)
OSTYPE=Darwin
endif

LDFLAGS+=$(if $(filter $(OSTYPE),Darwin),-lboost_regex-mt,-lboost_regex) 
LDFLAGS+=$(if $(filter $(OSTYPE),Darwin),-framework Security)

findstr: findstr.o $(if $(filter $(OSTYPE),Darwin),machmemory.o)

%: %.o
	$(CXX) -g -o $@ $^ -O3 -L$(BOOSTDIR)/lib $(LDFLAGS)
hexdumper_src=../../hexdumper
INCS=-I ../../cpputils -I $(hexdumper_src) -I $(BOOSTDIR)/include
CFLAGS+=-Wall -std=c++17 -g $(if $(D),-O0,-O3)
CFLAGS+=-DUSE_BOOST_REGEX
#CFLAGS+=-DUSE_STD_REGEX
#CFLAGS+=-DWITH_MEMSEARCH

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $^ $(INCS)
%.o: $(hexdumper_src)/%.cpp
	$(CXX) $(CFLAGS) -c -o $@ $^ $(INCS)

clean:
	$(RM) findstr $(wildcard *.o)

installbin:
	cp findstr ~/bin/
