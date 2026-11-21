#ifndef TDRAW_INTERNAL_H
#define TDRAW_INTERNAL_H

#include <pixman.h>
#include <stddef.h>
#include <stdint.h>

#define TDRAW_API __attribute__((visibility("default")))

#define TDRAW_NOMEM -1
#define TDRAW_USAGE -2

const char *draw_decode(const uint8_t *data, size_t len, uint32_t **px, int *w,
                        int *h);

struct tdraw_canvas {
	pixman_image_t *img;
	int w, h;
};

static inline pixman_color_t tdraw_color(uint32_t argb) {
	uint32_t a = argb >> 24;
	pixman_color_t c = {
	    .red = (uint16_t)(((argb >> 16 & 0xff) * a * 257 + 127) / 255),
	    .green = (uint16_t)(((argb >> 8 & 0xff) * a * 257 + 127) / 255),
	    .blue = (uint16_t)(((argb & 0xff) * a * 257 + 127) / 255),
	    .alpha = (uint16_t)(a * 257),
	};

	return c;
}

#endif
