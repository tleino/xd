#ifndef VIEWBUF_H
#define VIEWBUF_H

struct viewline {
	int lineno;
	char *text;
	char *src;
};

struct viewbuf {
	int bytes;
	int bytes_src;
	int nlines;
	int alloc;
	struct viewline *lines;
};

#define VIEWBUF_NLINES(_x)	(_x)->nlines
#define VIEWBUF_NBYTES(_x)	(_x)->bytes
#define VIEWBUF_NBYTES_SRC(_x)	(_x)->bytes_src
#define VIEWBUF_LINE(_x, _y)	(_x)->lines[(_y)].text
#define VIEWBUF_SRC(_x, _y)	(_x)->lines[(_y)].src

struct viewbuf			*viewbuf_create();
void				 viewbuf_free(struct viewbuf *);
const char			*viewbuf_get(struct viewbuf *, int);
void				 viewbuf_add(struct viewbuf *, const char *,
				    const char *);

#endif
