#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tundra/draw.h>
#include <tundra/toa.h>

#define SCALE_MAX 64
#define FROM_BASE UINT32_MAX

struct box {
	int x1, y1, x2, y2;
};

struct tdraw_art {
	struct ttoa t;
	pixman_image_t *img;
	uint8_t *idx;
	uint32_t *px;
	uint16_t *map;
	uint64_t *dirty;
	uint32_t *chg;
	uint32_t base[256];
	uint32_t from[256];
	uint32_t shown[256];
	const struct ttoa_anim *anim;
	int frame;
	int still;
	uint64_t next;
	uint64_t lap;
};

static uint32_t premultiply(uint32_t argb) {
	uint32_t a = argb >> 24;

	return a << 24 | ((argb >> 16 & 0xff) * a + 127) / 255 << 16 |
	       ((argb >> 8 & 0xff) * a + 127) / 255 << 8 |
	       ((argb & 0xff) * a + 127) / 255;
}

static uint32_t resolve(const struct ttoa *t, const struct ttoa_color *c,
                        const struct tdraw_theme *theme) {
	uint32_t argb =
	    0xffu << 24 | (uint32_t)c->r << 16 | (uint32_t)c->g << 8 | c->b;
	char name[12];

	if (c->kind == TTOA_TERMINAL) {
		snprintf(name, sizeof(name), "color%u", (unsigned)c->ref);
		argb = tdraw_theme_color(theme, name, argb);
	} else if (c->kind == TTOA_NAMED) {
		argb = tdraw_theme_color(theme, t->names + c->ref, argb);
	}
	argb = ((argb >> 24) * c->alpha + 127) / 255 << 24 | (argb & 0xffffff);
	return premultiply(argb);
}

static void resolve_all(struct tdraw_art *a, const struct tdraw_theme *theme) {
	for (int e = 0; e < 1 << a->t.bpp; e++)
		a->base[e] = resolve(&a->t, &a->t.palette[e], theme);
	for (uint32_t k = 0; k < a->t.npalchg; k++)
		a->chg[k] = resolve(&a->t, &a->t.palchg[k].color, theme);
}

static uint32_t color_of(const struct tdraw_art *a, int e) {
	return a->from[e] == FROM_BASE ? a->base[e] : a->chg[a->from[e]];
}

static int cell_box(const struct ttoa *t, int i, struct box *b) {
	b->x1 = i % t->cols * 8;
	b->y1 = i / t->cols * 8;
	b->x2 = b->x1 + 8 < t->width ? b->x1 + 8 : t->width;
	b->y2 = b->y1 + 8 < t->height ? b->y1 + 8 : t->height;
	return i;
}

static void grow(struct box *b, int x1, int y1, int x2, int y2) {
	if (b->x1 >= b->x2) {
		*b = (struct box){x1, y1, x2, y2};
		return;
	}
	b->x1 = x1 < b->x1 ? x1 : b->x1;
	b->y1 = y1 < b->y1 ? y1 : b->y1;
	b->x2 = x2 > b->x2 ? x2 : b->x2;
	b->y2 = y2 > b->y2 ? y2 : b->y2;
}

static void set_cell(struct tdraw_art *a, int i, unsigned v) {
	const struct ttoa *t = &a->t;
	unsigned tile = v & TTOA_CELL_TILE;
	const uint8_t *bits = tile ? ttoa_tile(t, (int)tile) : NULL;
	int hx = v & TTOA_CELL_HFLIP ? 7 : 0, vy = v & TTOA_CELL_VFLIP ? 7 : 0;
	int changed = 0;
	struct box b;

	cell_box(t, i, &b);
	for (int y = b.y1; y < b.y2; y++) {
		uint8_t *row = a->idx + (size_t)y * (size_t)t->width;

		for (int x = b.x1; x < b.x2; x++) {
			uint8_t e = bits
			                ? (uint8_t)ttoa_pixel(bits, t->bpp, (x - b.x1) ^ hx,
			                                      (y - b.y1) ^ vy)
			                : 0;

			changed |= row[x] != e;
			row[x] = e;
		}
	}
	a->map[i] = (uint16_t)v;
	if (changed)
		a->dirty[i / 64] |= 1ull << (i % 64);
}

