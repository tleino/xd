#include "viewbuf.h"
#include "linebuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>

struct xd {
	/* address range */
	int begin;
	int end;

	/* current address */
	int dot;

	struct viewbuf *vb;

	/* active command */
	int read_fd;
	int write_fd;
	char *prompt;

	/* plugin support */
	char *plugin_cmd;
	char *plugin_re;
};

int erange(char **, struct xd *);
int nextaddr(char **, int, int, struct viewbuf *);
void parseuser(char *, struct xd *);
void readuser(struct xd *, FILE *);

static void readfile(const char *, struct viewbuf *);
static void runcmd(const char *, const char *, struct xd *);

static struct viewbuf		*read_refresh(struct viewbuf *, const char *,
				    int);
static void			 read_reply(const char *, int);

static void			 write_fd(int fd, const char *s, ...);

static char errmsg[PATH_MAX + 128];

static void
seterrmsg(const char *s)
{
#ifdef __OpenBSD__
	if (strlcpy(errmsg, s, sizeof(errmsg)) >= sizeof(errmsg))
		warnx("truncated errmsg");
#else
	if (snprintf(errmsg, sizeof(errmsg), "%s", s) >= sizeof(errmsg))
		warnx("truncated errmsg");
#endif
}

static void
readfile(const char *file, struct viewbuf *vb)
{
	char *line = NULL;
	size_t sz = 0;
	ssize_t len;
	FILE *fp;

	fp = fopen(file, "r");
	if (fp == NULL) {
		warn("%s", file);
		return;
	}

	while ((len = getline(&line, &sz, fp)) != -1) {
		line[strcspn(line, "\n")] = '\0';
		viewbuf_add(vb, line, line);
	}

	free(line);
	if (ferror(fp))
		err(1, "getline");
	fclose(fp);

	printf("%d\n", VIEWBUF_NBYTES(vb));
}

static struct viewbuf *
read_refresh(struct viewbuf *vb, const char *prompt, int fd)
{
	struct linebuf *lb;
	char *s;

	lb = linebuf_create();
	
	vb = viewbuf_create();

	while (linebuf_fill_from_fd(lb, fd) > 0) {
		while ((s = linebuf_read(lb)) != NULL)
			viewbuf_add(vb, s, s);
		if (s == NULL && linebuf_get_partial(lb) != NULL)
			if (strcmp(prompt, linebuf_get_partial(lb)) == 0)
				break;
	}

	linebuf_free(lb);

	printf("%d\n", VIEWBUF_NBYTES(vb));

	return vb;
}

static void
read_reply(const char *prompt, int fd)
{
	struct linebuf *lb;
	char *s;

	lb = linebuf_create();
	
	while (linebuf_fill_from_fd(lb, fd) > 0) {
		while ((s = linebuf_read(lb)) != NULL)
			puts(s);
		if (s == NULL && linebuf_get_partial(lb) != NULL)
			if (strcmp(prompt, linebuf_get_partial(lb)) == 0)
				break;
	}

	linebuf_free(lb);
}

static void
write_fd(int fd, const char *s, ...)
{
	va_list ap;
	static char buf[1024];
	ssize_t len;

	va_start(ap, s);
	len = vsnprintf(buf, sizeof(buf), s, ap);
	va_end(ap);

	if (len < 0)
		warn("vsnprintf");
	else if (len >= sizeof(buf))
		warnx("vsnprintf: truncated");
	else if (len > 0)
		write(fd, buf, len);
}

static void
runcmd(const char *cmd, const char *url, struct xd *xd)
{
	pid_t pid;
	int status;
	int pfds[2];
	int pfds2[2];
	uint32_t rnd;
	static char prompt[32 + 1];

	pipe(pfds);
	pipe(pfds2);

#ifdef __OpenBSD__
	rnd = arc4random();
#else
	rnd = rand();
#endif
	snprintf(prompt, sizeof(prompt), "%x", rnd);

	pid = fork();

	if (pid == 0) {
		dup2(pfds[1], 1);
		dup2(pfds2[0], 0);
		close(pfds[0]);
		execlp(cmd, cmd, "-p", prompt, url, NULL);
		warn("execlp");
		_exit(127);
	} else {
		close(pfds[1]);
		close(pfds2[0]);
	}

	xd->write_fd = pfds2[1];
	xd->read_fd = pfds[0];
	xd->prompt = prompt;

	xd->vb = read_refresh(NULL, prompt, xd->read_fd);
}

