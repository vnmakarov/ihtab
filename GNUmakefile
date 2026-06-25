PREFIX    ?= /usr/local
INCLUDEDIR = $(PREFIX)/include

CXX      ?= g++
CC       ?= gcc
ARCH_FLAGS := $(if $(filter x86_64,$(shell uname -m)),-mavx2)
CXXFLAGS  = -I. -Ibenchmarks -std=c++20 -O3 $(ARCH_FLAGS) -DNDEBUG -Wall -Wpedantic
CFLAGS    = -I. -Ibenchmarks -std=c11 -O3 $(ARCH_FLAGS) -DNDEBUG -Wall

BENCH   = benchmarks/bench
SRC     = benchmarks/bench.cpp
BENCH_C = benchmarks/bench_c.c
BENCH_O = benchmarks/bench_c.o

TEST_CXXFLAGS = -I. -std=c++20 -O3 -Wall -Wpedantic
TEST_CFLAGS   = -I. -std=c11 -O3 -Wall
TEST_BINS     = tests/test tests/test_c tests/test_v0 tests/test_c_v0

.PHONY: bench test install uninstall clean

bench: $(BENCH)
	benchmarks/run_comparison.sh

$(BENCH_O): $(BENCH_C) benchmarks/bench_c.h ihtab.h ixhtab.h vmum.h
	$(CC) $(CFLAGS) -c $(BENCH_C) -o $(BENCH_O)

$(BENCH): $(SRC) $(BENCH_O) ihtab.hpp ixhtab.hpp vmum.h benchmarks/bench_c.h
	$(CXX) $(CXXFLAGS) $(SRC) $(BENCH_O) -o $(BENCH)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "=== $$t ==="; ./$$t || exit 1; done

tests/test: tests/test.cpp ihtab.hpp ixhtab.hpp
	$(CXX) $(TEST_CXXFLAGS) $< -o $@

tests/test_c: tests/test_c.c ihtab.h ixhtab.h
	$(CC) $(TEST_CFLAGS) $< -o $@

tests/test_v0: tests/test.cpp ihtab-v0.hpp ixhtab-v0.hpp
	$(CXX) $(TEST_CXXFLAGS) -DUSE_V0 $< -o $@

tests/test_c_v0: tests/test_c.c ihtab-v0.h ixhtab-v0.h
	$(CC) $(TEST_CFLAGS) -DUSE_V0 $< -o $@

install:
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 644 ihtab.hpp  $(DESTDIR)$(INCLUDEDIR)/ihtab.hpp
	install -m 644 ixhtab.hpp $(DESTDIR)$(INCLUDEDIR)/ixhtab.hpp
	install -m 644 ihtab.h      $(DESTDIR)$(INCLUDEDIR)/ihtab.h
	install -m 644 ixhtab.h     $(DESTDIR)$(INCLUDEDIR)/ixhtab.h

uninstall:
	$(RM) $(DESTDIR)$(INCLUDEDIR)/ihtab.hpp $(DESTDIR)$(INCLUDEDIR)/ixhtab.hpp
	$(RM) $(DESTDIR)$(INCLUDEDIR)/ihtab.h     $(DESTDIR)$(INCLUDEDIR)/ixhtab.h

clean:
	$(RM) $(BENCH) $(BENCH_O) $(TEST_BINS)
