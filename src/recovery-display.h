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
/* Firmware files install only when recovery.zip carries the offline RAUC tools. */
int recovery_firmware_supported(void);
int recovery_status_json(char *out, size_t size);
int recovery_format_banner(char *out, size_t size);
int recovery_oled_geometry(int *width, int *pages);
void recovery_render_status(uint8_t *frame, int width, int pages,
			    const char *top, const char *bottom);
int recovery_oled_canvas(void);
int recovery_oled_off(void);
int recovery_oled_show_addrs(const struct recovery_addrs *addrs);
int recovery_render_self_test(void);

/* Pro Desk panel: the bottom bar with a Reboot button, used by the panel
 * command. NONE draws the static full-width mode bar. A focused button,
 * selected by turning the knob, has a white ring. */
enum recovery_button {
	RECOVERY_BUTTON_NONE,
	RECOVERY_BUTTON_IDLE,
	RECOVERY_BUTTON_CONFIRM,
	RECOVERY_BUTTON_BUSY,
	RECOVERY_BUTTON_REBOOTING,
};

#define RECOVERY_PROGRESS_PATH "/run/onekvm-recovery-progress"

int recovery_panel_draw(const struct recovery_addrs *addrs, enum recovery_button button, int focused);
/* Whether a touch at raw (x, y), with axis maxima (max_x, max_y) in the
 * panel's portrait orientation, lands on the Reboot button. */
int recovery_panel_hit(int x, int y, int max_x, int max_y);
/* Whether a progress JSON line reports an upload or install in progress. */
int recovery_progress_busy(const char *json);

#endif