/*
 * cpat: compiled pattern
 */
int
cpat(char **ibuf, regex_t *re)
{
	char delim;
	char *p, *s;
	int err;

	if ((s = strdup(*ibuf)) == NULL)
		errx(1, "strdup");

	delim = *s;
	p = s+1;
	while (*p != delim && *p != '\0')
		p++;
	*ibuf += (p - s);
	if (*p != '\0')
		(*ibuf)++;
	*p = '\0';

	if ((err = regcomp(re, s+1, 0)) != 0)
		goto error;

	free(s);
	return 0;

error:
	regerror(err, re, errmsg, sizeof(errmsg));
	free(s);
	return -1;
}

int
matchln(int dot, struct viewbuf *vb, regex_t *re)
{
	int i;
	int err;
	char *s;
	regmatch_t pmatch[16];

	for (i = dot; i < VIEWBUF_NLINES(vb); i++) {
		s = viewbuf_get(vb, i-1);

		if ((err = regexec(re, s, 16, pmatch, 0)) == 0)
			break;
		else if (err != REG_NOMATCH)
			goto error;
	}
	if (i == VIEWBUF_NLINES(vb))
		goto nomatch;

	regfree(re);
	return i;

error:
	regerror(err, re, errmsg, sizeof(errmsg));
	goto final;
nomatch:
	seterrmsg("no match");
final:
	regfree(re);
	return -1;
}

int
matchstr(const char *s, const char *re)
{
	regmatch_t pmatch;
	regex_t cre;
	int e;

	if ((e = regcomp(&cre, re, 0)) != 0)
		goto error;
	if ((e = regexec(&cre, s, 1, &pmatch, 0)) == REG_NOMATCH) {
		regfree(&cre);
		goto error;
	}

	regfree(&cre);
	return 1;

error:
	return 0;
}

/*
 * extract address range
 *
 * [address[,address]]
 */
int
erange(char **ibuf, struct xd *xd)
{
	int begin, end, last;
	int addrcnt;

	if (xd->vb == NULL)
		goto error;

	addrcnt = 0;
	last = VIEWBUF_NLINES(xd->vb);
	end = -1;
	if ((begin = nextaddr(ibuf, xd->dot, last, xd->vb)) >= 0) {
		addrcnt++;
		if (**ibuf == ',') {
			(*ibuf)++;
			if ((end = nextaddr(ibuf, begin,
			    last, xd->vb)) == -1)
				goto error;
			else
				addrcnt++;
		} else
			end = begin;
	} else
		goto error;

	xd->dot = xd->begin = begin;
	xd->end = end;

	return addrcnt;
error:
	return -1;
}

int
nextaddr(char **ibuf, int current, int last, struct viewbuf *vb)
{
	unsigned char c;
	int addr, n;
	int first = 1;
	regex_t re = { 0 };

	addr = current;
	do {
		switch ((c = (unsigned char) **ibuf)) {
		case '+': case '-':
			(*ibuf)++;
			if (isdigit((unsigned char) **ibuf))
				n = (int) strtol(*ibuf, ibuf, 10);
			else
				n = 1;
			addr += (c == '-') ? -n : n;
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (!first)
				goto invalid_addr;
			addr = (int) strtol(*ibuf, ibuf, 10);
			break;
		case '.': case '$':
			if (!first)
				goto invalid_addr;
			addr = (c == '.') ? current : last;
			(*ibuf)++;
			break;
		case '/': case '?':
			if (!first)
				goto invalid_addr;
			if (cpat(ibuf, &re) != 0)
				return -1;
			if ((n = matchln(addr, vb, &re)) < 0)
				return -1;
			addr = n;
			break;
		default:
			if (addr < 0 || addr >= last)
				goto invalid_addr;
			return addr;
		}
		first = 0;
	} while (**ibuf != '\0');

	return addr;

invalid_addr:
	seterrmsg("invalid address");
	return -1;
}

#define CF_PRINT (1 << 0)
#define CF_UNAMBIGIOUS (1 << 1)
#define CF_NUMBERED (1 << 2)

