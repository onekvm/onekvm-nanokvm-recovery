#define main recovery_web_main
#include "../src/onekvm-recovery-web.c"
#undef main

int main(void)
{
	char *success[] = { "/bin/sh", "-c", "sleep 0.05; exit 0", NULL };
	char *failure[] = { "/bin/sh", "-c", "exit 7", NULL };
	char *missing[] = { "/missing-recovery-command", NULL };

	/* Match request workers forked by the real HTTP/HTTPS listener. */
	signal(SIGCHLD, SIG_IGN);
	if (spawn_command(success) != 0)
		return 1;
	signal(SIGCHLD, SIG_IGN);
	if (spawn_command(failure) == 0 || spawn_command(missing) == 0)
		return 1;
	puts("Recovery command exit status passed");
	return 0;
}
