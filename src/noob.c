#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESCAPE_SEQ(s) "\x1b[" s

void editor_clear_screen();

struct termios orig_termios;

void die(const char *s)
{
	editor_clear_screen();

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

char editor_read_key()
{
	char c;
	int read_count;
	while ((read_count = read(STDIN_FILENO, &c, 1)) != 1) {
		if (read_count == -1 && errno != EAGAIN) {
			die("Error while trying to read");
		}
	}
	return c;
}

void editor_draw_rows()
{
	for (int i = 0; i < 24; i++) {
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}

void editor_clear_screen()
{
	write(STDOUT_FILENO, ESCAPE_SEQ("2J"), 4);
	write(STDOUT_FILENO, ESCAPE_SEQ("H"), 3);
}

void editor_refresh_screen()
{
	editor_clear_screen();
	editor_draw_rows();

	write(STDOUT_FILENO, ESCAPE_SEQ("H"), 3);
}

void editor_process_keypress()
{
	char c = editor_read_key();

	switch (c) {
		case CTRL_KEY('q'): 
			editor_clear_screen();
			exit(0);
			break;
	}
}

int main()
{
	enable_raw_mode();
	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

	return 0;
}