int
readflags(char **ibuf, int flags)
{
	int done = 0;

	do {
		switch (**ibuf) {
		case 'p':
			flags |= CF_PRINT;
			(*ibuf)++;
			break;
		case 'l':
			flags |= CF_UNAMBIGIOUS;
			(*ibuf)++;
			break;
		case 'n':
			flags |= CF_NUMBERED;
			(*ibuf)++;
			break;
		default:
			done++;
		}
	} while (!done);
	if (**ibuf != '\0') {
		seterrmsg("invalid command suffix");
		return -1;
	}
	return flags;
}

int
printlns(int *dot, int end, int flags, struct viewbuf *vb)
{
	int i;
	const char *s;

	if (end-1 >= VIEWBUF_NLINES(vb)) {
		seterrmsg("invalid address - should not happen");
		return -1;
	}

	for (i = *dot; i <= end; i++) {
		s = viewbuf_get(vb, i-1);
		if (flags & CF_NUMBERED)
			printf("%d\t%s\n", i, s);
		else
			puts(s);
	}
	*dot = i;
}

void
parseuser(char *ibuf, struct xd *xd)
{
	int i, addrcnt;
	int flags = 0;
	char c;
	char *p, *begin;

	if (strncmp(ibuf, "plugin", strlen("plugin")) == 0) {
		p = begin = ibuf + strlen("plugin") + 1;
		while (!isspace(*p) && *p != '\0')
			p++;
		*p = '\0';
		xd->plugin_cmd = strdup(begin);
		p++;
		while (isspace(*p))
			p++;
		begin = p;
		while (!isspace(*p) && *p != '\0')
			p++;
		xd->plugin_re = strdup(begin);
	}

	c = *ibuf;

	switch (c) {
	case 'h':
		puts((*errmsg == '\0') ? "no error" : errmsg);
		return;
	case 'e':
		ibuf++;
		while (isspace(*ibuf))
			ibuf++;
		viewbuf_free(xd->vb);
		xd->vb = NULL;
		xd->dot = 1;
		if (xd->plugin_re != NULL && matchstr(ibuf, xd->plugin_re))
			runcmd(xd->plugin_cmd, ibuf, xd);
		else
			readfile(ibuf, xd->vb);
		return;
	}

	if ((addrcnt = erange(&ibuf, xd)) < 0) {
		puts("?");
		return;
	}

	c = *ibuf;

	switch (c) {
	case 'p':
	case 'n':
		if ((flags = readflags(&ibuf, flags)) < 0) {
			puts("?");
			return;
		}
	}

	switch (c) {
	case 'p':
	case 'n':
		printlns(&xd->dot, xd->end,
		    (c == 'n') ? (flags | CF_NUMBERED) : flags, xd->vb);
		break;
	case 'x':
		write_fd(xd->write_fd, "x %d\n", xd->dot);
		xd->vb = read_refresh(NULL, xd->prompt, xd->read_fd);
		break;
	case 'l':
		write_fd(xd->write_fd, "l %d\n", xd->dot);
		read_reply(xd->prompt, xd->read_fd);
		break;
	case '\0':
		printlns(&xd->dot, xd->dot, 0, xd->vb);
		break;
	default:
		seterrmsg("invalid command");
		puts("?");
		break;
	}
}

void
readuser(struct xd *xd, FILE *fp)
{
	char *ibuf = NULL;
	size_t sz = 0;
	ssize_t len;

	while ((len = getline(&ibuf, &sz, fp)) != -1) {
		ibuf[strcspn(ibuf, "\r\n")] = '\0';
		parseuser(ibuf, xd);
	}

	free(ibuf);
	if (ferror(stdin))
		err(1, "getline");
}

static void
readconfig(struct xd *xd)
{
	FILE *fp;
	char *home;
	char file[PATH_MAX + 1];

	home = getenv("HOME");
	if (home == NULL)
		home = "/";

	if (snprintf(file, sizeof(file), "%s/.xd", home) >= sizeof(file))
		return;

	fp = fopen(file, "r");
	if (fp == NULL)
		return;

	readuser(xd, fp);
	fclose(fp);
}

int
main(int argc, char *argv[])
{
	struct viewbuf *vb = NULL;
	struct xd xd = { 0 };
	char *file = NULL;

	vb = viewbuf_create();

	if (argc == 2) {
		file = argv[1];
	}

	readconfig(&xd);

	if (file != NULL)
		readfile(file, vb);

	xd.dot = 1;
	xd.vb = vb;

	readuser(&xd, stdin);

	viewbuf_free(xd.vb);
	return 0;
}
