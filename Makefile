CC ?= cc
BUILD ?= build
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS ?= -O2
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes
SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all clean debug test sanitize
all: $(BUILD)/netd
$(BUILD)/netd: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD):
	mkdir -p $@
$(BUILD)/test-core: tests/test_core.c src/buffer.c src/protocol.c src/command.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/test_core.c src/buffer.c src/protocol.c src/command.c $(LDLIBS) -o $@
$(BUILD)/test-client: tests/test_client.c src/client.c src/buffer.c src/protocol.c src/socket.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/test_client.c src/client.c src/buffer.c src/protocol.c src/socket.c $(LDLIBS) -o $@
test: all $(BUILD)/test-core $(BUILD)/test-client
	$(BUILD)/test-core
	$(BUILD)/test-client
	python3 tests/integration.py $(BUILD)/netd
debug:
	$(MAKE) BUILD=$(BUILD)/debug CFLAGS='-O0 -g3 -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes' all
sanitize:
	$(MAKE) BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g3 -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test
clean:
	rm -rf build
-include $(OBJECTS:.o=.d)
