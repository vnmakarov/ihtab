PREFIX    ?= /usr/local
INCLUDEDIR = $(PREFIX)/include

CXX      ?= g++
CXXFLAGS  = -I. -Ibenchmarks -std=c++20 -O3 -DNDEBUG -Wall -Wpedantic

BENCH = benchmarks/bench
SRC   = benchmarks/bench.cpp

.PHONY: test install uninstall clean

test: $(BENCH)
	benchmarks/run_comparison.sh

$(BENCH): $(SRC) ihtab.h ixhtab.h vmum.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BENCH)

install:
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 644 ihtab.h  $(DESTDIR)$(INCLUDEDIR)/ihtab.h
	install -m 644 ixhtab.h $(DESTDIR)$(INCLUDEDIR)/ixhtab.h

uninstall:
	$(RM) $(DESTDIR)$(INCLUDEDIR)/ihtab.h $(DESTDIR)$(INCLUDEDIR)/ixhtab.h

clean:
	$(RM) $(BENCH)
