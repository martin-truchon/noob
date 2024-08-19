/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/ioctls.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define NOOB_VERSION "1.0.0"
#define CTRL_KEY(k) ((k) & 0x1f)
#define ESCAPE_SEQ(s) "\x1b[" s

/*** data ***/
struct editorRow
{
	int size;
	char *chars;
};

struct editorConfig
{
	int cursor_x;
	int cursor_y;
	int screen_rows;
	int screen_cols;
	int num_rows;
	struct editorRow row;
	struct termios orig_termios;
};

struct editorConfig editor;

/*** terminal ***/

void die(const char *s)
{
	write(STDOUT_FILENO, ESCAPE_SEQ("2J"), 4);
	write(STDOUT_FILENO, ESCAPE_SEQ("H"), 3);

	perror(s);
	exit(1);
}

void disable_raw_mode()
{
	int set_attr_result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.orig_termios);
	if (set_attr_result == -1) {
		die("Error while trying to call tcsetattr");
	}
}

void enable_raw_mode()
{
	int get_attr_result = tcgetattr(STDIN_FILENO, &editor.orig_termios);
	if (get_attr_result == -1) {
		die("Error while trying to call tcgetattr");
	}

	atexit(disable_raw_mode);
	struct termios raw = editor.orig_termios;
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

int get_window_size(int *rows, int *cols)
{
	struct winsize size;
	int ioctl_result = ioctl(0, TIOCGWINSZ, &size);
	if (ioctl_result == -1 || size.ws_col == 0) {
		return -1;
	}
	else {
		*rows = size.ws_row;
		*cols = size.ws_col;
		return 0;
	}
}
/*** file i/o ***/

void editor_open(char *filename)
{
	FILE *file = fopen(filename, "r");
	if (!file) {
		die("Could not open file");
	}

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;
	line_len = getline(&line, &line_cap, file);
	if (line_len != -1) {
		while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
			line_len--;
		}

		editor.row.size = line_len;
		editor.row.chars = (char *)malloc(line_len + 1);
		memcpy(editor.row.chars, line, line_len);
		editor.row.chars[line_len] = '\0';
		editor.num_rows = 1;
	}
	free(line);
	fclose(file);
}

/*** append buffer ***/

struct append_buffer
{
	char *buffer;
	int len;
};

void ab_append(struct append_buffer *ab, const char *s, int len)
{
	char *new = realloc(ab->buffer, ab->len + len);

	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->buffer = new;
	ab->len += len;
}

void ab_free(struct append_buffer *ab)
{
	free(ab->buffer);
}

/*** output ***/

void editor_draw_welcome(struct append_buffer *ab)
{
	char message[80];
	int message_len = snprintf(
		message,
		sizeof(message),
		"NOOB editor -- version %s",
		NOOB_VERSION
	);

	if (message_len > editor.screen_cols) {
		message_len = editor.screen_cols;
	}

	int padding = (editor.screen_cols - message_len) / 2;
	if (padding) {
		ab_append(ab, "~", 1);
		padding--;
	}
	while (padding--) {
		ab_append(ab, " ", 1);
	}
	ab_append(ab, message, message_len);
}

void editor_draw_rows(struct append_buffer *ab)
{
	for (int i = 0; i < editor.screen_rows; i++) {
		if (i >= editor.num_rows) {
			if (editor.num_rows == 0 && i == editor.screen_rows / 3) {
				editor_draw_welcome(ab);
			}
			else {
				ab_append(ab, "~", 1);
			}
		}
		else {
			int len = editor.row.size;
			if (len > editor.screen_cols) {
				len = editor.screen_cols;
			}
			ab_append(ab, editor.row.chars, len);
		}

		ab_append(ab, ESCAPE_SEQ("K"), 3);
		if (i < editor.screen_rows - 1) {
			ab_append(ab, "\r\n", 2);
		}
	}
}

void editor_refresh_screen()
{
	struct append_buffer ab = { NULL, 0 };

	ab_append(&ab, ESCAPE_SEQ("?25l"), 6);
	ab_append(&ab, ESCAPE_SEQ("H"), 3);

	editor_draw_rows(&ab);

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", editor.cursor_y + 1, editor.cursor_x + 1);
	ab_append(&ab, buffer, strlen(buffer));
	ab_append(&ab, ESCAPE_SEQ("?25h"), 6);

	write(STDOUT_FILENO, ab.buffer, ab.len);
	ab_free(&ab);
}

/*** input ***/

void editor_move_cursor(char key)
{
	switch (key) {
		case 'h':
			if (editor.cursor_x != 0)
				editor.cursor_x--;
			break;
		case 'l':
			if (editor.cursor_x != editor.screen_cols - 1)
				editor.cursor_x++;
			break;
		case 'k':
			if (editor.cursor_y != 0)
				editor.cursor_y--;
			break;
		case 'j':
			if (editor.cursor_y != editor.screen_rows - 1)
				editor.cursor_y++;
			break;
	}
}

void editor_process_keypress()
{
	char c = editor_read_key();

	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, ESCAPE_SEQ("2J"), 4);
			write(STDOUT_FILENO, ESCAPE_SEQ("H"), 3);
			exit(0);
			break;
		case 'h':
		case 'j':
		case 'k':
		case 'l':
			editor_move_cursor(c);
			break;
	}
}

/*** init ***/

void init_editor()
{
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.num_rows = 0;

	int result = get_window_size(&editor.screen_rows, &editor.screen_cols);
	if (result == -1) {
		die("Error while trying to get the window size");
	}
}

int main(int argc, char *argv[])
{
	enable_raw_mode();
	init_editor();
	if (argc >= 2) {
		editor_open(argv[1]);
	}

	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

	return 0;
}

