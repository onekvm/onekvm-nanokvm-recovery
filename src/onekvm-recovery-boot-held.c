#include <dirent.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BOOT_CHIP_LABEL "3020000.gpio"
#define BOOT_OFFSET 30U
#define SAMPLES 3
#define SAMPLE_US 80000
#define PROBE_ATTEMPTS 15
#define PROBE_US 100000

/* 1 = held, 0 = released, -1 = device not ready. */

static int key_pressed(int fd)
{
	unsigned char bits[(KEY_MAX + 7) / 8];
	unsigned int code = KEY_DISPLAYTOGGLE;

	memset(bits, 0, sizeof(bits));
	if (ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0)
		return 0;
	return (bits[code / 8] & (1U << (code % 8))) != 0;
}

static int evdev_boot_held(void)
{
	DIR *dir;
	struct dirent *entry;
	int found = 0;
	int held = 0;

	dir = opendir("/dev/input");
	if (!dir)
		return -1;
	while ((entry = readdir(dir))) {
		char path[64];
		char name[256];
		int fd;

		if (strncmp(entry->d_name, "event", 5) != 0)
			continue;
		if (snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name) >= (int)sizeof(path))
			continue;
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
		    (strstr(name, "User Key") || strstr(name, "gpio-keys"))) {
			found = 1;
			held = key_pressed(fd);
			close(fd);
			break;
		}
		close(fd);
	}
	closedir(dir);
	if (!found)
		return -1;
	return held;
}

static int gpiochip_boot_held(void)
{
	DIR *dir;
	struct dirent *entry;
	int found = 0;
	int held = -1;

	dir = opendir("/dev");
	if (!dir)
		return -1;
	while ((entry = readdir(dir))) {
		char path[64];
		struct gpiochip_info info;
		struct gpio_v2_line_request req;
		struct gpio_v2_line_values values;
		int fd;

		if (strncmp(entry->d_name, "gpiochip", 8) != 0)
			continue;
		if (snprintf(path, sizeof(path), "/dev/%s", entry->d_name) >= (int)sizeof(path))
			continue;
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(&info, 0, sizeof(info));
		if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) < 0 ||
		    strcmp(info.label, BOOT_CHIP_LABEL) != 0) {
			close(fd);
			continue;
		}
		found = 1;
		if (BOOT_OFFSET >= info.lines) {
			close(fd);
			break;
		}
		memset(&req, 0, sizeof(req));
		req.offsets[0] = BOOT_OFFSET;
		req.num_lines = 1;
		req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_ACTIVE_LOW;
		strncpy(req.consumer, "onekvm-boot", sizeof(req.consumer) - 1);
		if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
			close(fd);
			break;
		}
		memset(&values, 0, sizeof(values));
		values.mask = 1;
		if (ioctl(req.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) == 0)
			held = (values.bits & 1U) != 0;
		close(req.fd);
		close(fd);
		break;
	}
	closedir(dir);
	if (!found || held < 0)
		return -1;
	return held;
}

static int boot_held_once(void)
{
	int ev = evdev_boot_held();

	if (ev >= 0)
		return ev;
	return gpiochip_boot_held();
}

int main(void)
{
	int waits;
	int samples;

	for (waits = 0; waits < PROBE_ATTEMPTS; waits++) {
		int sample = boot_held_once();

		if (sample < 0) {
			usleep(PROBE_US);
			continue;
		}
		if (!sample)
			return 1;
		for (samples = 1; samples < SAMPLES; samples++) {
			usleep(SAMPLE_US);
			sample = boot_held_once();
			if (sample <= 0)
				return 1;
		}
		return 0;
	}
	return 1;
}
