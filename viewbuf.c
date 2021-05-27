#include "viewbuf.h"

#include <stdlib.h>
#include <string.h>
#include <err.h>

struct viewbuf *
viewbuf_create()
{
	struct viewbuf *vb;

	vb = calloc(1, sizeof(struct viewbuf));
	if (vb == NULL)
		err(1, "calloc");
	return vb;
}

void
viewbuf_free(struct viewbuf *vb)
{
	int i;

	i = 0;
	while (vb->nlines--) {
		free(vb->lines[i].text);
		free(vb->lines[i].src);
		i++;
	}

	if (vb->lines != NULL)
		free(vb->lines);

	free(vb);
}

static void
viewbuf_make_space(struct viewbuf *vb)
{
	if (vb->nlines == vb->alloc) {
		if (vb->alloc == 0)
			vb->alloc = 64;
		vb->alloc *= 2;
		vb->lines = realloc(vb->lines, sizeof(struct viewline) *
		    vb->alloc);
		if (vb->lines == NULL)
			err(1, "realloc");
	}
}

void
viewbuf_add(struct viewbuf *vb, const char *line, const char *src)
{
	struct viewline *vl;

	viewbuf_make_space(vb);

	vl = &vb->lines[vb->nlines++];
	vl->lineno = vb->nlines;
	vl->text = strdup(line);
	if (vl->text == NULL)
		err(1, "strdup");
	vl->src = strdup(src);
	if (vl->src == NULL)
		err(1, "strdup");

	/*
	 * The bytes count is the size of rendered and wrapped text,
	 * and is useful for knowing the space required if saving the
	 * file as rendered. Practically it is useful for getting a
	 * fuzzy knowledge of the size of the content when reading.
	 */
	vb->bytes += strlen(vl->text) + strlen("\n");
	vb->bytes_src += strlen(src) + strlen("\n");
}

const char *
viewbuf_get(struct viewbuf *vb, int lineno)
{
	return vb->lines[lineno].text;
}
