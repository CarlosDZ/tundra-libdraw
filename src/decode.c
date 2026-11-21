#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__JPEG
#include <wuffs-v0.4.c>

#define IMAGE_SIDE_MAX 16384
#define IMAGE_PIXELS_MAX (16u << 20)

static void premultiply(uint32_t *px, size_t n) {
	for (size_t i = 0; i < n; i++) {
		uint32_t p = px[i], a = p >> 24;

		if (a == 255)
			continue;
		px[i] = a << 24 | ((p >> 16 & 0xff) * a + 127) / 255 << 16 |
		        ((p >> 8 & 0xff) * a + 127) / 255 << 8 |
		        ((p & 0xff) * a + 127) / 255;
	}
}

static const char *status(wuffs_base__status st) {
	return st.repr[0] == '#' ? st.repr + 1 : st.repr;
}

const char *draw_decode(const uint8_t *data, size_t len, uint32_t **px, int *w,
                        int *h) {
	wuffs_base__io_buffer src =
	    wuffs_base__ptr_u8__reader((uint8_t *)data, len, true);
	wuffs_base__image_decoder *dec = NULL;
	wuffs_base__image_config ic = {0};
	wuffs_base__pixel_buffer pb = {0};
	wuffs_base__status st;
	void *owner = NULL;
	uint8_t *out = NULL, *work = NULL;
	const char *e = NULL;
	uint32_t iw, ih;
	uint64_t wl;
	size_t size;

	*px = NULL;
	if (len >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8)) {
		wuffs_png__decoder *d = wuffs_png__decoder__alloc();

		owner = d;
		dec = d ? wuffs_png__decoder__upcast_as__wuffs_base__image_decoder(d)
		        : NULL;
	} else if (len >= 3 && data[0] == 0xff && data[1] == 0xd8 &&
	           data[2] == 0xff) {
		wuffs_jpeg__decoder *d = wuffs_jpeg__decoder__alloc();

		owner = d;
		dec = d ? wuffs_jpeg__decoder__upcast_as__wuffs_base__image_decoder(d)
		        : NULL;
	} else {
		return "neither a PNG nor a JPEG";
	}
	if (!dec)
		return "out of memory";
	st = wuffs_base__image_decoder__decode_image_config(dec, &ic, &src);
	if (!wuffs_base__status__is_ok(&st)) {
		e = status(st);
		goto out;
	}
	iw = wuffs_base__pixel_config__width(&ic.pixcfg);
	ih = wuffs_base__pixel_config__height(&ic.pixcfg);
	if (!iw || !ih || iw > IMAGE_SIDE_MAX || ih > IMAGE_SIDE_MAX ||
	    (uint64_t)iw * ih > IMAGE_PIXELS_MAX) {
		e = "too big";
		goto out;
	}
	wuffs_base__pixel_config__set(&ic.pixcfg,
	                              WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL,
	                              WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, iw, ih);
	size = (size_t)iw * ih * 4;
	wl = wuffs_base__image_decoder__workbuf_len(dec).max_incl;
	out = malloc(size);
	work = malloc(wl ? (size_t)wl : 1);
	if (!out || !work) {
		e = "out of memory";
		goto out;
	}
	st = wuffs_base__pixel_buffer__set_from_slice(
	    &pb, &ic.pixcfg, wuffs_base__make_slice_u8(out, size));
	if (wuffs_base__status__is_ok(&st))
		st = wuffs_base__image_decoder__decode_frame(
		    dec, &pb, &src, WUFFS_BASE__PIXEL_BLEND__SRC,
		    wuffs_base__make_slice_u8(work, (size_t)wl), NULL);
	if (!wuffs_base__status__is_ok(&st)) {
		e = status(st);
		goto out;
	}
	*px = (uint32_t *)(void *)out;
	premultiply(*px, (size_t)iw * ih);
	*w = (int)iw;
	*h = (int)ih;
	out = NULL;
out:
	free(out);
	free(work);
	free(owner);
	return e;
}
