CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -g
CFLAGS += -D_GNU_SOURCE -D_DEFAULT_SOURCE
INCLUDE = -Iinclude
LDFLAGS =
LDLIBS  = -lm

SRCDIR  = src
OBJDIR  = obj

# Определяем наличие libpcap
PCAP_CFLAGS  := $(shell pkg-config --cflags libpcap 2>/dev/null)
PCAP_LDFLAGS := $(shell pkg-config --libs libpcap 2>/dev/null)

ifeq ($(PCAP_CFLAGS),)
    # Пробуем найти pcap вручную
    HAS_PCAP := $(shell test -f /usr/include/pcap.h -o -f /usr/local/include/pcap.h && echo yes)
    ifeq ($(HAS_PCAP),yes)
        PCAP_CFLAGS  :=
        PCAP_LDFLAGS := -lpcap
    else
        $(info libpcap не найдена - захват отключён)
    endif
else
    HAS_PCAP := yes
endif

ifdef HAS_PCAP
    CFLAGS  += -DHAVE_PCAP $(PCAP_CFLAGS)
    LDFLAGS += $(PCAP_LDFLAGS)
    CAPTURE_SRC = $(SRCDIR)/capture.c
else
    CAPTURE_SRC = $(SRCDIR)/capture_stub.c
endif

COMMON_SRC = $(SRCDIR)/packet.c $(SRCDIR)/stream.c $(CAPTURE_SRC) $(SRCDIR)/main.c
COMMON_OBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(COMMON_SRC))

TEST_SRC = $(SRCDIR)/packet.c $(SRCDIR)/stream.c $(SRCDIR)/capture_stub.c tests/test_main.c
TEST_OBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(filter $(SRCDIR)/%.c,$(TEST_SRC)))
TEST_OBJ += $(patsubst tests/%.c,$(OBJDIR)/%.o,$(filter tests/%.c,$(TEST_SRC)))

TARGET = voip-analyzer
TESTER = test_voip

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(COMMON_OBJ) | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

$(OBJDIR)/%.o: tests/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

test: $(TESTER)
	./$(TESTER)

$(TESTER): $(TEST_OBJ) | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(OBJDIR) $(TARGET) $(TESTER)
