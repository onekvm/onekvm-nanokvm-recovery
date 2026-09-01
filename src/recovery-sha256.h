#ifndef RECOVERY_SHA256_H
#define RECOVERY_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct recovery_sha256 {
	uint32_t state[8];
	uint64_t bit_len;
	uint8_t buffer[64];
	size_t buffer_len;
};

void recovery_sha256_init(struct recovery_sha256 *ctx);
void recovery_sha256_update(struct recovery_sha256 *ctx, const void *data, size_t length);
void recovery_sha256_final(struct recovery_sha256 *ctx, uint8_t digest[32]);
void recovery_sha256_hex(const uint8_t digest[32], char out[65]);
int recovery_sha256_file(const char *path, uint64_t byte_limit, char out[65]);
int recovery_sha256_file_range(const char *path, uint64_t skip, uint64_t byte_limit, char out[65]);
int recovery_sha256_self_test(void);

#endif
