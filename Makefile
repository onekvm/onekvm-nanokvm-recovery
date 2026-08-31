CC ?= cc
CFLAGS ?= -O2
WEB_CFLAGS ?= $(CFLAGS)
WEB_LIBS ?=

.PHONY: all clean test

all: onekvm-recovery-read-key onekvm-recovery-boot-held onekvm-recovery-web onekvm-recovery-oled onekvm-recovery-sha256

DISPLAY_SRC = src/recovery-display.c
SHA_SRC = src/recovery-sha256.c

onekvm-recovery-read-key: src/onekvm-recovery-read-key.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

onekvm-recovery-boot-held: src/onekvm-recovery-boot-held.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

onekvm-recovery-web: src/onekvm-recovery-web.c $(DISPLAY_SRC) $(SHA_SRC)
	$(CC) $(CPPFLAGS) $(WEB_CFLAGS) $(LDFLAGS) $^ -o $@ $(WEB_LIBS)

onekvm-recovery-oled: src/onekvm-recovery-oled.c $(DISPLAY_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -o $@

onekvm-recovery-sha256: $(SHA_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRECOVERY_SHA256_MAIN $(LDFLAGS) $^ -o $@

test: onekvm-recovery-web onekvm-recovery-oled onekvm-recovery-sha256 onekvm-recovery-boot-held
	sh tests/reset-user-test.sh
	./onekvm-recovery-oled --self-test
	./onekvm-recovery-sha256 --self-test
	./onekvm-recovery-boot-held; test $$? -ne 0

clean:
	$(RM) onekvm-recovery-read-key onekvm-recovery-boot-held onekvm-recovery-web onekvm-recovery-oled onekvm-recovery-sha256
