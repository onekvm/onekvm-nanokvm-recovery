#define _GNU_SOURCE
#include "recovery-display.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <time.h>

#include "../display/assets.h"

#define OLED_CMD_RECOVERY 4
#define OLED_CMD_OFF 5

static const uint8_t font5x5_az[26][5] = {
	{ 14, 17, 31, 17, 17 }, { 30, 17, 30, 17, 30 },
	{ 15, 16, 16, 16, 15 }, { 30, 17, 17, 17, 30 },
	{ 31, 16, 30, 16, 31 }, { 31, 16, 30, 16, 16 },
	{ 15, 16, 23, 17, 15 }, { 17, 17, 31, 17, 17 },
	{ 31, 4, 4, 4, 31 },    { 1, 1, 1, 17, 14 },
	{ 17, 18, 28, 18, 17 }, { 16, 16, 16, 16, 31 },
	{ 17, 27, 21, 17, 17 }, { 17, 25, 21, 19, 17 },
	{ 14, 17, 17, 17, 14 }, { 30, 17, 30, 16, 16 },
	{ 14, 17, 21, 19, 15 }, { 30, 17, 30, 18, 17 },
	{ 15, 16, 14, 1, 30 },  { 31, 4, 4, 4, 4 },
	{ 17, 17, 17, 17, 14 }, { 17, 17, 17, 10, 4 },
	{ 17, 17, 21, 27, 17 }, { 17, 10, 4, 10, 17 },
	{ 17, 10, 4, 4, 4 },    { 31, 2, 4, 8, 31 },
};

static const uint8_t font5x5_digit[10][5] = {
	{ 14, 17, 17, 17, 14 }, { 4, 12, 4, 4, 14 },
	{ 30, 1, 14, 16, 31 },  { 30, 1, 14, 1, 30 },
	{ 17, 17, 31, 1, 1 },   { 31, 16, 30, 1, 30 },
	{ 14, 16, 30, 17, 14 }, { 31, 1, 2, 4, 8 },
	{ 14, 17, 14, 17, 14 }, { 14, 17, 15, 1, 14 },
};

