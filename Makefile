dirname=$(dir $(patsubst %/,%,$1))

boostasiohpp=$(firstword $(wildcard $(addsuffix /include/boost/asio.hpp,/usr/local /opt/local $(wildcard /usr/local/opt/boost*) /usr $(wildcard c:/local/boost*))))
BOOSTDIR=$(call dirname,$(call dirname,$(call dirname,$(boostasiohpp))))

cpputilssplit=$(firstword $(wildcard $(addsuffix /string-split.h, cpputils submodules/cpputils ../cpputils ../../cpputils)))
CPPUTILSDIR=$(call dirname,$(cpputilssplit))

hdmachmemory=$(firstword $(wildcard $(addsuffix /machmemory.h, hexdumper submodules/hexdumper ../hexdumper ../../hexdumper)))
HEXDUMPERDIR=$(call dirname,$(hdmachmemory))


ifneq ($(wildcard /System/Library/Extensions),)
OSTYPE=Darwin
endif

LDFLAGS+=$(if $(filter $(OSTYPE),Darwin),-lboost_regex-mt,-lboost_regex) 
LDFLAGS+=$(if $(filter $(OSTYPE),Darwin),-framework Security)

findstr: findstr.o $(if $(filter $(OSTYPE),Darwin),machmemory.o)

%: %.o
	$(CXX) -g -o $@ $^ -O3 -L$(BOOSTDIR)/lib $(LDFLAGS)
INCS=-I $(CPPUTILSDIR) -I $(HEXDUMPERDIR) -I $(BOOSTDIR)/include

CFLAGS+=-Wall -std=c++17 -g $(if $(D),-O0,-O3)
CFLAGS+=-DUSE_BOOST_REGEX
#CFLAGS+=-DUSE_STD_REGEX
#CFLAGS+=-DWITH_MEMSEARCH

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $^ $(INCS)
%.o: $(HEXDUMPERDIR)/%.cpp
	$(CXX) $(CFLAGS) -c -o $@ $^ $(INCS)

clean:
	$(RM) findstr $(wildcard *.o)

installbin:
	cp findstr ~/bin/
