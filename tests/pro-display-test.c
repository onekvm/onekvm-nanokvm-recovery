// SPDX-License-Identifier: MIT
#include "../src/recovery-display.c"

#include <assert.h>

static uint16_t native_pixel(const uint8_t *pixels, size_t offset)
{
    return (uint16_t)(pixels[offset] | (pixels[offset + 1] << 8));
}

static void preview(const char *prefix, const uint8_t *pixels, size_t stride, int count)
{
    char path[512];
    if (!prefix || !prefix[0]) return;
    assert(snprintf(path, sizeof(path), "%s-%d.ppm", prefix, count) < (int)sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n320 172\n255\n");
    for (int y = 0; y < 172; y++) for (int x = 0; x < 320; x++) {
        uint16_t color = native_pixel(pixels, (319 - x) * stride + y * 2);
        unsigned r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
        uint8_t rgb[] = { (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2) };
        assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    struct recovery_addrs addrs = {
        .ip = { "192.168.233.10", "10.100.99.191", "255.255.255.255", "172.16.0.1" },
    };
    size_t decoded = 0;
    for (size_t i = 0; i < sizeof(recovery_pro_background) / sizeof(uint16_t); i += 2)
        decoded += recovery_pro_background[i];
    assert(decoded == 320 * 172);

    for (size_t stride = 344; stride <= 352; stride += 8) {
        size_t size = stride * 320;
        uint8_t *guarded = malloc(size + 32);
        assert(guarded);
        uint8_t *pixels = guarded + 16;
        for (int count = 0; count <= RECOVERY_MAX_ADDRS; count++) {
            memset(guarded, 0xa5, size + 32);
            addrs.count = count;
            recovery_render_pro(pixels, stride, count ? &addrs : NULL);
            // Background and blue status bar must reach the correct native
            // panel positions, with RGB565 byte order independent of stride.
            assert(native_pixel(pixels, 319 * stride) == 0x0842);
            assert(native_pixel(pixels, (319 - 32) * stride + 136 * 2) == 0x13bf);
            for (size_t i = 0; i < 16; i++)
                assert(guarded[i] == 0xa5 && guarded[16 + size + i] == 0xa5);
            for (size_t row = 0; row < 320; row++)
                for (size_t col = 344; col < stride; col++)
                    assert(pixels[row * stride + col] == 0xa5);
            if (stride == 344) preview(argc > 1 ? argv[1] : NULL, pixels, stride, count);
        }
        free(guarded);
    }
    puts("Pro recovery RGB565, rotation, padded stride and address bounds passed");
    return 0;
}