static const uint8_t font3x5_digit[10][5] = {
	{ 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 },
	{ 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
	{ 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 },
	{ 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
	{ 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
};

static char ascii_upper(char ch)
{
	if (ch >= 'a' && ch <= 'z')
		return (char)(ch - 'a' + 'A');
	return ch;
}

static int glyph_width(char ch, int compact)
{
	ch = ascii_upper(ch);
	if (ch == '.')
		return 1;
	if (ch >= '0' && ch <= '9')
		return compact ? 3 : 5;
	if (ch >= 'A' && ch <= 'Z')
		return 5;
	if (ch == ' ')
		return 3;
	return 0;
}

static int text_width(const char *text, int scale, int compact)
{
	int width = 0;

	if (scale < 1)
		scale = 1;
	while (*text) {
		int gw = glyph_width(*text, compact);

		if (gw)
			width += (gw + 1) * scale;
		text++;
	}
	if (width <= 0)
		return 0;
	return width - scale;
}

static void glyph_or(uint8_t *frame, int width, int pages, int x, int y,
		     char ch, int scale, int compact)
{
	const uint8_t *rows = NULL;
	int gw;
	int gx, gy, sx, sy;
	uint8_t dot_row[5] = { 0, 0, 0, 0, 1 };

	ch = ascii_upper(ch);
	gw = glyph_width(ch, compact);
	if (gw <= 0 || scale < 1)
		return;
	if (ch == '.')
		rows = dot_row;
	else if (ch >= '0' && ch <= '9')
		rows = compact ? font3x5_digit[ch - '0'] : font5x5_digit[ch - '0'];
	else if (ch >= 'A' && ch <= 'Z')
		rows = font5x5_az[ch - 'A'];
	else
		return;
	for (gy = 0; gy < 5; gy++) {
		uint8_t bits = rows[gy];

		for (gx = 0; gx < gw; gx++) {
			if ((bits & (1U << (gw - 1 - gx))) == 0)
				continue;
			for (sy = 0; sy < scale; sy++) {
				int py = y + gy * scale + sy;
				int page;

				if (py < 0 || (py >> 3) >= pages)
					continue;
				page = py >> 3;
				for (sx = 0; sx < scale; sx++) {
					int px = x + gx * scale + sx;

					if (px < 0 || px >= width)
						continue;
					frame[page * width + px] |=
						(uint8_t)(1U << (py & 7));
				}
			}
		}
	}
}

static void draw_centered(uint8_t *frame, int width, int pages, const char *text,
			  int y, int scale, int compact)
{
	int tw = text_width(text, scale, compact);
	int x = (width - tw) / 2;

	if (x < 0)
		x = 0;
	while (*text) {
		int gw = glyph_width(*text, compact);
		int advance;

		if (!gw) {
			text++;
			continue;
		}
		advance = (gw + 1) * scale;
		glyph_or(frame, width, pages, x, y, *text, scale, compact);
		x += advance;
		text++;
	}
}

void recovery_render_status(uint8_t *frame, int width, int pages,
			    const char *top, const char *bottom)
{
	int compact_bottom;
	int top_scale;
	int bot_scale;
	int top_y;
	int bot_y;

	if (!frame || width <= 0 || pages <= 0)
		return;
	if ((size_t)width * (size_t)pages > RECOVERY_MAX_FRAME)
		return;
	memset(frame, 0, (size_t)width * (size_t)pages);
	if (!top)
		top = "";
	if (!bottom)
		bottom = "";
	if (width >= 128) {
		top_scale = 2;
		bot_scale = text_width(bottom, 2, 0) <= width ? 2 : 1;
		compact_bottom = 0;
		top_y = 10;
		bot_y = 38;
	} else {
		top_scale = 1;
		bot_scale = 1;
		compact_bottom = text_width(bottom, 1, 0) > width;
		top_y = 3;
		bot_y = 18;
	}
	draw_centered(frame, width, pages, top, top_y, top_scale, 0);
	draw_centered(frame, width, pages, bottom, bot_y, bot_scale, compact_bottom);
}

int recovery_collect_ipv4(struct recovery_addrs *addrs)
{
	struct ifaddrs *list = NULL;
	struct ifaddrs *item;

	if (!addrs)
		return -1;
	memset(addrs, 0, sizeof(*addrs));
	if (getifaddrs(&list) < 0)
		return -1;
	for (item = list; item && addrs->count < RECOVERY_MAX_ADDRS; item = item->ifa_next) {
		struct sockaddr_in *addr;

		if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET)
			continue;
		if (!item->ifa_name || !strcmp(item->ifa_name, "lo"))
			continue;
		if (item->ifa_flags & IFF_LOOPBACK)
			continue;
		if (!(item->ifa_flags & IFF_UP))
			continue;
		addr = (struct sockaddr_in *)item->ifa_addr;
		if (!inet_ntop(AF_INET, &addr->sin_addr, addrs->ip[addrs->count],
			       sizeof(addrs->ip[0])))
			continue;
		if (!strncmp(addrs->ip[addrs->count], "127.", 4))
			continue;
		addrs->count++;
	}
	freeifaddrs(list);
	return 0;
}

#define RECOVERY_FIRMWARE_DIR "/run/onekvm-firmware"
#define RECOVERY_FIRMWARE_RESERVE (16LL * 1024 * 1024)

static long meminfo_available(void)
{
	FILE *file;
	char line[128];
	long kib = 0;

	file = fopen("/proc/meminfo", "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "MemAvailable: %ld kB", &kib) == 1)
			break;
		kib = 0;
	}
	fclose(file);
	if (kib <= 0)
		return 0;
	return kib * 1024L;
}

long recovery_firmware_limit(void)
{
	struct statvfs vfs;
	long long available = 0;

	if (statvfs(RECOVERY_FIRMWARE_DIR, &vfs) == 0 && vfs.f_frsize > 0 && vfs.f_blocks > 0)
		available = (long long)vfs.f_bavail * (long long)vfs.f_frsize;
	if (available <= 0)
		available = meminfo_available();
	if (available <= RECOVERY_FIRMWARE_RESERVE)
		return 0;
	available -= RECOVERY_FIRMWARE_RESERVE;
	return (long)available;
}

static int read_sysfs_line(const char *path, char *out, size_t size)
{
	FILE *file;
	size_t length;

	if (!path || !out || size < 2)
		return -1;
	file = fopen(path, "r");
	if (!file)
		return -1;
	if (!fgets(out, (int)size, file)) {
		fclose(file);
		return -1;
	}
	fclose(file);
	length = strlen(out);
	while (length && (out[length - 1] == '\n' || out[length - 1] == '\r' ||
			  out[length - 1] == ' ' || out[length - 1] == '\t'))
		out[--length] = 0;
	return length ? 0 : -1;
}

static int is_mmc_disk(const char *name)
{
	const char *cursor;

	if (!name || strncmp(name, "mmcblk", 6) != 0)
		return 0;
	cursor = name + 6;
	if (!*cursor)
		return 0;
	while (*cursor) {
		if (!isdigit((unsigned char)*cursor))
			return 0;
		cursor++;
	}
	return 1;
}

static long long block_bytes(const char *name)
{
	char path[128];
	char line[32];
	char *end = NULL;
	unsigned long long sectors;

	if (snprintf(path, sizeof(path), "/sys/class/block/%s/size", name) >= (int)sizeof(path))
		return 0;
	if (read_sysfs_line(path, line, sizeof(line)) < 0)
		return 0;
	sectors = strtoull(line, &end, 10);
	if (!end || *end || sectors == 0 || sectors > (~0ULL / 512ULL))
		return 0;
	return (long long)(sectors * 512ULL);
}

static void recovery_storage_info(char *type, size_t type_size, long long *bytes)
{
    char device[128], path[192], kind[16];
    char *name, *partition;
    ssize_t length;
    if (type && type_size) type[0] = 0;
    if (bytes) *bytes = 0;
    /* The shared storage resolver identifies the root medium, even with both
     * SD and eMMC present. Unknown types stay hidden. */
    length = readlink("/dev/onekvm-rootfs-a", device, sizeof(device) - 1);
    if (length <= 0) return;
    device[length] = 0;
    name = strrchr(device, '/');
    name = name ? name + 1 : device;
    partition = strrchr(name, 'p');
    if (!partition) return;
    *partition = 0;
    if (!is_mmc_disk(name)) return;
    snprintf(path, sizeof(path), "/sys/class/block/%s/device/type", name);
    if (read_sysfs_line(path, kind, sizeof(kind)) < 0) return;
    if (!strcmp(kind, "SD")) snprintf(type, type_size, "SD Card");
    else if (!strcmp(kind, "MMC")) snprintf(type, type_size, "eMMC");
    else return;
    if (bytes) *bytes = block_bytes(name);
}

struct network_display {
    char mode[16], source[16], address[32], fallback[32];
};
static struct network_display network_display_read(void)
{
    struct network_display value = { "dhcp", "fallback", "-", "-" };
    FILE *file = fopen("/run/onekvm-recovery-network.display", "r");
    if (file) {
        char mode[16], source[16], address[32], fallback[32];
        if (fscanf(file, "%15s%15s%31s%31s", mode, source, address, fallback) == 4) {
            if (!strcmp(mode, "static") || !strcmp(mode, "dhcp")) strcpy(value.mode, mode);
            if (!strcmp(source, "system") || !strcmp(source, "custom") || !strcmp(source, "session") || !strcmp(source, "fallback")) strcpy(value.source, source);
            strcpy(value.address, address); strcpy(value.fallback, fallback);
            value.address[strcspn(value.address, "/")] = 0;
            value.fallback[strcspn(value.fallback, "/")] = 0;
        }
        fclose(file);
    }
    return value;
}
static const char *network_address_kind(const struct network_display *network, const char *ip)
{
    if (!strcmp(network->fallback, ip)) return "FALLBACK";
    if (!strcmp(network->mode, "static") && !strcmp(network->address, ip)) return "STATIC";
    return !strcmp(network->mode, "dhcp") ? "DHCP" : "IP";
}
static const char *recovery_display_address(const struct recovery_addrs *addrs)
{
    return addrs && addrs->count ? addrs->ip[(unsigned long)time(NULL) / 8 % addrs->count] : "NO IP";
}

int recovery_status_json(char *out, size_t size)
{
	struct recovery_addrs addrs;
	char storage_type[16] = { 0 };
	size_t used = 0;
	int i;
	int n;
	long firmware_max;
	long long storage_bytes = 0;

	if (!out || size < 32)
		return -1;
	if (recovery_collect_ipv4(&addrs) < 0)
		addrs.count = 0;
	firmware_max = recovery_firmware_limit();
	recovery_storage_info(storage_type, sizeof(storage_type), &storage_bytes);
	used = (size_t)snprintf(out, size, "{\"addresses\":[");
	if (used >= size)
		return -1;
	for (i = 0; i < addrs.count; i++) {
		n = snprintf(out + used, size - used, "%s\"%s\"", i ? "," : "", addrs.ip[i]);
		if (n < 0 || (size_t)n >= size - used)
			return -1;
		used += (size_t)n;
	}
    struct network_display network = network_display_read();
	n = snprintf(out + used, size - used,
		     "],\"firmwareMax\":%ld,\"storageType\":\"%s\",\"storageBytes\":%lld,\"networkSource\":\"%s\",\"ipv4Mode\":\"%s\"}\n",
		     firmware_max, storage_type, storage_bytes, network.source, network.mode);
	if (n < 0 || (size_t)n >= size - used)
		return -1;
	return 0;
}

int recovery_format_banner(char *out, size_t size)
{
	struct recovery_addrs addrs;
	size_t used = 0;
	int i;

	if (!out || size < 32)
		return -1;
	if (recovery_collect_ipv4(&addrs) < 0)
		addrs.count = 0;
	struct network_display network = network_display_read();
	used = (size_t)snprintf(out, size, "OneKVM recovery web (%s, %s)\n", network.source, network.mode);
	if (used >= size)
		return -1;
	if (addrs.count == 0) {
		int n = snprintf(out + used, size - used, "  %s\n", !strcmp(network.mode, "static") ? "static IPv4 is unavailable; use the console" : "waiting for DHCP");

		return n < 0 || (size_t)n >= size - used ? -1 : 0;
	}
	for (i = 0; i < addrs.count; i++) {
		int n = snprintf(out + used, size - used,
				 "  %s http://%s/\n  https://%s/\n",
				 network_address_kind(&network, addrs.ip[i]), addrs.ip[i], addrs.ip[i]);

		if (n < 0 || (size_t)n >= size - used)
			return -1;
		used += (size_t)n;
	}
	return 0;
}

static int read_trimmed(const char *path, char *out, size_t size)
{
	FILE *file;
	size_t len;

	if (!path || !out || size < 2)
		return -1;
	file = fopen(path, "r");
	if (!file)
		return -1;
	if (!fgets(out, (int)size, file)) {
		fclose(file);
		return -1;
	}
	fclose(file);
	len = strlen(out);
	while (len && (out[len - 1] == '\n' || out[len - 1] == '\r'))
		out[--len] = 0;
	return 0;
}

int recovery_oled_geometry(int *width, int *pages)
{
	char variant[32];

	if (!width || !pages)
		return -1;
	if (read_trimmed(RECOVERY_OLED_VARIANT, variant, sizeof(variant)) < 0)
		return -1;
	if (!strcmp(variant, "none") || variant[0] == 0)
		return -1;
	if (!strcmp(variant, "pcie")) {
		*width = 64;
		*pages = 4;
		return 0;
	}
	*width = 128;
	*pages = 8;
	return 0;
}

static int oled_open(void)
{
	return open(RECOVERY_OLED_PATH, O_WRONLY | O_CLOEXEC);
}

/* Draw directly into the rotated framebuffer: no intermediate RGB canvas. */
static void recovery_pro_pixel(uint8_t *pixels, size_t stride, int x, int y,
                               uint16_t color)
{
    size_t offset = (size_t)(319 - x) * stride + (size_t)y * 2;
    pixels[offset] = (uint8_t)color;
    pixels[offset + 1] = (uint8_t)(color >> 8);
}

static void recovery_render_pro(uint8_t *pixels, size_t stride,
                                const struct recovery_addrs *addrs)
{
    size_t pixel = 0;
    int count = addrs ? addrs->count : 0;

    if (count < 0) count = 0;
    if (count > RECOVERY_MAX_ADDRS) count = RECOVERY_MAX_ADDRS;
    for (size_t i = 0; i + 1 < sizeof(recovery_pro_background) / sizeof(uint16_t); i += 2) {
        for (unsigned n = 0; n < recovery_pro_background[i] && pixel < 320 * 172; n++, pixel++)
            recovery_pro_pixel(pixels, stride, (int)(pixel % 320), (int)(pixel / 320),
                               recovery_pro_background[i + 1]);
    }
    if (!count) {
        for (int y = 0; y < 20; y++) for (int x = 0; x < 160; x++)
            recovery_pro_pixel(pixels, stride, 148 + x, 50 + y,
                               recovery_pro_waiting[y * 160 + x]);
        return;
    }
    for (int row = 0; row < count; row++) {
        int top = 28 + (80 - count * RECOVERY_IP_HEIGHT) / 2 + row * RECOVERY_IP_HEIGHT;
        for (int col = 0; col < 15 && addrs->ip[row][col]; col++) {
            char ch = addrs->ip[row][col];
            int index = ch == '.' ? 10 : ch - '0';
            if (index < 0 || index > 10) continue;
            for (int y = 0; y < RECOVERY_IP_HEIGHT; y++) for (int x = 0; x < RECOVERY_IP_WIDTH; x++) {
                unsigned alpha = recovery_ip_glyphs[index][y * RECOVERY_IP_WIDTH + x];
                unsigned r = (8 * (255 - alpha) + 244 * alpha) / 255;
                unsigned g = (11 * (255 - alpha) + 244 * alpha) / 255;
                unsigned b = (17 * (255 - alpha) + 245 * alpha) / 255;
                uint16_t color = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                recovery_pro_pixel(pixels, stride, 148 + col * RECOVERY_IP_WIDTH + x, top + y, color);
            }
        }
    }
}

/* Pro Desk is a 172x320 RGB565 framebuffer, mounted in landscape. */
static int recovery_framebuffer(const struct recovery_addrs *addrs)
{
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    uint8_t *pixels;
    size_t size;
    int fd, result = -1;
    size_t done = 0;
    fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
        var.xres != 172 || var.yres != 320 || var.bits_per_pixel != 16 ||
        var.xoffset || var.yoffset || var.red.offset != 11 || var.red.length != 5 ||
        var.green.offset != 5 || var.green.length != 6 ||
        var.blue.offset != 0 || var.blue.length != 5 ||
        fix.line_length < 344 || fix.line_length > 4096) { close(fd); return -1; }
    size = (size_t)fix.line_length * var.yres;
    pixels = calloc(1, size);
    if (!pixels) { close(fd); return -1; }
    recovery_render_pro(pixels, fix.line_length, addrs);
    while (done < size) {
        ssize_t wrote = pwrite(fd, pixels + done, size - done, (off_t)done);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) goto finished;
        done += (size_t)wrote;
    }
    (void)ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
    result = 0;
finished:
    free(pixels);
    close(fd);
    return result;
}

