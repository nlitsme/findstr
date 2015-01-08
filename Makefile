findstr: findstr.o  stringutils.o

%: %.o
	clang++ -g -o $@ $^ -lboost_regex-mt

INCS=-I ../../itslib/include/itslib -I /usr/local/include
CFLAGS+=-Wall -std=c++1y -g -O0  -DUSE_BOOST_REGEX

%.o: %.cpp
	clang++ $(CFLAGS) -c -o $@ $^ $(INCS)

%.o: ../../itslib/src/%.cpp
	clang++ $(CFLAGS) -c -o $@ $^ $(INCS)

clean:
	$(RM) findstr $(wildcard *.o)


