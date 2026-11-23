#include "internal.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tundra/conf.h>
#include <tundra/draw.h>

#define FACES_MAX 32
#define PAGES (0x110000 >> 8)
#define ADVANCE_MAX (4 * TDRAW_FONT_SIZE_MAX)
#define GLYPH_SIDE_MAX 4096
#define KEY(gid) ((void *)(uintptr_t)((gid) + 1))

enum { UNKNOWN, INK, BLANK };

struct face {
	FT_Face ft;
	int scale;
	FT_Int32 load;
};

struct slot {
	uint32_t gid;
	uint8_t face, state;
	int16_t adv, dx;
};

struct tdraw_font {
	FT_Library lib;
	struct face face[FACES_MAX];
	int nface;
	int height, ascent, cell;
	pixman_glyph_cache_t *cache;
	pixman_glyph_t *run;
	size_t nrun;
	struct slot *page[PAGES];
};

typedef char font_path[TCONF_VALUE_MAX];

static int name_ok(const char *s) {
	size_t n;

	if (!s)
		return 0;
	for (n = 0; s[n]; n++) {
		char c = s[n];

		if (n == TCONF_NAME_MAX - 1 ||
		    !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
			return 0;
	}
	return n > 0;
}

static int fonts_parse(const char *where, const char *text, size_t len,
                       const char *use, font_path *path, int *n, char *err) {
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
		snprintf(err, DRAW_ERR_MAX, "%.200s:%d: a fonts file has no blocks",
		         where, g->blocks[0].line);
		goto out;
	}
	for (int i = 0; i < g->n; i++) {
		const struct tconf_item *it = &g->items[i];

		if (it->value[0] != '/') {
			snprintf(err, DRAW_ERR_MAX, "%.200s:%d: not an absolute path",
			         where, it->line);
			goto out;
		}
		if (strcmp(it->key, use))
			continue;
		if (*n == FACES_MAX) {
			snprintf(err, DRAW_ERR_MAX, "%.200s:%d: more than %d fonts for %s",
			         where, it->line, FACES_MAX, use);
			goto out;
		}
		memcpy(path[(*n)++], it->value, sizeof(font_path));
	}
	r = 0;
out:
	free(g);
	return r;
}

static int fonts_list(const char *sysdir, const char *usrdir, const char *use,
                      font_path *path, int *n, char *err) {
	const char *dir[2] = {usrdir, sysdir};

	for (int d = 0; d < 2 && !*n; d++) {
		char where[4200], *text;
		size_t len;
		int r;

		if (!dir[d])
			continue;
		if (draw_read_conf(dir[d], "fonts", &text, &len, err))
			return -1;
		if (!text)
			continue;
		snprintf(where, sizeof(where), "%s/fonts", dir[d]);
		r = fonts_parse(where, text, len, use, path, n, err);
		free(text);
		if (r)
			return -1;
	}
	if (!*n) {
		snprintf(err, DRAW_ERR_MAX, "no fonts for %s", use);
		return -1;
	}
	return 0;
}

static int bitmap_fit(FT_Face ft, int size) {
	int best = -1;

	for (int i = 0; i < ft->num_fixed_sizes; i++) {
		int h = ft->available_sizes[i].height;

		if (h > 0 && size % h == 0 &&
		    (best < 0 || h > ft->available_sizes[best].height))
			best = i;
	}
	if (best < 0 || FT_Select_Size(ft, best))
		return 0;
	return size / ft->available_sizes[best].height;
}

static int metrics_up(FT_Face ft) {
	return (int)(ft->size->metrics.ascender / 64);
}

static int metrics_down(FT_Face ft) {
	return (int)(-ft->size->metrics.descender / 64);
}

static int vector_line(FT_Face ft, int size) {
	long span = ft->ascender - ft->descender, ppem;

	if (span <= 0)
		return 0;
	for (ppem = size * (long)ft->units_per_EM / span; ppem > 0; ppem--) {
		if (FT_Set_Pixel_Sizes(ft, 0, (FT_UInt)ppem))
			return 0;
		if (metrics_up(ft) + metrics_down(ft) <= size)
			return (int)ppem;
	}
	return 0;
}

static int vector_fit(FT_Face ft, int up, int down, int cell) {
	long em = ft->units_per_EM, ppem = TDRAW_FONT_SIZE_MAX;
	int grid = cell && FT_IS_FIXED_WIDTH(ft);

	if (ft->ascender > 0 && up * em / ft->ascender < ppem)
		ppem = up * em / ft->ascender;
	if (ft->descender < 0 && down * em / -ft->descender < ppem)
		ppem = down * em / -ft->descender;
	if (grid && ft->max_advance_width > 0 &&
	    cell * em / ft->max_advance_width < ppem)
		ppem = cell * em / ft->max_advance_width;
	for (; ppem > 0; ppem--) {
		if (FT_Set_Pixel_Sizes(ft, 0, (FT_UInt)ppem))
			return 0;
		if (metrics_up(ft) <= up && metrics_down(ft) <= down &&
		    (!grid || ft->size->metrics.max_advance / 64 <= cell))
			return (int)ppem;
	}
	return 0;
}

