#include "recovery-display.h"

#include <stdio.h>
#include <string.h>

static int usage(void)
{
	fprintf(stderr, "usage: onekvm-recovery-oled {canvas|show|off|print|status|--self-test}\n");
	return 2;
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
