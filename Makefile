# GeoDispatch ADS — Makefile
#
# Targets:
#   make / build.bat   → geodispatch.exe + bench + test
#   make run-test      → build and run unit tests
#   make run-bench     → build and run benchmark
#   make clean         → remove build outputs

CC     = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -I. -Isrc

LIB_SRC = src/kd.c src/kd_dynamic.c src/voronoi.c src/algo.c

EXE   = geodispatch
BENCH = bench
TEST  = test_kd

.PHONY: all clean run-bench run-test

all: $(EXE) $(BENCH) $(TEST)

$(EXE): src/main.c $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(BENCH): src/bench.c src/kd.c src/kd_dynamic.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(TEST): src/test.c $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $^ -lm

run-bench: $(BENCH)
	./$(BENCH)

run-test: $(TEST)
	./$(TEST)

clean:
	rm -f $(EXE) $(EXE).exe $(BENCH) $(TEST)
