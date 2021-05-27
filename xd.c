#include "viewbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <limits.h>

struct xd {
	/* address range */
	int begin;
	int end;

	/* current address */
	int dot;

	struct viewbuf *vb;
};

int erange(char **, struct xd *);
int nextaddr(char **, int, int, struct viewbuf *);
void parseuser(char *, struct xd *);
void readuser(struct xd *);

static void readfile(const char *, struct viewbuf *);

static char errmsg[PATH_MAX + 128];

static void
seterrmsg(const char *s)
{
	if (strlcpy(errmsg, s, sizeof(errmsg)) >= sizeof(errmsg))
		warnx("truncated errmsg");
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
	case 'h':
		puts((*errmsg == '\0') ? "no error" : errmsg);
		return;
	case 'p':
	case 'n':
		printlns(&xd->dot, xd->end,
		    (c == 'n') ? (flags | CF_NUMBERED) : flags, xd->vb);
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
readuser(struct xd *xd)
{
	char *ibuf = NULL;
	size_t sz = 0;
	ssize_t len;

	while ((len = getline(&ibuf, &sz, stdin)) != -1) {
		ibuf[strcspn(ibuf, "\r\n")] = '\0';
		parseuser(ibuf, xd);
	}

	free(ibuf);
	if (ferror(stdin))
		err(1, "getline");
}

int
main(int argc, char *argv[])
{
	struct viewbuf *vb;
	struct xd xd = { 0 };
	char *file = NULL;

	vb = viewbuf_create();

	if (argc == 2) {
		file = argv[1];
	}

	if (file != NULL) {
		readfile(file, vb);
		printf("%d\n", VIEWBUF_NBYTES(vb));
	}

	xd.dot = 1;
	xd.vb = vb;

	readuser(&xd);

	viewbuf_free(vb);
	return 0;
}
