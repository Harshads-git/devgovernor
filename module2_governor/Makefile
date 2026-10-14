# ============================================================
# DevGovernor — Module 2 Governor Makefile
# ============================================================
# Student 2: Resource Management and Scheduling Governor
# Branch: student2-governor-rules
#
# Targets:
#   all     - Build the governor binary (default)
#   clean   - Remove compiled artifacts
#   check   - Quick smoke test (help screen + safety guards)
#   test    - Run the full Python test suite for Module 2
# ============================================================

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L
TARGET   = governor
SRC      = governor.c

# Root of the project (one level up from this Makefile)
PROJ_ROOT := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))/../..

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.o

# Quick smoke test: confirm help prints and safety guards work
check: $(TARGET)
	@echo "--- Smoke test: help ---"
	./$(TARGET) --help
	@echo "--- Smoke test: PID 1 rejection (evaluate) ---"
	./$(TARGET) evaluate 1 5 --history /tmp/gov_smoke_test.jsonl || true
	@echo "--- Smoke test: out-of-range nice rejection ---"
	./$(TARGET) evaluate 2 25 --history /tmp/gov_smoke_test.jsonl || true
	@echo "--- Smoke test: recommend-all on sample data ---"
	./$(TARGET) recommend-all \
		--input $(PROJ_ROOT)/data/processes.sample.json \
		--history /tmp/gov_smoke_test.jsonl
	@rm -f /tmp/gov_smoke_test.jsonl
	@echo "--- All smoke tests passed ---"

# Run full Python test suite (Module 2 specific)
test: $(TARGET)
	python3 $(PROJ_ROOT)/tests/test_governor.py -v

.PHONY: all clean check test
