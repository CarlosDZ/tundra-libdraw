CC = gcc
VERSION = $(shell grep '^version=' metapkg | cut -d= -f2)

SOMAJOR = 0
LIB = libtundra-draw.so
SONAME = $(LIB).$(SOMAJOR)

TUNDRA_STD  = -std=c11
TUNDRA_DEFS = -D_GNU_SOURCE -DTDRAW_VERSION=\"$(VERSION)\"
TUNDRA_INC  = -Iinclude -Isrc
TUNDRA_PIC  = -fPIC -fvisibility=hidden
TUNDRA_SO   = -shared -Wl,-soname,$(SONAME)

CFLAGS ?= -Wall -Wextra -O2

BUILD = build
SRCS = $(wildcard src/*.c)
HDRS = $(wildcard src/*.h include/tundra/*.h)
OBJS = $(SRCS:src/%.c=$(BUILD)/%.o)
DEPS = $(OBJS:.o=.d)

TARGET = $(BUILD)/$(SONAME)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TUNDRA_SO) -o $@ $(OBJS)
	ln -sf $(SONAME) $(BUILD)/$(LIB)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TUNDRA_STD) $(CPPFLAGS) $(CFLAGS) $(TUNDRA_PIC) $(TUNDRA_INC) $(TUNDRA_DEFS) -MMD -MP -c $< -o $@

-include $(DEPS)

$(OBJS): Makefile metapkg

clean:
	rm -rf $(BUILD)

TEST_BUILD = $(BUILD)/test
UNIT_FLAGS = -g -fsanitize=address,undefined -fno-omit-frame-pointer
UNIT_TESTS = $(patsubst tests/unit/%.c,$(TEST_BUILD)/unit/%,$(wildcard tests/unit/*.c))

$(TEST_BUILD)/unit/%: tests/unit/%.c tests/unit/test.h $(SRCS) $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(TUNDRA_STD) $(CFLAGS) $(UNIT_FLAGS) $(TUNDRA_INC) -Itests/unit \
		-D_GNU_SOURCE -DTDRAW_VERSION=\"$(VERSION)-test\" -o $@ $<

check-unit: $(UNIT_TESTS)
	@rc=0; for t in $(UNIT_TESTS); do echo "# $$t"; $$t || rc=1; done; exit $$rc

$(TEST_BUILD)/abi/t-link: tests/abi/t-link.c tests/unit/test.h $(TARGET)
	@mkdir -p $(dir $@)
	$(CC) $(TUNDRA_STD) $(CFLAGS) -Iinclude -Itests/unit -o $@ $< \
		-L$(BUILD) -Wl,-rpath,$(CURDIR)/$(BUILD) -ltundra-draw

check: check-unit $(TEST_BUILD)/abi/t-link
	@echo "# $(TEST_BUILD)/abi/t-link"; $(TEST_BUILD)/abi/t-link $(VERSION)
	@sh tests/abi/symbols.sh $(TARGET)

.PHONY: install uninstall clean check check-unit

PREFIX ?= /usr
DESTDIR ?=
NAME = tundra-libdraw

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/lib/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(PREFIX)/lib/$(LIB)
	install -Dm644 include/tundra/draw.h $(DESTDIR)$(PREFIX)/include/tundra/draw.h
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/$(NAME)/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(SONAME) $(DESTDIR)$(PREFIX)/lib/$(LIB)
	rm -f $(DESTDIR)$(PREFIX)/include/tundra/draw.h
	rm -rf $(DESTDIR)$(PREFIX)/share/licenses/$(NAME)
