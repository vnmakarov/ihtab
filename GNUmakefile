PREFIX    ?= /usr/local
INCLUDEDIR = $(PREFIX)/include

CXX      ?= g++
CXXFLAGS  = -I. -Ibenchmarks -std=c++20 -O3 -DNDEBUG -Wall -Wpedantic

BENCH = benchmarks/bench
SRC   = benchmarks/bench.cpp

.PHONY: test install uninstall clean

test: $(BENCH)
	benchmarks/run_comparison.sh

$(BENCH): $(SRC) ihtab.hpp ixhtab.hpp vmum.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BENCH)

install:
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 644 ihtab.hpp  $(DESTDIR)$(INCLUDEDIR)/ihtab.hpp
	install -m 644 ixhtab.hpp $(DESTDIR)$(INCLUDEDIR)/ixhtab.hpp

uninstall:
	$(RM) $(DESTDIR)$(INCLUDEDIR)/ihtab.hpp $(DESTDIR)$(INCLUDEDIR)/ixhtab.hpp

clean:
	$(RM) $(BENCH)
