#include <stdlib.h>
#include <string.h>

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

