#ifndef LINEBUF_H
#define LINEBUF_H

struct linebuf;
struct tls;

struct linebuf*			 linebuf_create(void);
void				 linebuf_free(struct linebuf *);

char				*linebuf_read(struct linebuf *);
int				 linebuf_fill_from_fd(struct linebuf *, int);

const char			*linebuf_get_partial(struct linebuf *);

#endif