static void add_face(struct tdraw_font *f, const char *path, int size) {
	struct face *fc = &f->face[f->nface];
	FT_Face ft;
	int scale = 0, ok;

	if (FT_New_Face(f->lib, path, 0, &ft))
		return;
	if (!FT_IS_SCALABLE(ft))
		ok = (scale = bitmap_fit(ft, size)) > 0;
	else if (!f->nface)
		ok = vector_line(ft, size) > 0;
	else
		ok = vector_fit(ft, f->ascent, f->height - f->ascent, f->cell) > 0;
	if (!ok) {
		FT_Done_Face(ft);
		return;
	}
	fc->ft = ft;
	fc->scale = scale;
	fc->load = scale
	               ? FT_LOAD_RENDER | FT_LOAD_TARGET_MONO
	               : FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP;
	if (!f->nface) {
		int s = scale ? scale : 1, up = metrics_up(ft) * s,
		    down = metrics_down(ft) * s;

		f->height = size;
		f->ascent = scale ? up : up + (size - up - down) / 2;
		f->cell = FT_IS_FIXED_WIDTH(ft)
		              ? (int)(ft->size->metrics.max_advance / 64) * s
		              : 0;
	}
	f->nface++;
}

static struct tdraw_font *font_open_at(const char *sysdir, const char *usrdir,
                                       const char *use, int size,
                                       const char **errp) {
	static _Thread_local char err[DRAW_ERR_MAX];
	font_path *path = NULL;
	struct tdraw_font *f = NULL;
	int n = 0;

	*errp = err;
	if (!name_ok(use) || size < 1 || size > TDRAW_FONT_SIZE_MAX) {
		snprintf(err, DRAW_ERR_MAX,
		         "a use of [A-Za-z0-9_.-] and a size of 1 to %d pixels",
		         TDRAW_FONT_SIZE_MAX);
		return NULL;
	}
	if (!(path = malloc(FACES_MAX * sizeof(*path))) ||
	    !(f = calloc(1, sizeof(*f))))
		goto nomem;
	if (fonts_list(sysdir, usrdir, use, path, &n, err))
		goto fail;
	if (FT_Init_FreeType(&f->lib)) {
		f->lib = NULL;
		goto nomem;
	}
	if (!(f->cache = pixman_glyph_cache_create()))
		goto nomem;
	for (int i = 0; i < n; i++)
		add_face(f, path[i], size);
	if (!f->nface) {
		snprintf(err, DRAW_ERR_MAX, "no font for %s fits %d pixels", use, size);
		goto fail;
	}
	free(path);
	return f;
nomem:
	snprintf(err, DRAW_ERR_MAX, "out of memory");
fail:
	free(path);
	tdraw_font_free(f);
	return NULL;
}

static int exact_at(FT_Face *ft, int n, int size) {
	for (int i = 0; i < n; i++) {
		if (FT_IS_SCALABLE(ft[i]))
			return 0;
		for (int k = 0; k < ft[i]->num_fixed_sizes; k++) {
			int h = ft[i]->available_sizes[k].height;

			if (h > 0 && size % h == 0)
				return 1;
		}
	}
	return 0;
}

static int font_exact_at(const char *sysdir, const char *usrdir,
                         const char *use, int size, const char **errp) {
	static _Thread_local char err[DRAW_ERR_MAX];
	font_path *path = NULL;
	FT_Library lib = NULL;
	FT_Face ft[FACES_MAX];
	int n = 0, m = 0, r = 0;

	*errp = err;
	if (!name_ok(use) || size < 1 || size > TDRAW_FONT_SIZE_MAX) {
		snprintf(err, DRAW_ERR_MAX,
		         "a use of [A-Za-z0-9_.-] and a size of 1 to %d pixels",
		         TDRAW_FONT_SIZE_MAX);
		return TDRAW_USAGE;
	}
	if (!(path = malloc(FACES_MAX * sizeof(*path))) || FT_Init_FreeType(&lib)) {
		free(path);
		snprintf(err, DRAW_ERR_MAX, "out of memory");
		return TDRAW_NOMEM;
	}
	if (fonts_list(sysdir, usrdir, use, path, &n, err)) {
		r = TDRAW_USAGE;
		goto out;
	}
	for (int i = 0; i < n; i++)
		if (!FT_New_Face(lib, path[i], 0, &ft[m]))
			m++;
	for (int s = size; s > 0 && !r; s--)
		if (exact_at(ft, m, s))
			r = s;
	for (int i = 0; i < m; i++)
		FT_Done_Face(ft[i]);
out:
	FT_Done_FreeType(lib);
	free(path);
	return r;
}

