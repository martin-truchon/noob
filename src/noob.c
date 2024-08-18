#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define QUIT_CHAR 'q'

struct termios orig_termios;

void die(const char *s)
{
	perror(s);
	exit(1);
}

void disable_raw_mode()
{
	int set_attr_result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
	if (set_attr_result == -1) {
		die("Error while trying to call tcsetattr");
	}
}

void enable_raw_mode()
{
	int get_attr_result = tcgetattr(STDIN_FILENO, &orig_termios);
	if (get_attr_result == -1) {
		die("Error while trying to call tcgetattr");
	}

	atexit(disable_raw_mode);
	struct termios raw = orig_termios;
	raw.c_iflag &= ~(IXON); // Turn off software flow control
	raw.c_oflag &= ~(OPOST); // Turn off output processing
	raw.c_lflag &= ~(ECHO | ICANON); // Turn off echoing and canonical mode
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	int set_attr_result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	if (set_attr_result == -1) {
		die("Error while trying to call tcsetattr");
	}
}

int main()
{
	enable_raw_mode();
	
	while (1) {
		char c = '\0';
		int read_result = read(STDIN_FILENO, &c, 1);
		if (read_result == -1 && errno != EAGAIN) {
			die("Error while trying to read");
		}

		if (iscntrl(c)) {
			printf("%d\r\n", c);
		}
		else{
			printf("%d ('%c')\r\n", c, c);
		}

		if (c == QUIT_CHAR) break;
	}

	return 0;
}

