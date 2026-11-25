# Build the NetPulse agent. The registry is plain Python and is tested separately.

CC := gcc
PKG_CONFIG ?= pkg-config

ifeq ($(shell $(PKG_CONFIG) --exists netsnmp && echo yes),)
$(error libsnmp headers not found. Install libsnmp-dev and pkg-config.)
endif

CFLAGS := -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -pthread -Iagent/include \
	$(shell $(PKG_CONFIG) --cflags netsnmp) -MMD -MP
LDFLAGS := -pthread
LDLIBS := $(shell $(PKG_CONFIG) --libs netsnmp)

BUILD := build
AGENT_SRCS := $(filter-out agent/src/main.c,$(wildcard agent/src/*.c))
AGENT_OBJS := $(patsubst agent/src/%.c,$(BUILD)/%.o,$(AGENT_SRCS))
TEST_SRCS := $(wildcard agent/tests/test_*.c)
TEST_OBJS := $(patsubst agent/tests/%.c,$(BUILD)/%.o,$(TEST_SRCS))

PY ?= python3

.PHONY: all test valgrind test-registry check clean

all: $(BUILD)/netpulse

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: agent/src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: agent/tests/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/netpulse: $(AGENT_OBJS) $(BUILD)/main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/netpulse-tests: $(AGENT_OBJS) $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD)/netpulse-tests
	./$(BUILD)/netpulse-tests

valgrind: $(BUILD)/netpulse-tests
	valgrind --leak-check=full --show-leak-kinds=definite,indirect \
		--errors-for-leak-kinds=definite,indirect --error-exitcode=1 \
		--quiet ./$(BUILD)/netpulse-tests

test-registry:
	$(PY) -m compileall -q registry
	$(PY) -m pytest -q registry/tests

check: test test-registry

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
