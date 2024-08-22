/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "append_buffer.c"
#include "string.c"
#include "window.c"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define NOOB_VERSION "1.0.0"
#define NOOB_TAB_STOP 8
#define CTRL_KEY(k) ((k) & 0x1f)
#define ESCAPE_SEQ(s) "\x1b[" s

/*** data ***/

struct editorRow
{
	int size;
	int render_size;
	char *chars;
	char *render;
};

struct editorConfig
{
	int cursor_x, cursor_y, render_x;
	int screen_rows,screen_cols;
	int row_offset, col_offset;
	int num_rows;
	struct editorRow *rows;
	struct termios orig_termios;
	char *filename;
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

int editor_row_cursor_x_to_render_x(struct editorRow *row, int cursor_x)
{
	int render_x = 0;

	for (int i = 0; i < cursor_x; i++) {
		if (row->chars[i] == '\t') {
			render_x += (NOOB_TAB_STOP - 1) - (render_x % NOOB_TAB_STOP);
		}
		render_x++;
	}
	return render_x;
}

void editor_update_row(struct editorRow *row)
{
	int index = 0;
	int tab_count = char_count(row->chars, row->size, '\t');

	free(row->render);
	row->render = malloc(row->size + tab_count * (NOOB_TAB_STOP - 1) + 1);
	
	for (int i = 0; i < row->size; i++)
	{
		if (row->chars[i] == '\t') {
			row->render[index++] = ' ';
			while (index % NOOB_TAB_STOP != 0) row->render[index++] = ' ';
		}
		else {
			row->render[index++] = row->chars[i];
		}
	}
	row->render[index] = '\0';
	row->render_size = index;
}

void editor_append_row(char *line, size_t line_len) {
	editor.rows = realloc(editor.rows, sizeof(struct editorRow) * (editor.num_rows + 1));

	int at = editor.num_rows;
	editor.rows[at].size = line_len;
	editor.rows[at].chars = malloc(line_len + 1);
	memcpy(editor.rows[at].chars, line, line_len);
	editor.rows[at].chars[line_len] = '\0';

	editor.rows[at].render_size = 0;
	editor.rows[at].render = NULL;
	editor_update_row(&editor.rows[at]);

	editor.num_rows++;
}

void editor_row_insert_char(struct editorRow *row, int at, int c)
{
	if (at < 0 || at > row->size) {
		at = row->size;
	}

	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editor_update_row(row);
}

void editor_insert_char(int c)
{
	if (editor.cursor_y == editor.num_rows) {
		editor_append_row("", 0);
	}

	editor_row_insert_char(&editor.rows[editor.cursor_y], editor.cursor_x, c);
	editor.cursor_x++;
}

void editor_row_delete_char(struct editorRow *row, int at)
{
	if (at < 0 || at > row->size - 1) return;

	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editor_update_row(row);
}

void editor_delete_char()
{
	if (editor.cursor_y == editor.num_rows)
		return;

	struct editorRow *row = &editor.rows[editor.cursor_y];
	if (editor.cursor_x >= 0) {
		editor_row_delete_char(row, editor.cursor_x);
		if (editor.cursor_x - 1 >= 0)
			editor.cursor_x--;
		else
			editor.cursor_x = 0;
	}
}

/*** file i/o ***/

void editor_open(char *filename)
{
	FILE *file = fopen(filename, "r");
	editor.filename = strdup(filename);
	if (!file) {
		die("Could not open file");
	}

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;
	while ((line_len = getline(&line, &line_cap, file)) != -1) {
			while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
			line_len--;
		}
		editor_append_row(line, line_len);
	}
	free(line);
	fclose(file);
}

