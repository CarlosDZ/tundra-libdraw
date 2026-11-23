#include "canvas.c"
#include "decode.c"
#include "image.c"
#include "jpeg-encode.h"
#include "png-encode.h"
#include "shape.c"
#include "test.h"

#define W 24
#define H 24

static uint32_t px[W * H];
static struct tdraw_canvas *cv;
static uint8_t png[8192], jpg[8192];
static size_t png_len, jpg_len;
static const char *err;

static uint32_t at(int x, int y) { return px[y * W + x]; }

static int near(uint32_t got, uint32_t want, int tol) {
	for (int s = 0; s < 32; s += 8) {
		int d = (int)(got >> s & 0xff) - (int)(want >> s & 0xff);

		if (d < -tol || d > tol)
			return 0;
	}
	return 1;
}

static void make_png(void) {
	static const uint8_t rgba[16] = {255, 0, 0,   128, 0,   255, 0,   255,
	                                 0,   0, 255, 0,   255, 255, 255, 64};

	png_len = png_encode(png, 2, 2, rgba);
}

static void make_jpeg(void) {
	static const uint8_t rgb[4][3] = {
	    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {128, 128, 128}};

	jpg_len = jpeg_encode(jpg, 2, 2, rgb);
}

static void test_png(void) {
	struct tdraw_image *im = tdraw_image_decode(png, png_len, &err);
	int w = 0, h = 0;

	CHECK(im != NULL, "a PNG is decoded");
	if (!im) {
		printf("# %s\n", err);
		return;
	}
	tdraw_image_size(im, &w, &h);
	CHECK(w == 2 && h == 2 && near(im->px[0], 0x80800000, 1) &&
	          im->px[1] == 0xff00ff00 && im->px[2] == 0 &&
	          near(im->px[3], 0x40404040, 1),
	      "into premultiplied ARGB");
	memset(px, 0, sizeof(px));
	tdraw_fill(cv, 0, 0, W, H, 0xff0000ff);
	CHECK(tdraw_image(cv, im, 1, 1, 1) == 0 && near(at(1, 1), 0xff80007f, 1) &&
	          at(2, 1) == 0xff00ff00 && at(1, 2) == 0xff0000ff &&
	          near(at(2, 2), 0xff4040ff, 1) && at(0, 0) == 0xff0000ff &&
	          at(3, 3) == 0xff0000ff,
	      "a half transparent PNG over a fill gives the pixels expected");
	memset(px, 0, sizeof(px));
	CHECK(tdraw_image(cv, im, 0, 0, 3) == 0 && at(0, 0) == im->px[0] &&
	          at(2, 2) == im->px[0] && at(3, 0) == 0xff00ff00 &&
	          at(5, 2) == 0xff00ff00 && at(0, 3) == 0 && at(6, 0) == 0,
	      "at scale 3, each pixel is a square of 3");
	memset(px, 0, sizeof(px));
	tdraw_clip(cv, 0, 0, 1, 1);
	tdraw_image(cv, im, 0, 0, 3);
	tdraw_unclip(cv);
	CHECK(at(0, 0) && !at(1, 0) && !at(0, 1), "images keep to the clip");
	CHECK(tdraw_image(cv, im, 0, 0, 0) == TDRAW_USAGE &&
	          tdraw_image(cv, im, 0, 0, 65) == TDRAW_USAGE &&
	          tdraw_image(cv, im, 9000, 0, 1) == TDRAW_USAGE,
	      "a scale out of 1 to 64 or a place past the limit is refused");
	tdraw_image_free(im);
}

static void test_fit(void) {
	static const uint8_t rgba[16] = {0,   0,   0,   255, 255, 255, 255, 255,
	                                 255, 255, 255, 255, 0,   0,   0,   255};
	uint8_t check[256];
	size_t n = png_encode(check, 2, 2, rgba);
	struct tdraw_image *im = tdraw_image_decode(check, n, &err);
	int mixed = 0;

	memset(px, 0, sizeof(px));
	CHECK(im && tdraw_image_fit(cv, im, 0, 0, 8, 8) == 0 &&
	          at(0, 0) == 0xff000000 && at(7, 0) == 0xffffffff &&
	          at(0, 7) == 0xffffffff && at(7, 7) == 0xff000000 && !at(8, 8),
	      "fit stretches an image, its corners as they were");
	for (int x = 0; x < 8; x++)
		mixed += at(x, 3) != 0xff000000 && at(x, 3) != 0xffffffff;
	CHECK(mixed > 0, "and blends in between");
	CHECK(tdraw_image_fit(cv, im, 0, 0, 0, 8) == TDRAW_USAGE,
	      "fitting to no width is refused");
	tdraw_image_free(im);
}

static void test_jpeg(void) {
	struct tdraw_image *im = tdraw_image_decode(jpg, jpg_len, &err);
	int w = 0, h = 0;

	CHECK(im != NULL, "a JPEG is decoded");
	if (!im) {
		printf("# %s\n", err);
		return;
	}
	tdraw_image_size(im, &w, &h);
	CHECK(w == 16 && h == 16 && near(im->px[0], 0xffff0000, 3) &&
	          near(im->px[7 * 16 + 7], 0xffff0000, 3) &&
	          near(im->px[8], 0xff00ff00, 3) &&
	          near(im->px[8 * 16], 0xff0000ff, 3) &&
	          near(im->px[15 * 16 + 15], 0xff808080, 3),
	      "with its four colors, opaque");
	tdraw_image_free(im);
}

