#include "recovery-sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const uint32_t k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotr(uint32_t value, uint32_t bits)
{
	return (value >> bits) | (value << (32 - bits));
}

static void process_block(struct recovery_sha256 *ctx, const uint8_t block[64])
{
	uint32_t w[64];
	uint32_t a, b, c, d, e, f, g, h;
	int i;

	for (i = 0; i < 16; i++) {
		w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
		       ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
	}
	for (i = 16; i < 64; i++) {
		uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);

		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];
	f = ctx->state[5];
	g = ctx->state[6];
	h = ctx->state[7];
	for (i = 0; i < 64; i++) {
		uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		uint32_t ch = (e & f) ^ ((~e) & g);
		uint32_t temp1 = h + S1 + ch + k[i] + w[i];
		uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t temp2 = S0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}
	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

void recovery_sha256_init(struct recovery_sha256 *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->state[0] = 0x6a09e667;
	ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
}

void recovery_sha256_update(struct recovery_sha256 *ctx, const void *data, size_t length)
{
	const uint8_t *bytes = data;

	ctx->bit_len += (uint64_t)length * 8;
	while (length) {
		size_t room = 64 - ctx->buffer_len;
		size_t chunk = length < room ? length : room;

		memcpy(ctx->buffer + ctx->buffer_len, bytes, chunk);
		ctx->buffer_len += chunk;
		bytes += chunk;
		length -= chunk;
		if (ctx->buffer_len == 64) {
			process_block(ctx, ctx->buffer);
			ctx->buffer_len = 0;
		}
	}
}

void recovery_sha256_final(struct recovery_sha256 *ctx, uint8_t digest[32])
{
	uint8_t pad[72];
	size_t pad_len;
	uint64_t bits = ctx->bit_len;
	int i;

	pad[0] = 0x80;
	pad_len = (ctx->buffer_len < 56) ? (56 - ctx->buffer_len) : (120 - ctx->buffer_len);
	memset(pad + 1, 0, pad_len - 1);
	for (i = 0; i < 8; i++)
		pad[pad_len + i] = (uint8_t)(bits >> (56 - 8 * i));
	recovery_sha256_update(ctx, pad, pad_len + 8);
	for (i = 0; i < 8; i++) {
		digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
		digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
		digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
		digest[i * 4 + 3] = (uint8_t)ctx->state[i];
	}
}

void recovery_sha256_hex(const uint8_t digest[32], char out[65])
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 32; i++) {
		out[i * 2] = hex[digest[i] >> 4];
		out[i * 2 + 1] = hex[digest[i] & 0xf];
	}
	out[64] = 0;
}

int recovery_sha256_file_range(const char *path, uint64_t skip, uint64_t byte_limit, char out[65])
{
	struct recovery_sha256 ctx;
	uint8_t digest[32];
	uint8_t buffer[16384];
	int fd;
	uint64_t remaining = byte_limit;

	if (!path || !out)
		return -1;
	fd = strcmp(path, "-") == 0 ? STDIN_FILENO : open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	if (skip) {
		if (fd == STDIN_FILENO || lseek(fd, (off_t)skip, SEEK_SET) != (off_t)skip) {
			if (fd != STDIN_FILENO)
				close(fd);
			return -1;
		}
	}
	recovery_sha256_init(&ctx);
	while (remaining) {
		size_t want = sizeof(buffer);
		ssize_t got;

		if (remaining < want)
			want = (size_t)remaining;
		got = read(fd, buffer, want);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			if (fd != STDIN_FILENO)
				close(fd);
			return -1;
		}
		if (got == 0)
			break;
		recovery_sha256_update(&ctx, buffer, (size_t)got);
		remaining -= (uint64_t)got;
	}
	if (fd != STDIN_FILENO)
		close(fd);
	recovery_sha256_final(&ctx, digest);
	recovery_sha256_hex(digest, out);
	return 0;
}

int recovery_sha256_file(const char *path, uint64_t byte_limit, char out[65])
{
	return recovery_sha256_file_range(path, 0, byte_limit, out);
}

int recovery_sha256_self_test(void)
{
	struct recovery_sha256 ctx;
	uint8_t digest[32];
	char hex[65];

	recovery_sha256_init(&ctx);
	recovery_sha256_final(&ctx, digest);
	recovery_sha256_hex(digest, hex);
	if (strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") != 0)
		return 1;
	recovery_sha256_init(&ctx);
	recovery_sha256_update(&ctx, "abc", 3);
	recovery_sha256_final(&ctx, digest);
	recovery_sha256_hex(digest, hex);
	if (strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0)
		return 1;
	return 0;
}

#ifdef RECOVERY_SHA256_MAIN
#include <stdlib.h>

int main(int argc, char **argv)
{
	char hex[65];
	const char *path;
	uint64_t skip = 0;
	uint64_t limit = ~(uint64_t)0;

	if (argc == 2 && !strcmp(argv[1], "--self-test"))
		return recovery_sha256_self_test();
	if (argc == 2) {
		path = argv[1];
	} else if (argc == 4 && !strcmp(argv[1], "--bytes")) {
		limit = (uint64_t)strtoull(argv[2], NULL, 10);
		path = argv[3];
	} else if (argc == 6 && !strcmp(argv[1], "--skip") && !strcmp(argv[3], "--bytes")) {
		skip = (uint64_t)strtoull(argv[2], NULL, 10);
		limit = (uint64_t)strtoull(argv[4], NULL, 10);
		path = argv[5];
	} else {
		fprintf(stderr, "usage: onekvm-recovery-sha256 [--skip N] [--bytes N] FILE\n");
		return 2;
	}
	if (recovery_sha256_file_range(path, skip, limit, hex) < 0)
		return 1;
	printf("%s\n", hex);
	return 0;
}
#endif
