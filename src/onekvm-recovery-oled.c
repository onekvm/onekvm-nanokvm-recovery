#include "recovery-display.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PANEL_MAX_INPUTS 8
#define PANEL_CONFIRM_MS 5000
#define PANEL_REFRESH_MS 1000
#define LONG_BITS (8 * sizeof(long))

enum { PANEL_TAP = 1, PANEL_PRESS = 2, PANEL_TURN = 4 };

struct panel_input {
	int fd;
	int touch;
	int max_x, max_y;
	int x, y;
};

static int usage(void)
{
	fprintf(stderr, "usage: onekvm-recovery-oled {canvas|show|off|print|status|panel|--self-test}\n");
	return 2;
}

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int has_bit(const unsigned long *bits, int bit)
{
	return (bits[bit / LONG_BITS] >> (bit % LONG_BITS)) & 1;
}

/* The touchscreen, any key device with Enter (the Pro's knob press) and the
 * knob's rotary encoder. */
static int panel_open_inputs(struct panel_input *inputs, int max)
{
	DIR *dir = opendir("/dev/input");
	struct dirent *entry;
	int count = 0;

	if (!dir)
		return 0;
	while (count < max && (entry = readdir(dir))) {
		unsigned long keys[KEY_MAX / LONG_BITS + 1] = { 0 };
		unsigned long abs[ABS_MAX / LONG_BITS + 1] = { 0 };
		unsigned long rel[REL_MAX / LONG_BITS + 1] = { 0 };
		struct input_absinfo ax, ay;
		char path[300];
		int fd;

		if (strncmp(entry->d_name, "event", 5) ||
		    snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name) >= (int)sizeof(path))
			continue;
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		(void)ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		(void)ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
		(void)ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel);
		if (has_bit(keys, BTN_TOUCH) && has_bit(abs, ABS_X) && has_bit(abs, ABS_Y) &&
		    ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &ay) == 0)
			inputs[count++] = (struct panel_input){ .fd = fd, .touch = 1,
				.max_x = ax.maximum, .max_y = ay.maximum, .x = -1, .y = -1 };
		else if (has_bit(keys, KEY_ENTER) || has_bit(rel, REL_X) || has_bit(rel, REL_Y) ||
			 has_bit(rel, REL_DIAL) || has_bit(rel, REL_WHEEL))
			inputs[count++] = (struct panel_input){ .fd = fd };
		else
			close(fd);
	}
	closedir(dir);
	return count;
}

static int panel_busy(void)
{
	char json[512];
	ssize_t got;
	int fd = open(RECOVERY_PROGRESS_PATH, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return 0;
	got = read(fd, json, sizeof(json) - 1);
	close(fd);
	if (got <= 0)
		return 0;
	json[got] = 0;
	return recovery_progress_busy(json);
}

/* Same as the recovery web's reboot: clear the boot retry counter so the
 * next boot tries the system, then reboot without init. */
static int panel_reboot(void)
{
	pid_t child = fork();
	int status;

	if (child < 0)
		return -1;
	if (child == 0) {
		execl("/usr/sbin/onekvm-recovery-clear-marker", "onekvm-recovery-clear-marker",
		      "--boot-retry", (char *)NULL);
		_exit(127);
	}
	while (waitpid(child, &status, 0) < 0)
		if (errno != EINTR)
			return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		return -1;
	sync();
	reboot(RB_AUTOBOOT);
	return -1;
}

/* Returns PANEL_* flags: a touch released on the button, a knob press or a
 * knob turn; -1 when the device went away. */
static int panel_read(struct panel_input *input)
{
	struct input_event events[16];
	int activated = 0;

	for (;;) {
		ssize_t got = read(input->fd, events, sizeof(events));

		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0 && errno == EAGAIN)
			return activated;
		if (got <= 0)
			return -1;
		for (size_t i = 0; i < (size_t)got / sizeof(events[0]); i++) {
			const struct input_event *ev = &events[i];

			if (input->touch && ev->type == EV_ABS && ev->code == ABS_X)
				input->x = ev->value;
			else if (input->touch && ev->type == EV_ABS && ev->code == ABS_Y)
				input->y = ev->value;
			else if (input->touch && ev->type == EV_KEY && ev->code == BTN_TOUCH && ev->value == 0)
				activated |= recovery_panel_hit(input->x, input->y, input->max_x, input->max_y) ?
					     PANEL_TAP : 0;
			else if (!input->touch && ev->type == EV_KEY && ev->code == KEY_ENTER && ev->value == 1)
				activated |= PANEL_PRESS;
			else if (!input->touch && ev->type == EV_REL && ev->value)
				activated |= PANEL_TURN;
		}
	}
}

/* Draw the addresses with a Reboot button and handle the touchscreen and
 * knob. Turning the knob, or pressing it, focuses the button; a tap or a
 * press on the focused button asks for confirmation for five seconds, and a
 * second one reboots. The button is disabled while firmware uploads or
 * installs. */
