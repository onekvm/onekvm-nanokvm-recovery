CC ?= cc
CFLAGS ?= -O2
WEB_CFLAGS ?= $(CFLAGS)
WEB_LIBS ?=

.PHONY: all clean test

all: onekvm-recovery-read-key onekvm-recovery-web onekvm-recovery-oled

DISPLAY_SRC = src/recovery-display.c

onekvm-recovery-read-key: src/onekvm-recovery-read-key.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

onekvm-recovery-web: src/onekvm-recovery-web.c $(DISPLAY_SRC)
	$(CC) $(CPPFLAGS) $(WEB_CFLAGS) $(LDFLAGS) $^ -o $@ $(WEB_LIBS)

onekvm-recovery-oled: src/onekvm-recovery-oled.c $(DISPLAY_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -o $@

test: onekvm-recovery-web onekvm-recovery-oled
	sh tests/reset-user-test.sh
	./onekvm-recovery-oled --self-test

clean:
	$(RM) onekvm-recovery-read-key onekvm-recovery-web onekvm-recovery-oled
