// SPDX-License-Identifier: MIT
#include "../src/recovery-display.c"

#include <assert.h>

static uint16_t native_pixel(const uint8_t *pixels, size_t offset)
{
    return (uint16_t)(pixels[offset] | (pixels[offset + 1] << 8));
}

static uint16_t landscape_pixel(const uint8_t *pixels, size_t stride, int x, int y)
{
    return native_pixel(pixels, (size_t)(319 - x) * stride + (size_t)y * 2);
}

static void preview(const char *prefix, const uint8_t *pixels, size_t stride, const char *name)
{
    char path[512];
    if (!prefix || !prefix[0]) return;
    assert(snprintf(path, sizeof(path), "%s-%s.ppm", prefix, name) < (int)sizeof(path));
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
        .ip = { "192.168.233.10", "198.51.100.25", "255.255.255.255", "172.16.0.1" },
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
            recovery_render_pro(pixels, stride, count ? &addrs : NULL, RECOVERY_BUTTON_NONE, 0);
            // Background and blue status bar must reach the correct native
            // panel positions, with RGB565 byte order independent of stride.
            assert(native_pixel(pixels, 319 * stride) == 0x0842);
            assert(native_pixel(pixels, (319 - 32) * stride + 136 * 2) == 0x13bf);
            for (size_t i = 0; i < 16; i++)
                assert(guarded[i] == 0xa5 && guarded[16 + size + i] == 0xa5);
            for (size_t row = 0; row < 320; row++)
                for (size_t col = 344; col < stride; col++)
                    assert(pixels[row * stride + col] == 0xa5);
            if (stride == 344) {
                char name[16];
                snprintf(name, sizeof(name), "%d", count);
                preview(argc > 1 ? argv[1] : NULL, pixels, stride, name);
            }
        }
        free(guarded);
    }

    /* Panel states: the mode label stays blue, the button takes its state's
     * color, and the static bar is blue across the button area. */
    {
        static const struct { enum recovery_button button; int focused; const char *name; uint16_t fill; } states[] = {
            { RECOVERY_BUTTON_NONE, 0, "static", 0x13bf },
            { RECOVERY_BUTTON_IDLE, 0, "idle", 0x2125 },
            { RECOVERY_BUTTON_IDLE, 1, "idle-focus", 0x2125 },
            { RECOVERY_BUTTON_CONFIRM, 0, "confirm", 0xd924 },
            { RECOVERY_BUTTON_CONFIRM, 1, "confirm-focus", 0xd924 },
            { RECOVERY_BUTTON_BUSY, 0, "busy", 0x18c3 },
            { RECOVERY_BUTTON_REBOOTING, 0, "rebooting", 0x2125 },
        };
        size_t stride = 344;
        uint8_t *pixels = calloc(1, stride * 320);
        assert(pixels);
        addrs.count = 2;
        for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
            recovery_render_pro(pixels, stride, &addrs, states[i].button, states[i].focused);
            assert(landscape_pixel(pixels, stride, 30, 136) == 0x13bf);
            assert(landscape_pixel(pixels, stride, 300, 135) == states[i].fill);
            /* Focus ring: white just inside the button's top edge. */
            if (states[i].button != RECOVERY_BUTTON_NONE)
                assert((landscape_pixel(pixels, stride, 251, 113) == 0xf7be) == states[i].focused);
            assert(landscape_pixel(pixels, stride, 0, 171) == 0x0842);
            preview(argc > 1 ? argv[1] : NULL, pixels, stride, states[i].name);
        }
        free(pixels);
    }

    /* Touch: the CST816 reports portrait 0..171 x 0..319; landscape x is
     * 319 - y and landscape y is x, as in the framebuffer. */
    assert(recovery_panel_hit(136, 319 - 250, 171, 319));      /* button centre */
    assert(recovery_panel_hit(171, 0, 171, 319));              /* bottom-right corner */
    assert(!recovery_panel_hit(136, 319 - 100, 171, 319));     /* mode label */
    assert(!recovery_panel_hit(60, 319 - 250, 171, 319));      /* addresses */
    assert(recovery_panel_hit(1365, 4095 - 3200, 1365, 4095)); /* other axis ranges */
    assert(!recovery_panel_hit(-1, -1, 171, 319));             /* no position yet */

    assert(recovery_progress_busy("{\"phase\":\"upload\",\"received\":1,\"total\":2}"));
    assert(recovery_progress_busy("{\"phase\": \"write-rootfs\"}"));
    assert(recovery_progress_busy("{\"phase\":\"verify\"}"));
    assert(!recovery_progress_busy("{\"phase\":\"done\"}"));
    assert(!recovery_progress_busy("{\"phase\":\"error\",\"message\":\"upload\"}"));
    assert(!recovery_progress_busy("{\"phase\":\"idle\"}"));
    assert(!recovery_progress_busy(""));
    assert(!recovery_progress_busy(NULL));
    puts("Pro recovery RGB565, rotation, padded stride, address bounds, button states, touch and busy passed");
    return 0;
}
