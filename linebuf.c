/*
 * Read arbitrary length data in optimal chunks and split to lines
 * as separated by CRLF or LF.
 */

#include "linebuf.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>

#define READ_BUFFER_ALLOC 4096
#define READ_BUFFER_CHUNK 512

struct linebuf
{
	size_t alloc;	/* memory allocated for the buffer */
	size_t chunk;	/* read chunk size */
	size_t sz;	/* bytes currently in a line */
	size_t shrink;	/* shrink buffer by n bytes from beginning */
	char *buf;	/* line data */
};

static void linebuf_make_space(struct linebuf *);

struct linebuf *
linebuf_create()
{
	struct linebuf *line;

	line = calloc(1, sizeof(struct linebuf));
	if (line == NULL)
		err(1, "calloc");

	line->alloc = READ_BUFFER_ALLOC;
	line->chunk = READ_BUFFER_CHUNK;
	line->sz = 0;
	line->shrink = 0;
	if ((line->buf = malloc(line->alloc)) == NULL)
		err(1, "init_tls_line_buffer");	

	return line;
}

void
linebuf_free(struct linebuf *line)
{
	if (line->buf != NULL)
		free(line->buf);
	free(line);
}

static void
linebuf_make_space(struct linebuf *line)
{
	if (line->sz >= line->alloc - line->chunk) {
		line->alloc *= 2;
		line->buf = realloc(line->buf, line->alloc);
	}
}

int
linebuf_fill_from_fd(struct linebuf *line, int fd)
{
	int n;

	linebuf_make_space(line);

	n = read(fd, &line->buf[line->sz], line->chunk);
	if (n < 0)
		warn("read");
	line->sz += n;
	return n;
}

char *
linebuf_read(struct linebuf *line)
{
	size_t i;

	if (line->shrink > 0) {
		line->sz -= line->shrink;
		if (line->sz >= 1)
			memmove(line->buf, &line->buf[line->shrink], line->sz);
		else
			line->sz = 0;
		line->shrink = 0;
		line->buf[line->sz] = '\0';
	}

	for (i = 0; i < line->sz; i++) {
		if (line->buf[i] != '\n')
			continue;
		if (i > 0 && line->buf[i-1] == '\r')
			line->buf[i-1] = '\0';

		line->buf[i] = '\0';
		line->shrink = (i + 1);

		return line->buf;
	}

	return NULL;
}

const char *
linebuf_get_partial(struct linebuf *line)
{
	return line->buf;
}
