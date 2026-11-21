#include "internal.h"

#include <stdlib.h>
#include <tundra/draw.h>

TDRAW_API struct tdraw_canvas *tdraw_canvas_new(uint32_t *px, int width,
                                                int height, int stride) {
	struct tdraw_canvas *c;

	if (!px || width <= 0 || height <= 0 || width > TDRAW_COORD_MAX ||
	    height > TDRAW_COORD_MAX || stride % 4 || stride / 4 < width)
		return NULL;
	if (!(c = malloc(sizeof(*c))))
		return NULL;
	c->img =
	    pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, px, stride);
	if (!c->img) {
		free(c);
		return NULL;
	}
	c->w = width;
	c->h = height;
	return c;
}

TDRAW_API void tdraw_canvas_free(struct tdraw_canvas *c) {
	if (!c)
		return;
	pixman_image_unref(c->img);
	free(c);
}

static int rect_ok(int x, int y, int w, int h) {
	return w >= 0 && h >= 0 && x >= -TDRAW_COORD_MAX && y >= -TDRAW_COORD_MAX &&
	       x + (double)w <= TDRAW_COORD_MAX && y + (double)h <= TDRAW_COORD_MAX;
}

static pixman_box32_t inside(const struct tdraw_canvas *c, int x, int y, int w,
                             int h) {
	pixman_box32_t b = {x < 0 ? 0 : x, y < 0 ? 0 : y,
	                    x + w > c->w ? c->w : x + w,
	                    y + h > c->h ? c->h : y + h};

	if (b.x2 < b.x1)
		b.x2 = b.x1;
	if (b.y2 < b.y1)
		b.y2 = b.y1;
	return b;
}

TDRAW_API int tdraw_clip(struct tdraw_canvas *c, int x, int y, int w, int h) {
	pixman_region32_t r;
	pixman_box32_t b;
	int ok;

	if (!rect_ok(x, y, w, h))
		return TDRAW_USAGE;
	b = inside(c, x, y, w, h);
	pixman_region32_init_rects(&r, &b, 1);
	ok = pixman_image_set_clip_region32(c->img, &r);
	pixman_region32_fini(&r);
	return ok ? 0 : TDRAW_NOMEM;
}

TDRAW_API void tdraw_unclip(struct tdraw_canvas *c) {
	pixman_image_set_clip_region32(c->img, NULL);
}

TDRAW_API int tdraw_fill(struct tdraw_canvas *c, int x, int y, int w, int h,
                         uint32_t color) {
	pixman_color_t pc = tdraw_color(color);
	pixman_box32_t b;

	if (!rect_ok(x, y, w, h))
		return TDRAW_USAGE;
	b = inside(c, x, y, w, h);
	if (b.x1 == b.x2 || b.y1 == b.y2 || !(color >> 24))
		return 0;
	return pixman_image_fill_boxes(PIXMAN_OP_OVER, c->img, &pc, 1, &b)
	           ? 0
	           : TDRAW_NOMEM;
}