TDRAW_API int tdraw_font_exact(const char *use, int size, const char **err) {
	char usr[4096];
	const char *ignored;

	return font_exact_at(TDRAW_CONF_DIR,
	                     draw_user_dir(usr, sizeof(usr)) ? NULL : usr, use,
	                     size, err ? err : &ignored);
}

TDRAW_API struct tdraw_font *tdraw_font_open(const char *use, int size,
                                             const char **err) {
	char usr[4096];
	const char *ignored;

	return font_open_at(TDRAW_CONF_DIR,
	                    draw_user_dir(usr, sizeof(usr)) ? NULL : usr, use, size,
	                    err ? err : &ignored);
}

TDRAW_API void tdraw_font_free(struct tdraw_font *f) {
	if (!f)
		return;
	for (int i = 0; i < PAGES; i++)
		free(f->page[i]);
	if (f->cache)
		pixman_glyph_cache_destroy(f->cache);
	for (int i = 0; i < f->nface; i++)
		FT_Done_Face(f->face[i].ft);
	if (f->lib)
		FT_Done_FreeType(f->lib);
	free(f->run);
	free(f);
}

TDRAW_API void tdraw_font_metrics(const struct tdraw_font *f,
                                  struct tdraw_font_metrics *m) {
	m->height = f->height;
	m->ascent = f->ascent;
	m->cell = f->cell;
}

static int clamp(long v, int lo, int hi) {
	return v < lo ? lo : v > hi ? hi : (int)v;
}

static const void *load(struct tdraw_font *f, struct slot *s, int first) {
	struct face *fc = &f->face[s->face];
	int scale = fc->scale ? fc->scale : 1, w, h, stride, mono, ink = 0;
	FT_GlyphSlot g;
	FT_Bitmap *b;
	const uint8_t *top;
	pixman_image_t *img;
	const void *glyph;
	uint8_t *buf;

	if (first) {
		s->state = BLANK;
		s->adv = (int16_t)f->cell;
		s->dx = 0;
	}
	if (FT_Load_Glyph(fc->ft, s->gid, fc->load))
		return NULL;
	g = fc->ft->glyph;
	b = &g->bitmap;
	if (first) {
		int adv = clamp((g->advance.x + 32) / 64 * scale, 0, ADVANCE_MAX);

		if (!f->cell)
			s->adv = (int16_t)adv;
		else if (s->face)
			s->dx =
			    (int16_t)clamp((f->cell - adv) / 2, -ADVANCE_MAX, ADVANCE_MAX);
	}
	mono = b->pixel_mode == FT_PIXEL_MODE_MONO;
	if ((!mono && b->pixel_mode != FT_PIXEL_MODE_GRAY) || !b->width ||
	    !b->rows || b->width * scale > GLYPH_SIDE_MAX ||
	    b->rows * scale > GLYPH_SIDE_MAX)
		return NULL;
	top = b->pitch < 0 ? b->buffer - (ptrdiff_t)(b->rows - 1) * b->pitch
	                   : b->buffer;
	for (unsigned y = 0; y < b->rows && !ink; y++) {
		const uint8_t *row = top + (ptrdiff_t)y * b->pitch;

		for (unsigned i = 0; i < (mono ? (b->width + 7) / 8 : b->width); i++)
			ink |= row[i];
	}
	if (!ink)
		return NULL;
	if (first)
		s->state = INK;
	w = (int)b->width * scale;
	h = (int)b->rows * scale;
	stride = (w + 3) & ~3;
	if (!(buf = calloc((size_t)stride, (size_t)h)))
		return NULL;
	for (unsigned y = 0; y < b->rows; y++) {
		const uint8_t *row = top + (ptrdiff_t)y * b->pitch;
		uint8_t *dst = buf + (size_t)y * scale * stride;

		for (unsigned x = 0; x < b->width; x++) {
			unsigned v =
			    mono ? (row[x >> 3] >> (7 - (x & 7)) & 1) * 255 : row[x];

			if (!mono && b->num_grays > 1 && b->num_grays != 256)
				v = v * 255 / (b->num_grays - 1u);
			memset(dst + x * scale, (int)v, (size_t)scale);
		}
		for (int k = 1; k < scale; k++)
			memcpy(dst + (size_t)k * stride, dst, (size_t)w);
	}
	img = pixman_image_create_bits(PIXMAN_a8, w, h, (uint32_t *)(void *)buf,
	                               stride);
	glyph = img ? pixman_glyph_cache_insert(f->cache, fc, KEY(s->gid),
	                                        -g->bitmap_left * scale,
	                                        g->bitmap_top * scale, img)
	            : NULL;
	if (img)
		pixman_image_unref(img);
	free(buf);
	return glyph;
}

