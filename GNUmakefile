PREFIX    ?= /usr/local
INCLUDEDIR = $(PREFIX)/include

CXX      ?= g++
CC       ?= gcc
CXXFLAGS  = -I. -Ibenchmarks -std=c++20 -O3 -DNDEBUG -Wall -Wpedantic
CFLAGS    = -I. -Ibenchmarks -std=c11 -O3 -DNDEBUG -Wall

BENCH   = benchmarks/bench
SRC     = benchmarks/bench.cpp
BENCH_C = benchmarks/bench_c.c
BENCH_O = benchmarks/bench_c.o

TEST_CXXFLAGS = -I. -std=c++20 -O3 -Wall -Wpedantic
TEST_CFLAGS   = -I. -std=c11 -O3 -Wall
TEST_BINS     = tests/test tests/test_c

.PHONY: bench test install uninstall clean

bench: $(BENCH)
	benchmarks/run_comparison.sh

$(BENCH_O): $(BENCH_C) benchmarks/bench_c.h iht.h ixht.h vmum.h
	$(CC) $(CFLAGS) -c $(BENCH_C) -o $(BENCH_O)

$(BENCH): $(SRC) $(BENCH_O) ihtab.hpp ixhtab.hpp vmum.h benchmarks/bench_c.h
	$(CXX) $(CXXFLAGS) $(SRC) $(BENCH_O) -o $(BENCH)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "=== $$t ==="; ./$$t || exit 1; done

tests/test: tests/test.cpp ihtab.hpp ixhtab.hpp
	$(CXX) $(TEST_CXXFLAGS) $< -o $@

tests/test_c: tests/test_c.c iht.h ixht.h
	$(CC) $(TEST_CFLAGS) $< -o $@

install:
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 644 ihtab.hpp  $(DESTDIR)$(INCLUDEDIR)/ihtab.hpp
	install -m 644 ixhtab.hpp $(DESTDIR)$(INCLUDEDIR)/ixhtab.hpp
	install -m 644 iht.h      $(DESTDIR)$(INCLUDEDIR)/iht.h
	install -m 644 ixht.h     $(DESTDIR)$(INCLUDEDIR)/ixht.h

uninstall:
	$(RM) $(DESTDIR)$(INCLUDEDIR)/ihtab.hpp $(DESTDIR)$(INCLUDEDIR)/ixhtab.hpp
	$(RM) $(DESTDIR)$(INCLUDEDIR)/iht.h     $(DESTDIR)$(INCLUDEDIR)/ixht.h

clean:
	$(RM) $(BENCH) $(BENCH_O) $(TEST_BINS)