/*** output ***/
void editor_scroll()
{
	editor.render_x = 0;
	if (editor.cursor_y < editor.num_rows) {
		editor.render_x = editor_row_cursor_x_to_render_x(&editor.rows[editor.cursor_y], editor.cursor_x);
	}

	if (editor.cursor_y < editor.row_offset) {
		editor.row_offset = editor.cursor_y;
	}

	if (editor.cursor_y >= editor.row_offset + editor.screen_rows) {
		editor.row_offset = editor.cursor_y - editor.screen_rows + 1;
	}

	if (editor.cursor_x < editor.col_offset) {
		editor.col_offset = editor.render_x;
	}

	if (editor.cursor_x >= editor.col_offset + editor.screen_cols) {
		editor.col_offset = editor.render_x - editor.screen_cols + 1;
	}
}

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
		int current_row = i + editor.row_offset;
		if (current_row >= editor.num_rows) {
			if (editor.num_rows == 0 && i == editor.screen_rows / 3) {
				editor_draw_welcome(ab);
			}
			else {
				ab_append(ab, "~", 1);
			}
		}
		else {
			int len = editor.rows[current_row].render_size - editor.col_offset;
			if (len < 0) {
				len = 0;
			}
			if (len > editor.screen_cols) {
				len = editor.screen_cols;
			}
			ab_append(ab, &editor.rows[current_row].render[editor.col_offset], len);
		}

		ab_append(ab, ESCAPE_SEQ("K"), 3);
		ab_append(ab, "\r\n", 2);
	}
}

void editor_draw_status_bar(struct append_buffer *ab)
{
	ab_append(ab, ESCAPE_SEQ("7m"), 4);
	char status[80], right_status[80];
	char *filename = editor.filename;
	int len = snprintf(status, sizeof(status), "%.20s - %d lines", filename ? filename : "New file", editor.num_rows);
	int right_len = snprintf(right_status, sizeof(right_status), "%d:%d  ", editor.cursor_x + 1, editor.cursor_y+1);

	if (len > editor.screen_cols) len = editor.screen_cols;
	ab_append(ab, status, len);
	while (len < editor.screen_cols) {
		if (editor.screen_cols - len == right_len) {
			ab_append(ab, right_status, right_len);
			break;
		}
		else {
			ab_append(ab, " ", 1);
			len++;
		}
	}
	ab_append(ab, ESCAPE_SEQ("m"), 3);
}

void editor_refresh_screen()
{
	editor_scroll();

	struct append_buffer ab = { NULL, 0 };

	ab_append(&ab, ESCAPE_SEQ("?25l"), 6);
	ab_append(&ab, ESCAPE_SEQ("H"), 3);

	editor_draw_rows(&ab);
	editor_draw_status_bar(&ab);

	char buffer[32];
	snprintf(buffer,
	  sizeof(buffer),
	  ESCAPE_SEQ("%d;%dH"),
	  editor.cursor_y - editor.row_offset + 1,
	  editor.render_x - editor.col_offset + 1
	);
	
	ab_append(&ab, buffer, strlen(buffer));
	ab_append(&ab, ESCAPE_SEQ("?25h"), 6);

	write(STDOUT_FILENO, ab.buffer, ab.len);
	ab_free(&ab);
}

/*** input ***/

struct editorRow *get_current_row()
{
	if (editor.cursor_y >= editor.num_rows)
		return NULL;

	return &editor.rows[editor.cursor_y];
}

void editor_move_cursor(char key)
{
	struct editorRow *row = get_current_row();
	switch (key) {
		case 'h':
			if (editor.cursor_x != 0)
				editor.cursor_x--;
			break;
		case 'l':
			if (row && editor.cursor_x < row->size - 1)
				editor.cursor_x++;
			break;
		case 'k':
			if (editor.cursor_y != 0)
				editor.cursor_y--;
			break;
		case 'j':
			if (editor.cursor_y < editor.num_rows)
				editor.cursor_y++;
			break;
	}

	row = get_current_row();
	int row_len = row ? row->size : 0;
	if (editor.cursor_x > row_len) {
		editor.cursor_x = row_len ? row_len - 1 : 0;
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
		case 'x':
			editor_delete_char();
			break;
		case 'h':
		case 'j':
		case 'k':
		case 'l':
			editor_move_cursor(c);
			break;
		default:
			editor_insert_char(c);
	}
}

/*** init ***/

void init_editor()
{
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.render_x = 0;
	editor.num_rows = 0;
	editor.row_offset = 0;
	editor.col_offset = 0;
	editor.rows = NULL;
	editor.filename = NULL;

	int result = get_window_size(&editor.screen_rows, &editor.screen_cols);
	if (result == -1) {
		die("Error while trying to get the window size");
	}
	editor.screen_rows -= 1;
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

