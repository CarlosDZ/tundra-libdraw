#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <tundra/conf.h>
#include <tundra/draw.h>
#include <unistd.h>

#define DIR_MASK                                                               \
	(IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_CREATE | IN_DELETE |    \
	 IN_DELETE_SELF | IN_MOVE_SELF)
#define PARENT_MASK (IN_CREATE | IN_MOVED_TO | IN_ONLYDIR)

struct color {
	char name[TCONF_NAME_MAX];
	uint32_t argb;
};

struct watch {
	char dir[4096], parent[4096];
	int wd, pwd;
};

struct tdraw_theme {
	int fd;
	struct watch w[2];
	int nw;
	struct color *c;
	int n;
};

static int hex(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int color_parse(const char *v, uint32_t *argb) {
	uint32_t rgb = 0, a = 255;
	int digits = 0;

	for (int i = 0; i < 6; i++) {
		if (hex(v[i]) < 0)
			return -1;
		rgb = rgb << 4 | (uint32_t)hex(v[i]);
	}
	v += 6;
	if (*v && *v != ' ' && *v != '\t')
		return -1;
	while (*v == ' ' || *v == '\t')
		v++;
	if (*v) {
		for (a = 0; *v >= '0' && *v <= '9'; v++, digits++)
			a = a * 10 + (uint32_t)(*v - '0');
		while (*v == ' ' || *v == '\t')
			v++;
		if (!digits || digits > 3 || a > 255 || *v)
			return -1;
	}
	*argb = a << 24 | rgb;
	return 0;
}

static int by_name(const void *a, const void *b) {
	return strcmp(((const struct color *)a)->name,
	              ((const struct color *)b)->name);
}

static int theme_parse(const char *where, const char *text, size_t len,
                       struct color *c, int *n, char *err) {
	struct tconf *g = malloc(sizeof(*g));
	int r = -1;

	if (!g) {
		snprintf(err, DRAW_ERR_MAX, "out of memory");
		return -1;
	}
	if (tconf_parse(text, len, g)) {
		snprintf(err, DRAW_ERR_MAX, "%.200s:%d: %.90s", where, g->err_line,
		         g->err);
		goto out;
	}
	if (g->nblocks) {
		snprintf(err, DRAW_ERR_MAX, "%.200s:%d: a theme file has no blocks",
		         where, g->blocks[0].line);
		goto out;
	}
	for (int i = 0; i < g->n; i++) {
		const struct tconf_item *it = &g->items[i];
		uint32_t argb;
		int k;

		if (color_parse(it->value, &argb)) {
			snprintf(err, DRAW_ERR_MAX,
			         "%.200s:%d: a color is RRGGBB, then an alpha of 0 to 255",
			         where, it->line);
			goto out;
		}
		for (k = 0; k < i && strcmp(g->items[k].key, it->key); k++)
			;
		if (k < i) {
			snprintf(err, DRAW_ERR_MAX, "%.200s:%d: %s twice", where, it->line,
			         it->key);
			goto out;
		}
		for (k = 0; k < *n && strcmp(c[k].name, it->key); k++)
			;
		if (k == *n) {
			memset(c[k].name, 0, sizeof(c[k].name));
			memcpy(c[k].name, it->key, strlen(it->key));
			(*n)++;
		}
		c[k].argb = argb;
	}
	r = 0;
out:
	free(g);
	return r;
}

static void watch_init(struct watch *w, const char *dir) {
	char *slash;

	snprintf(w->dir, sizeof(w->dir), "%s", dir);
	snprintf(w->parent, sizeof(w->parent), "%s", dir);
	if ((slash = strrchr(w->parent, '/')))
		*(slash == w->parent ? slash + 1 : slash) = 0;
	w->wd = w->pwd = -1;
}

static void rewatch(struct tdraw_theme *t) {
	for (int i = 0; i < t->nw && t->fd >= 0; i++) {
		struct watch *w = &t->w[i];

		if (w->wd >= 0)
			continue;
		w->wd = inotify_add_watch(t->fd, w->dir, DIR_MASK);
		if (w->wd >= 0 && w->pwd >= 0) {
			inotify_rm_watch(t->fd, w->pwd);
			w->pwd = -1;
		} else if (w->wd < 0 && w->pwd < 0) {
			w->pwd = inotify_add_watch(t->fd, w->parent, PARENT_MASK);
		}
	}
}

static void drain(struct tdraw_theme *t) {
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t got;

	while (t->fd >= 0 && (got = read(t->fd, buf, sizeof(buf))) > 0) {
		for (char *p = buf; p < buf + got;) {
			const struct inotify_event *e = (const void *)p;

			for (int i = 0; i < t->nw; i++) {
				struct watch *w = &t->w[i];

				if (e->wd != w->wd ||
				    !(e->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)))
					continue;
				if (e->mask & IN_MOVE_SELF)
					inotify_rm_watch(t->fd, w->wd);
				w->wd = -1;
			}
			p += sizeof(*e) + e->len;
		}
	}
}

