CC ?= cc
CFLAGS ?= -O2
WEB_CFLAGS ?= $(CFLAGS)
WEB_LIBS ?=

.PHONY: all clean test

all: onekvm-recovery-read-key onekvm-recovery-web

onekvm-recovery-read-key: src/onekvm-recovery-read-key.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

onekvm-recovery-web: src/onekvm-recovery-web.c
	$(CC) $(CPPFLAGS) $(WEB_CFLAGS) $(LDFLAGS) $< -o $@ $(WEB_LIBS)

test: onekvm-recovery-web
	sh tests/reset-user-test.sh

clean:
	$(RM) onekvm-recovery-read-key onekvm-recovery-web
