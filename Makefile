CC ?= cc
CFLAGS ?= -O2
WEB_CFLAGS ?= $(CFLAGS)
WEB_LIBS ?= -ljson-c

.PHONY: all clean test test-pro-display

all: onekvm-recovery-read-key onekvm-recovery-boot-held onekvm-recovery-web onekvm-recovery-oled onekvm-recovery-sha256 onekvm-recovery-network-config

DISPLAY_SRC = src/recovery-display.c
SHA_SRC = src/recovery-sha256.c
NETWORK_SRC = src/recovery-network.c

onekvm-recovery-web onekvm-recovery-oled command-wait-test: src/recovery-display.h display/assets.h

onekvm-recovery-read-key: src/onekvm-recovery-read-key.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

onekvm-recovery-boot-held: src/onekvm-recovery-boot-held.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

onekvm-recovery-web: src/onekvm-recovery-web.c $(DISPLAY_SRC) $(SHA_SRC) $(NETWORK_SRC)
	$(CC) $(CPPFLAGS) $(WEB_CFLAGS) $(LDFLAGS) $(filter %.c,$^) -o $@ $(WEB_LIBS)

onekvm-recovery-oled: src/onekvm-recovery-oled.c $(DISPLAY_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) -o $@

onekvm-recovery-sha256: $(SHA_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRECOVERY_SHA256_MAIN $(LDFLAGS) $^ -o $@

onekvm-recovery-network-config: $(NETWORK_SRC) $(SHA_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRECOVERY_NETWORK_MAIN $(LDFLAGS) $^ -o $@ -ljson-c

command-wait-test: tests/command-wait-test.c src/onekvm-recovery-web.c $(DISPLAY_SRC) $(SHA_SRC) $(NETWORK_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/command-wait-test.c $(DISPLAY_SRC) $(SHA_SRC) $(NETWORK_SRC) -o $@ -ljson-c

test-pro-display:
	@work=$$(mktemp -d); trap 'rm -rf "$$work"' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/pro-display-test.c -o "$$work/test" && \
	"$$work/test" "$(OUTPUT_PREFIX)"

test: onekvm-recovery-network-config onekvm-recovery-web onekvm-recovery-oled onekvm-recovery-sha256 onekvm-recovery-boot-held command-wait-test test-pro-display
	./command-wait-test
	@work=$$(mktemp -d); trap 'rm -rf "$$work"' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC $(LDFLAGS) $(NETWORK_SRC) $(SHA_SRC) -o "$$work/network.so" -ljson-c && \
	python3 tests/network-test.py ./onekvm-recovery-network-config "$$work/network.so"
	sh tests/reset-user-test.sh
	sh tests/install-detect-test.sh
	sh tests/bounded-copy-test.sh
	sh tests/nextboot-mode-test.sh
	sh tests/boot-retry-test.sh
	./onekvm-recovery-oled --self-test
	./onekvm-recovery-oled status | grep storageBytes
	./onekvm-recovery-sha256 --self-test
	printf 'xxabc' >.sha-range
	test "$$(./onekvm-recovery-sha256 --skip 2 --bytes 3 .sha-range)" = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
	$(RM) .sha-range
	./onekvm-recovery-boot-held; test $$? -ne 0

clean:
	$(RM) onekvm-recovery-network-config onekvm-recovery-read-key onekvm-recovery-boot-held onekvm-recovery-web onekvm-recovery-oled onekvm-recovery-sha256 command-wait-test
