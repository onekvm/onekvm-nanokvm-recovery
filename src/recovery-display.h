#ifndef RECOVERY_DISPLAY_H
#define RECOVERY_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#define RECOVERY_MAX_ADDRS 4
#define RECOVERY_MAX_FRAME 1024
#define RECOVERY_OLED_PATH "/dev/onekvm-oled"
#define RECOVERY_OLED_VARIANT "/sys/class/misc/onekvm-oled/variant"

struct recovery_addrs {
	char ip[RECOVERY_MAX_ADDRS][16];
	int count;
};

int recovery_collect_ipv4(struct recovery_addrs *addrs);
long recovery_firmware_limit(void);
int recovery_status_json(char *out, size_t size);
int recovery_format_banner(char *out, size_t size);
int recovery_oled_geometry(int *width, int *pages);
void recovery_render_status(uint8_t *frame, int width, int pages,
			    const char *top, const char *bottom);
int recovery_oled_canvas(void);
int recovery_oled_off(void);
int recovery_oled_show_addrs(const struct recovery_addrs *addrs);
int recovery_render_self_test(void);

#endif