static void test_capture(void) {
	struct tdraw_pen soft = {0xc0e08040, 3, 1};
	struct tdraw_image *im;
	void *out = NULL;
	size_t len = 0;
	int w = 0, h = 0, same = 1;

	memset(px, 0, sizeof(px));
	tdraw_fill(cv, 2, 2, 6, 6, 0xff336699);
	tdraw_ellipse(cv, 12, 12, 7, 5, &soft);
	CHECK(tdraw_capture(cv, 0, 0, W, H, &out, &len) == 0 && len > 50,
	      "the canvas is captured as a PNG");
	im = tdraw_image_decode(out, len, &err);
	if (im)
		tdraw_image_size(im, &w, &h);
	for (int i = 0; im && i < W * H; i++)
		same &= im->px[i] == px[i];
	CHECK(im && w == W && h == H && same,
	      "and read back, it is the same, soft edges included");
	tdraw_image_free(im);
	free(out);
	out = NULL;
	CHECK(tdraw_capture(cv, 10, 11, 5, 3, &out, &len) == 0 &&
	          (im = tdraw_image_decode(out, len, &err)) != NULL,
	      "a rectangle of it too");
	same = 1;
	if (im) {
		tdraw_image_size(im, &w, &h);
		for (int j = 0; j < 3; j++)
			for (int i = 0; i < 5; i++)
				same &= im->px[j * 5 + i] == at(10 + i, 11 + j);
	}
	CHECK(w == 5 && h == 3 && same, "with its own pixels");
	tdraw_image_free(im);
	free(out);
	CHECK(tdraw_capture(cv, 20, 0, 5, 1, &out, &len) == TDRAW_USAGE &&
	          tdraw_capture(cv, 0, 0, 0, 1, &out, &len) == TDRAW_USAGE,
	      "a rectangle past the canvas, or empty, is refused");
}

static void test_broken(void) {
	static const char junk[] = "GIF89a, or nothing at all";
	struct tdraw_image *im;
	int refused = 0, decoded = 0;

	err = NULL;
	CHECK(!tdraw_image_decode(junk, sizeof(junk), &err) && err,
	      "what is neither PNG nor JPEG is refused, and says why");
	CHECK(!tdraw_image_decode(png, png_len / 2, &err) &&
	          !tdraw_image_decode(jpg, jpg_len / 2, &err),
	      "a cut PNG or JPEG is refused");
	for (int f = 0; f < 2; f++) {
		uint8_t *src = f ? jpg : png;
		size_t n = f ? jpg_len : png_len;

		for (size_t i = 0; i < n; i++)
			for (int bit = 0; bit < 8; bit++) {
				uint8_t *b = malloc(n);

				memcpy(b, src, n);
				b[i] ^= (uint8_t)(1u << bit);
				im = tdraw_image_decode(b, n, &err);
				if (im) {
					decoded++;
					tdraw_image(cv, im, 0, 0, 1);
				} else {
					refused++;
				}
				tdraw_image_free(im);
				free(b);
			}
	}
	CHECK(refused > 0 && decoded > 0,
	      "a bit flipped anywhere is decoded or refused");
}

#define LINE 8192

static uint32_t line_src[2048], line_dst[LINE];

static int line_ok(int down, int scale, int flip, int x0) {
	int n = 16384 / scale < 2048 ? 16384 / scale : 2048;
	pixman_image_t *src =
	    pixman_image_create_bits(PIXMAN_a8r8g8b8, down ? 1 : n, down ? n : 1,
	                             line_src, down ? 4 : n * 4);
	struct tdraw_canvas *c = tdraw_canvas_new(
	    line_dst, down ? 1 : LINE, down ? LINE : 1, down ? 4 : LINE * 4);
	int ok = src && c;

	memset(line_dst, 0, sizeof(line_dst));
	if (ok)
		ok = draw_scaled(c, src, down ? 1 : n, down ? n : 1, down ? 0 : x0,
		                 down ? x0 : 0, scale, flip) == 0;
	for (int i = 0; ok && i < LINE; i++) {
		int u = i - x0, k = u / scale;
		uint32_t want =
		    u < 0 || u >= n * scale ? 0 : line_src[flip ? n - 1 - k : k];

		if (line_dst[i] != want) {
			printf("# scale %d, flip %d, from %d: %d is %08x, not %08x\n",
			       scale, flip, x0, i, line_dst[i], want);
			ok = 0;
		}
	}
	if (src)
		pixman_image_unref(src);
	tdraw_canvas_free(c);
	return ok;
}

static void test_scales(void) {
	int ok = 1;

	for (int i = 0; i < 2048; i++)
		line_src[i] = 0xff000000u | (uint32_t)i;
	for (int s = 1; s <= 64; s++)
		for (int f = 0; f < 2; f++) {
			int n = 16384 / s < 2048 ? 16384 / s : 2048;

			ok &= line_ok(0, s, f ? TDRAW_HFLIP : 0, 0);
			ok &= line_ok(0, s, f ? TDRAW_HFLIP : 0, LINE - n * s);
			ok &= line_ok(1, s, f ? TDRAW_VFLIP : 0, 0);
			ok &= line_ok(1, s, f ? TDRAW_VFLIP : 0, LINE - n * s);
		}
	CHECK(ok, "each pixel is a square of scale pixels, at every scale and "
	          "flip, at both ends of the coordinates");
}

int main(void) {
	cv = tdraw_canvas_new(px, W, H, W * 4);
	make_png();
	make_jpeg();
	test_png();
	test_fit();
	test_jpeg();
	test_capture();
	test_broken();
	test_scales();
	tdraw_canvas_free(cv);
	return test_summary();
}
