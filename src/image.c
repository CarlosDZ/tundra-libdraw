#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <tundra/draw.h>
#include <zlib.h>

#define SCALE_MAX 64

struct tdraw_image {
	uint32_t *px;
	int w, h;
};

TDRAW_API struct tdraw_image *tdraw_image_decode(const void *data, size_t len,
                                                 const char **err) {
	struct tdraw_image *im = malloc(sizeof(*im));
	const char *e;

	if (!im) {
		*err = "out of memory";
		return NULL;
	}
	if ((e = draw_decode(data, len, &im->px, &im->w, &im->h))) {
		free(im);
		*err = e;
		return NULL;
	}
	return im;
}

TDRAW_API void tdraw_image_size(const struct tdraw_image *im, int *w, int *h) {
	*w = im->w;
	*h = im->h;
}

TDRAW_API void tdraw_image_free(struct tdraw_image *im) {
	if (!im)
		return;
	free(im->px);
	free(im);
}

static pixman_image_t *source(const struct tdraw_image *im) {
	return pixman_image_create_bits(PIXMAN_a8r8g8b8, im->w, im->h, im->px,
	                                im->w * 4);
}

static int span_ok(int x, int w) {
	return x >= -TDRAW_COORD_MAX && (double)x + w <= TDRAW_COORD_MAX;
}

TDRAW_API int tdraw_image(struct tdraw_canvas *c, const struct tdraw_image *im,
                          int x, int y, int scale) {
	pixman_image_t *src;
	pixman_transform_t t;

	if (scale < 1 || scale > SCALE_MAX || !span_ok(x, im->w * scale) ||
	    !span_ok(y, im->h * scale))
		return TDRAW_USAGE;
	if (!(src = source(im)))
		return TDRAW_NOMEM;
	if (scale > 1) {
		pixman_transform_init_scale(&t, pixman_double_to_fixed(1.0 / scale),
		                            pixman_double_to_fixed(1.0 / scale));
		pixman_image_set_transform(src, &t);
		pixman_image_set_filter(src, PIXMAN_FILTER_NEAREST, NULL, 0);
	}
	pixman_image_composite32(PIXMAN_OP_OVER, src, NULL, c->img, 0, 0, 0, 0, x,
	                         y, im->w * scale, im->h * scale);
	pixman_image_unref(src);
	return 0;
}

TDRAW_API int tdraw_image_fit(struct tdraw_canvas *c,
                              const struct tdraw_image *im, int x, int y, int w,
                              int h) {
	pixman_image_t *src;
	pixman_transform_t t;

	if (w < 1 || h < 1 || !span_ok(x, w) || !span_ok(y, h))
		return TDRAW_USAGE;
	if (!(src = source(im)))
		return TDRAW_NOMEM;
	pixman_transform_init_scale(&t, pixman_double_to_fixed((double)im->w / w),
	                            pixman_double_to_fixed((double)im->h / h));
	pixman_image_set_transform(src, &t);
	pixman_image_set_filter(src, PIXMAN_FILTER_BILINEAR, NULL, 0);
	pixman_image_set_repeat(src, PIXMAN_REPEAT_PAD);
	pixman_image_composite32(PIXMAN_OP_OVER, src, NULL, c->img, 0, 0, 0, 0, x,
	                         y, w, h);
	pixman_image_unref(src);
	return 0;
}

static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static size_t chunk_end(uint8_t *p, uint32_t n) {
	put_be32(p, n);
	put_be32(p + 8 + n, (uint32_t)crc32(crc32(0, NULL, 0), p + 4, n + 4));
	return n + 12;
}

TDRAW_API int tdraw_capture(struct tdraw_canvas *c, int x, int y, int w, int h,
                            void **png, size_t *len) {
	const uint32_t *src = pixman_image_get_data(c->img);
	size_t stride = (size_t)pixman_image_get_stride(c->img) / 4;
	size_t row = 1 + (size_t)w * 4, raw_len = row * (size_t)h, n = 0;
	uLongf zlen;
	uint8_t *raw, *out;

	if (w < 1 || h < 1 || x < 0 || y < 0 || x > c->w - w || y > c->h - h)
		return TDRAW_USAGE;
	zlen = compressBound((uLong)raw_len);
	raw = malloc(raw_len);
	out = malloc(8 + 25 + 12 + zlen + 12);
	if (!raw || !out) {
		free(raw);
		free(out);
		return TDRAW_NOMEM;
	}
	for (int j = 0; j < h; j++) {
		uint8_t *r = raw + row * (size_t)j;

		*r++ = 0;
		for (int i = 0; i < w; i++) {
			uint32_t p = src[(size_t)(y + j) * stride + (size_t)(x + i)];
			uint32_t a = p >> 24;

			for (int s = 16; s >= 0; s -= 8)
				*r++ = a ? (uint8_t)(((p >> s & 0xff) * 255 + a / 2) / a) : 0;
			*r++ = (uint8_t)a;
		}
	}
	memcpy(out, "\x89PNG\r\n\x1a\n", 8);
	n = 8;
	memcpy(out + n + 4, "IHDR", 4);
	put_be32(out + n + 8, (uint32_t)w);
	put_be32(out + n + 12, (uint32_t)h);
	memcpy(out + n + 16, "\x08\x06\x00\x00\x00", 5);
	n += chunk_end(out + n, 13);
	memcpy(out + n + 4, "IDAT", 4);
	if (compress2(out + n + 8, &zlen, raw, (uLong)raw_len, Z_BEST_SPEED) !=
	    Z_OK) {
		free(raw);
		free(out);
		return TDRAW_NOMEM;
	}
	n += chunk_end(out + n, (uint32_t)zlen);
	memcpy(out + n + 4, "IEND", 4);
	n += chunk_end(out + n, 0);
	free(raw);
	*png = out;
	*len = n;
	return 0;
}
