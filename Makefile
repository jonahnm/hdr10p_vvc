CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?=

SRCS := src/bitbuf.c src/json.c src/st2094.c src/vvc.c src/vvc_poc.c src/rpu.c src/mkv.c src/main.c
HDRS := src/bitbuf.h src/json.h src/st2094.h src/vvc.h src/vvc_poc.h src/rpu.h src/mkv.h

BIN := hdr10p_vvc

all: $(BIN)

$(BIN): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: all clean