static struct tdraw_theme *theme_open_at(const char *sysdir,
                                         const char *usrdir) {
	struct tdraw_theme *t = calloc(1, sizeof(*t));

	if (!t)
		return NULL;
	t->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	watch_init(&t->w[t->nw++], sysdir);
	if (usrdir)
		watch_init(&t->w[t->nw++], usrdir);
	rewatch(t);
	return t;
}

TDRAW_API struct tdraw_theme *tdraw_theme_open(void) {
	char usr[4096];

	return theme_open_at(TDRAW_CONF_DIR,
	                     draw_user_dir(usr, sizeof(usr)) ? NULL : usr);
}

TDRAW_API void tdraw_theme_free(struct tdraw_theme *t) {
	if (!t)
		return;
	if (t->fd >= 0)
		close(t->fd);
	free(t->c);
	free(t);
}

TDRAW_API int tdraw_theme_fd(const struct tdraw_theme *t) { return t->fd; }

TDRAW_API int tdraw_theme_load(struct tdraw_theme *t, const char **errp) {
	static _Thread_local char err[DRAW_ERR_MAX];
	struct color *c = malloc(2 * TCONF_ITEMS_MAX * sizeof(*c));
	const char *ignored;
	int n = 0, changed;

	*(errp ? errp : &ignored) = err;
	if (!c) {
		snprintf(err, DRAW_ERR_MAX, "out of memory");
		return TDRAW_NOMEM;
	}
	drain(t);
	rewatch(t);
	for (int i = 0; i < t->nw; i++) {
		char where[4200], *text;
		size_t len;
		int r;

		if (draw_read_conf(t->w[i].dir, "theme", &text, &len, err)) {
			free(c);
			return TDRAW_USAGE;
		}
		if (!text)
			continue;
		snprintf(where, sizeof(where), "%s/theme", t->w[i].dir);
		r = theme_parse(where, text, len, c, &n, err);
		free(text);
		if (r) {
			free(c);
			return TDRAW_USAGE;
		}
	}
	qsort(c, (size_t)n, sizeof(*c), by_name);
	changed = n != t->n || (n && memcmp(c, t->c, (size_t)n * sizeof(*c)));
	free(t->c);
	t->c = c;
	t->n = n;
	return changed;
}

TDRAW_API uint32_t tdraw_theme_color(const struct tdraw_theme *t,
                                     const char *name, uint32_t fallback) {
	struct color key;
	const struct color *found;

	if (!t || !t->n || !name || strlen(name) >= sizeof(key.name))
		return fallback;
	memset(key.name, 0, sizeof(key.name));
	memcpy(key.name, name, strlen(name));
	found = bsearch(&key, t->c, (size_t)t->n, sizeof(*t->c), by_name);
	return found ? found->argb : fallback;
}