static int panel_run(void)
{
	struct panel_input inputs[PANEL_MAX_INPUTS];
	struct recovery_addrs addrs;
	enum recovery_button button = RECOVERY_BUTTON_IDLE;
	long long confirm_until = 0, next_refresh = 0;
	int count = 0, dirty = 1, focused = 0;

	memset(&addrs, 0, sizeof(addrs));
	if (recovery_collect_ipv4(&addrs) < 0)
		memset(&addrs, 0, sizeof(addrs));
	if (panel_busy())
		button = RECOVERY_BUTTON_BUSY;
	/* Fails unless this is the Pro's panel framebuffer. */
	if (recovery_panel_draw(&addrs, button, focused) < 0)
		return 1;
	dirty = 0;
	for (;;) {
		struct pollfd fds[PANEL_MAX_INPUTS];
		long long now = now_ms();
		long long wake;

		if (now >= next_refresh) {
			struct recovery_addrs fresh;
			int busy = panel_busy();

			memset(&fresh, 0, sizeof(fresh));
			if (recovery_collect_ipv4(&fresh) < 0)
				memset(&fresh, 0, sizeof(fresh));
			if (memcmp(&fresh, &addrs, sizeof(addrs))) {
				addrs = fresh;
				dirty = 1;
			}
			if (busy && button != RECOVERY_BUTTON_BUSY) {
				button = RECOVERY_BUTTON_BUSY;
				dirty = 1;
			} else if (!busy && button == RECOVERY_BUTTON_BUSY) {
				button = RECOVERY_BUTTON_IDLE;
				dirty = 1;
			}
			/* Input drivers can bind after Recovery starts. */
			if (!count)
				count = panel_open_inputs(inputs, PANEL_MAX_INPUTS);
			next_refresh = now + PANEL_REFRESH_MS;
		}
		if (button == RECOVERY_BUTTON_CONFIRM && now >= confirm_until) {
			button = RECOVERY_BUTTON_IDLE;
			dirty = 1;
		}
		if (dirty) {
			(void)recovery_panel_draw(&addrs, button, focused);
			dirty = 0;
		}
		wake = next_refresh;
		if (button == RECOVERY_BUTTON_CONFIRM && confirm_until < wake)
			wake = confirm_until;
		for (int i = 0; i < count; i++)
			fds[i] = (struct pollfd){ .fd = inputs[i].fd, .events = POLLIN };
		if (poll(fds, (nfds_t)count, wake > now ? (int)(wake - now) : 0) < 0 && errno != EINTR)
			return 1;
		for (int i = 0; i < count; i++) {
			int result;

			if (!fds[i].revents)
				continue;
			result = panel_read(&inputs[i]);
			if (result < 0) {
				/* A device went away: reopen all of them on the next refresh. */
				for (int j = 0; j < count; j++)
					close(inputs[j].fd);
				count = 0;
				break;
			}
			if ((result & (PANEL_TURN | PANEL_PRESS)) && !focused) {
				/* The first knob action only selects the button. */
				focused = 1;
				dirty = 1;
				result &= ~(PANEL_TURN | PANEL_PRESS);
			}
			result &= ~PANEL_TURN;
			if (!result || button == RECOVERY_BUTTON_BUSY)
				continue;
			if (button == RECOVERY_BUTTON_IDLE) {
				button = RECOVERY_BUTTON_CONFIRM;
				confirm_until = now_ms() + PANEL_CONFIRM_MS;
				dirty = 1;
			} else if (button == RECOVERY_BUTTON_CONFIRM) {
				if (panel_busy()) {
					button = RECOVERY_BUTTON_BUSY;
				} else {
					(void)recovery_panel_draw(&addrs, RECOVERY_BUTTON_REBOOTING, 0);
					(void)panel_reboot();
					button = RECOVERY_BUTTON_IDLE;
				}
				dirty = 1;
			}
		}
	}
}

int main(int argc, char **argv)
{
	struct recovery_addrs addrs;
	char text[512];

	if (argc != 2)
		return usage();
	if (!strcmp(argv[1], "--self-test"))
		return recovery_render_self_test();
	if (!strcmp(argv[1], "canvas"))
		return recovery_oled_canvas() == 0 ? 0 : 1;
	if (!strcmp(argv[1], "off"))
		return recovery_oled_off() == 0 ? 0 : 1;
	if (!strcmp(argv[1], "show")) {
		if (recovery_collect_ipv4(&addrs) < 0)
			addrs.count = 0;
		return recovery_oled_show_addrs(&addrs) == 0 ? 0 : 1;
	}
	if (!strcmp(argv[1], "panel"))
		return panel_run();
	if (!strcmp(argv[1], "print")) {
		if (recovery_format_banner(text, sizeof(text)) < 0)
			return 1;
		fputs(text, stdout);
		return 0;
	}
	if (!strcmp(argv[1], "status")) {
		if (recovery_status_json(text, sizeof(text)) < 0)
			return 1;
		fputs(text, stdout);
		return 0;
	}
	return usage();
}
