CC ?= cc
CFLAGS ?= -O2

.PHONY: all clean

all: onekvm-recovery-read-key

onekvm-recovery-read-key: src/onekvm-recovery-read-key.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	$(RM) onekvm-recovery-read-key