int recovery_oled_canvas(void)
{
	int fd = oled_open();

	if (fd < 0)
		return recovery_framebuffer(NULL);
	if (ioctl(fd, _IO('O', OLED_CMD_RECOVERY), 0) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int recovery_oled_off(void)
{
	int fd = oled_open();

	if (fd < 0) {
        char panel[32];
        /* Older initramfs publishers also send OLED burn-in blanking to the
         * Pro. Keep its color status visible when only recovery.zip updates. */
        return read_trimmed("/sys/class/graphics/fb0/name", panel, sizeof(panel)) == 0 &&
               !strcmp(panel, "fb_jd9853") ? 0 : -1;
    }
	if (ioctl(fd, _IO('O', OLED_CMD_OFF), 0) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int recovery_oled_show_addrs(const struct recovery_addrs *addrs)
{
	uint8_t frame[RECOVERY_MAX_FRAME];
	const char *bottom = "NO IP";
	int width = 0;
	int pages = 0;
	int fd;
	int bytes;
	ssize_t wrote;

	if (recovery_oled_geometry(&width, &pages) < 0)
		return recovery_framebuffer(addrs);
	bytes = width * pages;
	if (bytes <= 0 || bytes > (int)sizeof(frame))
		return -1;
    bottom = recovery_display_address(addrs);
	recovery_render_status(frame, width, pages, "RECOVERY", bottom);
	fd = oled_open();
	if (fd < 0)
		return -1;
	wrote = write(fd, frame, (size_t)bytes);
	close(fd);
	return wrote == bytes ? 0 : -1;
}

static int count_pixels(const uint8_t *frame, int bytes)
{
	int total = 0;
	int i;

	for (i = 0; i < bytes; i++)
		total += __builtin_popcount((unsigned int)frame[i]);
	return total;
}

int recovery_render_self_test(void)
{
	uint8_t small[64 * 4];
	uint8_t large[128 * 8];
	const char *ip = "192.168.100.123";

	if (text_width(ip, 1, 1) > 64)
		return 1;
	if (text_width(ip, 1, 0) > 128)
		return 1;
	recovery_render_status(small, 64, 4, "RECOVERY", ip);
	recovery_render_status(large, 128, 8, "RECOVERY", ip);
	if (count_pixels(small, (int)sizeof(small)) < 40)
		return 1;
	if (count_pixels(large, (int)sizeof(large)) < 80)
		return 1;
	{
		long limit = recovery_firmware_limit();

		if (limit < 0)
			return 1;
	}
	return 0;
}