static struct slot *slot_of(struct tdraw_font *f, uint32_t cp) {
	struct slot **page = &f->page[cp >> 8], *s;

	if (!*page && !(*page = calloc(256, sizeof(**page))))
		return NULL;
	s = &(*page)[cp & 0xff];
	if (s->state == UNKNOWN) {
		s->face = 0;
		s->gid = 0;
		for (int i = 0; i < f->nface; i++) {
			FT_UInt gid = FT_Get_Char_Index(f->face[i].ft, cp);

			if (gid) {
				s->face = (uint8_t)i;
				s->gid = gid;
				break;
			}
		}
		load(f, s, 1);
	}
	return s;
}

static uint32_t utf8_next(const uint8_t *u, size_t len, size_t *i) {
	unsigned c = u[(*i)++], lo = 0x80, hi = 0xbf, n;
	uint32_t cp;

	if (c < 0x80)
		return c;
	if (c >= 0xc2 && c <= 0xdf) {
		n = 1;
		cp = c & 0x1f;
	} else if (c >= 0xe0 && c <= 0xef) {
		n = 2;
		cp = c & 0x0f;
		lo = c == 0xe0 ? 0xa0 : lo;
		hi = c == 0xed ? 0x9f : hi;
	} else if (c >= 0xf0 && c <= 0xf4) {
		n = 3;
		cp = c & 0x07;
		lo = c == 0xf0 ? 0x90 : lo;
		hi = c == 0xf4 ? 0x8f : hi;
	} else {
		return 0xfffd;
	}
	while (n--) {
		if (*i >= len || u[*i] < lo || u[*i] > hi)
			return 0xfffd;
		cp = cp << 6 | (u[(*i)++] & 0x3f);
		lo = 0x80;
		hi = 0xbf;
	}
	return cp;
}

static int coord_ok(int v) {
	return v >= -TDRAW_COORD_MAX && v <= TDRAW_COORD_MAX;
}

TDRAW_API int tdraw_text(struct tdraw_canvas *c, struct tdraw_font *f, int x,
                         int y, const char *utf8, size_t len, uint32_t color) {
	const uint8_t *u = (const uint8_t *)utf8;
	pixman_color_t pc = tdraw_color(color);
	pixman_image_t *src;
	size_t i = 0, n = 0;
	int pen = x, base, r = 0;

	if (!c || !f || (!utf8 && len) || len > TDRAW_TEXT_MAX || !coord_ok(x) ||
	    !coord_ok(y))
		return TDRAW_USAGE;
	if (!len || !(color >> 24))
		return 0;
	if (f->nrun < len) {
		pixman_glyph_t *run = realloc(f->run, len * sizeof(*run));

		if (!run)
			return TDRAW_NOMEM;
		f->run = run;
		f->nrun = len;
	}
	if (!(src = pixman_image_create_solid_fill(&pc)))
		return TDRAW_NOMEM;
	base = y + f->ascent;
	pixman_glyph_cache_freeze(f->cache);
	while (i < len) {
		struct slot *s = slot_of(f, utf8_next(u, len, &i));
		const void *g;

		if (!s) {
			r = TDRAW_NOMEM;
			break;
		}
		if (s->state == INK) {
			g = pixman_glyph_cache_lookup(f->cache, &f->face[s->face],
			                              KEY(s->gid));
			if (g || (g = load(f, s, 0)))
				f->run[n++] = (pixman_glyph_t){pen + s->dx, base, g};
			else
				r = TDRAW_NOMEM;
		}
		pen += s->adv;
	}
	if (n)
		pixman_composite_glyphs_no_mask(PIXMAN_OP_OVER, src, c->img, 0, 0, 0, 0,
		                                f->cache, (int)n, f->run);
	pixman_glyph_cache_thaw(f->cache);
	pixman_image_unref(src);
	return r;
}

TDRAW_API int tdraw_text_width(struct tdraw_font *f, const char *utf8,
                               size_t len) {
	const uint8_t *u = (const uint8_t *)utf8;
	size_t i = 0;
	long w = 0;
	int r = 0;

	if (!f || (!utf8 && len) || len > TDRAW_TEXT_MAX)
		return TDRAW_USAGE;
	if (f->cell) {
		while (i < len) {
			utf8_next(u, len, &i);
			w += f->cell;
		}
		return (int)w;
	}
	pixman_glyph_cache_freeze(f->cache);
	while (i < len) {
		struct slot *s = slot_of(f, utf8_next(u, len, &i));

		if (!s) {
			r = TDRAW_NOMEM;
			break;
		}
		w += s->adv;
	}
	pixman_glyph_cache_thaw(f->cache);
	return r ? r : (int)w;
}
