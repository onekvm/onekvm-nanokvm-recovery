#define _GNU_SOURCE
#include "recovery-display.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
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

static int is_nvme_disk(const char *name)
{
	const char *cursor;

	if (!name || strncmp(name, "nvme", 4) != 0 || strchr(name, 'p'))
		return 0;
	cursor = name + 4;
	if (!isdigit((unsigned char)*cursor))
		return 0;
	while (isdigit((unsigned char)*cursor))
		cursor++;
	if (*cursor != 'n')
		return 0;
	cursor++;
	if (!isdigit((unsigned char)*cursor))
		return 0;
	while (isdigit((unsigned char)*cursor))
		cursor++;
	return *cursor == 0;
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
	DIR *dir;
	struct dirent *entry;
	char mmc_fallback[32] = { 0 };
	long long mmc_bytes = 0;
	long long nvme_bytes = 0;

	if (type && type_size)
		type[0] = 0;
	if (bytes)
		*bytes = 0;
	dir = opendir("/sys/class/block");
	if (!dir)
		return;
	while ((entry = readdir(dir))) {
		char path[160];
		char kind[16];

		if (is_nvme_disk(entry->d_name) && nvme_bytes <= 0)
			nvme_bytes = block_bytes(entry->d_name);
		if (!is_mmc_disk(entry->d_name))
			continue;
		if (snprintf(path, sizeof(path), "/sys/class/block/%s/device/type",
			     entry->d_name) >= (int)sizeof(path))
			continue;
		if (read_sysfs_line(path, kind, sizeof(kind)) < 0)
			continue;
		if (!strcasecmp(kind, "SD")) {
			if (type && type_size)
				snprintf(type, type_size, "SD Card");
			if (bytes)
				*bytes = block_bytes(entry->d_name);
			closedir(dir);
			return;
		}
		if (!strcasecmp(kind, "MMC") && !mmc_fallback[0]) {
			snprintf(mmc_fallback, sizeof(mmc_fallback), "eMMC");
			mmc_bytes = block_bytes(entry->d_name);
		}
	}
	closedir(dir);
	if (mmc_fallback[0]) {
		if (type && type_size)
			snprintf(type, type_size, "%s", mmc_fallback);
		if (bytes)
			*bytes = mmc_bytes;
		return;
	}
	if (nvme_bytes > 0) {
		if (type && type_size)
			snprintf(type, type_size, "NVMe");
		if (bytes)
			*bytes = nvme_bytes;
	}
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
	n = snprintf(out + used, size - used,
		     "],\"firmwareMax\":%ld,\"storageType\":\"%s\",\"storageBytes\":%lld}\n",
		     firmware_max, storage_type, storage_bytes);
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
	used = (size_t)snprintf(out, size, "OneKVM recovery web\n");
	if (used >= size)
		return -1;
	if (addrs.count == 0) {
		int n = snprintf(out + used, size - used, "  waiting for DHCP\n");

		return n < 0 || (size_t)n >= size - used ? -1 : 0;
	}
	for (i = 0; i < addrs.count; i++) {
		int n = snprintf(out + used, size - used,
				 "  http://%s/\n  https://%s/\n",
				 addrs.ip[i], addrs.ip[i]);

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

int recovery_oled_canvas(void)
{
	int fd = oled_open();

	if (fd < 0)
		return -1;
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

	if (fd < 0)
		return -1;
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
		return -1;
	bytes = width * pages;
	if (bytes <= 0 || bytes > (int)sizeof(frame))
		return -1;
	if (addrs && addrs->count > 0)
		bottom = addrs->ip[0];
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
