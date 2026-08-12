#include <poll.h>
#include <termios.h>
#include <unistd.h>

int main(void)
{
    struct pollfd input = { .fd = STDIN_FILENO, .events = POLLIN };
    char buffer[256];
    ssize_t count;

    if (poll(&input, 1, 1000) <= 0)
        return 1;

    count = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count != 1 || poll(&input, 1, 50) > 0) {
        tcflush(STDIN_FILENO, TCIFLUSH);
        return 1;
    }

    return 0;
}