static void flush(struct tdraw_art *a, struct box *b) {
	const struct ttoa *t = &a->t;
	int ncells = t->cols * t->rows, any = 0;
	uint8_t recolor[256];

	*b = (struct box){0, 0, 0, 0};
	for (int e = 0; e < 1 << t->bpp; e++) {
		uint32_t c = color_of(a, e);

		recolor[e] = c != a->shown[e];
		any |= recolor[e];
		a->shown[e] = c;
	}
	for (int y = 0; any && y < t->height; y++) {
		const uint8_t *row = a->idx + (size_t)y * (size_t)t->width;
		uint32_t *out = a->px + (size_t)y * (size_t)t->width;
		int x1 = -1, x2 = 0;

		for (int x = 0; x < t->width; x++)
			if (recolor[row[x]]) {
				out[x] = a->shown[row[x]];
				x1 = x1 < 0 ? x : x1;
				x2 = x + 1;
			}
		if (x1 >= 0)
			grow(b, x1, y, x2, y + 1);
	}
	for (int w = 0; w < (ncells + 63) / 64; w++)
		for (uint64_t bits = a->dirty[w]; bits; bits &= bits - 1) {
			struct box c;
			int changed = 0;

			cell_box(t, w * 64 + __builtin_ctzll(bits), &c);
			for (int y = c.y1; y < c.y2; y++)
				for (int x = c.x1; x < c.x2; x++) {
					size_t p = (size_t)y * (size_t)t->width + (size_t)x;
					uint32_t v = a->shown[a->idx[p]];

					changed |= a->px[p] != v;
					a->px[p] = v;
				}
			if (changed)
				grow(b, c.x1, c.y1, c.x2, c.y2);
			a->dirty[w] = 0;
		}
}

static void apply(struct tdraw_art *a, int f) {
	const struct ttoa *t = &a->t;
	const struct ttoa_frame *fr = &t->frames[f];

	for (uint32_t k = fr->pal_first; k < fr->pal_first + fr->npal; k++)
		a->from[t->palchg[k].entry] = k;
	for (uint32_t k = fr->cell_first; k < fr->cell_first + fr->ncell; k++)
		if (a->map[t->cellchg[k].cell] != t->cellchg[k].value)
			set_cell(a, t->cellchg[k].cell, t->cellchg[k].value);
}

static void to_base(struct tdraw_art *a) {
	for (int e = 0; e < 256; e++)
		a->from[e] = FROM_BASE;
	for (int i = 0; i < a->t.cols * a->t.rows; i++)
		if (a->map[i] != a->t.map[i])
			set_cell(a, i, a->t.map[i]);
}

static void settle(struct tdraw_art *a) {
	int n = a->anim->nframes;

	a->still = a->frame == n - 1 && (!(a->anim->flags & TTOA_LOOP) || n == 1);
}

static int duration(const struct tdraw_art *a, int k) {
	return a->t.frames[a->anim->first + k].duration;
}

TDRAW_API struct tdraw_art *tdraw_art_open(const void *toa, size_t len,
                                           const struct tdraw_theme *theme,
                                           const char **err) {
	static _Thread_local char msg[TTOA_ERR_MAX];
	struct tdraw_art *a = calloc(1, sizeof(*a));
	const char *e = "out of memory";
	size_t npx, ncells;
	struct box b;

	if (!a)
		goto fail;
	if (ttoa_open(toa, len, &a->t) < 0) {
		snprintf(msg, sizeof(msg), "%s", a->t.err);
		e = msg;
		goto fail;
	}
	npx = (size_t)a->t.width * (size_t)a->t.height;
	ncells = (size_t)a->t.cols * (size_t)a->t.rows;
	a->idx = calloc(npx, 1);
	a->px = calloc(npx, 4);
	a->map = calloc(ncells, 2);
	a->dirty = calloc((ncells + 63) / 64, 8);
	a->chg = malloc((a->t.npalchg ? a->t.npalchg : 1) * sizeof(uint32_t));
	if (!a->idx || !a->px || !a->map || !a->dirty || !a->chg)
		goto fail;
	a->img = pixman_image_create_bits(PIXMAN_a8r8g8b8, a->t.width, a->t.height,
	                                  a->px, a->t.width * 4);
	if (!a->img)
		goto fail;
	resolve_all(a, theme);
	memcpy(a->shown, a->base, sizeof(a->base));
	to_base(a);
	flush(a, &b);
	a->still = 1;
	return a;
fail:
	tdraw_art_free(a);
	if (err)
		*err = e;
	return NULL;
}

TDRAW_API void tdraw_art_free(struct tdraw_art *a) {
	if (!a)
		return;
	if (a->img)
		pixman_image_unref(a->img);
	free(a->idx);
	free(a->px);
	free(a->map);
	free(a->dirty);
	free(a->chg);
	free(a);
}

TDRAW_API void tdraw_art_size(const struct tdraw_art *a, int *w, int *h) {
	*w = a->t.width;
	*h = a->t.height;
}

TDRAW_API int tdraw_art_theme(struct tdraw_art *a,
                              const struct tdraw_theme *theme) {
	struct box b;

	resolve_all(a, theme);
	flush(a, &b);
	return b.x1 < b.x2;
}

TDRAW_API int tdraw_art_play(struct tdraw_art *a, const char *anim,
                             uint64_t now_ms) {
	const struct ttoa_anim *an = NULL;

	for (int i = 0; anim && !an; i++) {
		if (i == a->t.nanims)
			return TDRAW_USAGE;
		if (!strcmp(a->t.names + a->t.anims[i].name, anim))
			an = &a->t.anims[i];
	}
	a->anim = an;
	a->frame = -1;
	a->still = 0;
	a->next = now_ms;
	a->lap = 0;
	for (int k = 0; an && k < an->nframes; k++)
		a->lap += (uint64_t)duration(a, k);
	return 0;
}

static int wait_for(uint64_t ms) { return ms > INT_MAX ? INT_MAX : (int)ms; }

TDRAW_API int tdraw_art_advance(struct tdraw_art *a, uint64_t now_ms,
                                struct tdraw_rect *changed) {
	struct box b = {0, 0, 0, 0};

	if (!a->still && now_ms >= a->next && a->frame < 0) {
		to_base(a);
		if (a->anim) {
			apply(a, a->anim->first);
			a->frame = 0;
			a->next += (uint64_t)duration(a, 0);
			settle(a);
		} else {
			a->still = 1;
		}
	}
	while (!a->still && now_ms >= a->next && a->frame >= 0) {
		int n = a->anim->nframes;

		if (a->anim->flags & TTOA_LOOP && now_ms - a->next >= a->lap)
			a->next += (now_ms - a->next) / a->lap * a->lap;
		if (a->frame == n - 1) {
			apply(a, a->anim->first + n);
			a->frame = 0;
		} else {
			apply(a, a->anim->first + ++a->frame);
		}
		a->next += (uint64_t)duration(a, a->frame);
		settle(a);
	}
	flush(a, &b);
	if (changed)
		*changed = (struct tdraw_rect){b.x1, b.y1, b.x2 - b.x1, b.y2 - b.y1};
	return a->still ? -1 : wait_for(a->next - now_ms);
}

static int span_ok(int x, int w) {
	return x >= -TDRAW_COORD_MAX && (double)x + w <= TDRAW_COORD_MAX;
}

TDRAW_API int tdraw_art(struct tdraw_canvas *c, const struct tdraw_art *a,
                        int x, int y, int scale, int flip) {
	int w = a->t.width, h = a->t.height;

	if (scale < 1 || scale > SCALE_MAX || flip & ~(TDRAW_HFLIP | TDRAW_VFLIP) ||
	    !span_ok(x, w * scale) || !span_ok(y, h * scale))
		return TDRAW_USAGE;
	return draw_scaled(c, a->img, w, h, x, y, scale, flip);
}
