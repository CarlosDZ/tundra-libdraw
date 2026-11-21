#include "canvas.c"
#include "shape.c"
#include "test.h"

#define W 24
#define H 24

static uint32_t px[W * H];
static struct tdraw_canvas *cv;

static void clear(void) { memset(px, 0, sizeof(px)); }

static uint32_t at(int x, int y) { return px[y * W + x]; }

static int alpha(int x, int y) { return (int)(at(x, y) >> 24); }

static int near(uint32_t got, uint32_t want) {
	for (int s = 0; s < 32; s += 8) {
		int d = (int)(got >> s & 0xff) - (int)(want >> s & 0xff);

		if (d < -1 || d > 1)
			return 0;
	}
	return 1;
}

static int crisp(void) {
	for (int i = 0; i < W * H; i++)
		if (px[i] >> 24 != 0 && px[i] >> 24 != 255)
			return 0;
	return 1;
}

static int painted(void) {
	int n = 0;

	for (int i = 0; i < W * H; i++)
		n += px[i] != 0;
	return n;
}

static void test_canvas(void) {
	static uint32_t small[4];

	CHECK(!tdraw_canvas_new(NULL, 2, 2, 8) &&
	          !tdraw_canvas_new(small, 2, 2, 4) &&
	          !tdraw_canvas_new(small, 0, 2, 8) &&
	          !tdraw_canvas_new(small, 9000, 1, 36000),
	      "a canvas without memory, width or room in its rows is refused");
	cv = tdraw_canvas_new(px, W, H, W * 4);
	CHECK(cv != NULL, "a canvas over a buffer of the caller");
}

static void test_fill(void) {
	clear();
	CHECK(tdraw_fill(cv, 2, 3, 4, 5, 0xffff0000) == 0 &&
	          at(2, 3) == 0xffff0000 && at(5, 7) == 0xffff0000 &&
	          at(1, 3) == 0 && at(6, 3) == 0 && at(2, 8) == 0 &&
	          painted() == 20,
	      "fill paints its rectangle, exactly");
	tdraw_fill(cv, 0, 0, W, H, 0xff0000ff);
	tdraw_fill(cv, 0, 0, 1, 1, 0x80ff0000);
	CHECK(near(at(0, 0), 0xff80007f), "a half transparent red over blue");
	clear();
	tdraw_fill(cv, 0, 0, 1, 1, 0x80ff0000);
	CHECK(near(at(0, 0), 0x80800000), "and over nothing, premultiplied");
	clear();
	CHECK(tdraw_fill(cv, -5, -5, 100, 100, 0xff00ff00) == 0 &&
	          painted() == W * H,
	      "a rectangle past the edges is cut to the canvas");
	clear();
	CHECK(tdraw_clip(cv, 3, 3, 2, 2) == 0 &&
	          tdraw_fill(cv, 0, 0, W, H, 0xffffffff) == 0 && painted() == 4 &&
	          at(3, 3) && at(4, 4) && !at(5, 5),
	      "a clip keeps the paint inside it");
	tdraw_unclip(cv);
	tdraw_fill(cv, 0, 0, W, H, 0xffffffff);
	CHECK(painted() == W * H, "and unclip lifts it");
	clear();
	tdraw_clip(cv, -5, -5, 8, 8);
	tdraw_fill(cv, -9, -9, 40, 40, 0xffffffff);
	CHECK(painted() == 9, "a clip past the edges is cut to the canvas too");
	tdraw_unclip(cv);
	CHECK(tdraw_fill(cv, 0, 0, -1, 1, 0xffffffff) == TDRAW_USAGE &&
	          tdraw_fill(cv, 9000, 0, 1, 1, 0xffffffff) == TDRAW_USAGE,
	      "a negative size or a coordinate past the limit is refused");
}

static void test_shapes(void) {
	static uint32_t ref[W * H];
	struct tdraw_pen fill = {0xffffffff, 0, 0}, line = {0xffffffff, 1, 0};
	struct tdraw_pen soft = {0xffffffff, 0, 1};

	clear();
	tdraw_fill(cv, 2, 3, 7, 5, 0xffffffff);
	memcpy(ref, px, sizeof(px));
	clear();
	CHECK(tdraw_rect(cv, 2, 3, 7, 5, 0, &fill) == 0 &&
	          !memcmp(px, ref, sizeof(px)),
	      "a filled rectangle on whole pixels is the same as fill");
	clear();
	CHECK(tdraw_rect(cv, 0.5f, 0.5f, 9, 9, 0, &line) == 0 && painted() == 36 &&
	          at(0, 0) && at(9, 9) && at(9, 0) && !at(1, 1) && !at(8, 8) &&
	          !at(10, 10) && crisp(),
	      "a stroked rectangle on pixel centers: a border one pixel wide");
	clear();
	CHECK(tdraw_line(cv, 0.5f, 2.5f, 9.5f, 2.5f, &line) == 0 &&
	          painted() == 10 && at(0, 2) && at(9, 2) && !at(0, 1) && !at(0, 3),
	      "a line covers both of its ends");
	clear();
	line.smooth = 1;
	tdraw_line(cv, 0.5f, 2.5f, 9.5f, 2.5f, &line);
	CHECK(painted() == 10 && crisp(),
	      "smoothed, a line on pixel centers stays crisp");
	clear();
	tdraw_line(cv, 0, 0, 20, 13, &line);
	CHECK(!crisp(), "smoothed, a diagonal line has pixels in between");
	clear();
	line.smooth = 0;
	tdraw_line(cv, 0, 0, 20, 13, &line);
	CHECK(painted() > 15 && crisp(), "unsmoothed, it does not");

	clear();
	CHECK(tdraw_ellipse(cv, 10, 10, 6, 6, &fill) == 0 && crisp() &&
	          at(10, 10) && !at(3, 3) && !at(16, 16),
	      "a filled circle, its corners empty");
	int sym = 1;
	for (int y = 4; y < 16; y++)
		for (int x = 4; x < 16; x++)
			sym &= !at(x, y) == !at(19 - x, y) && !at(x, y) == !at(x, 19 - y);
	CHECK(sym, "and symmetric");
	clear();
	tdraw_ellipse(cv, 10, 10, 6, 6, &soft);
	CHECK(!crisp() && alpha(10, 10) == 255, "smoothed, its edge is soft");
	clear();
	line.width = 2;
	tdraw_ellipse(cv, 10, 10, 6, 6, &line);
	CHECK(!at(10, 10) && at(10, 4) && at(16, 10), "a stroked circle is a ring");

	clear();
	CHECK(tdraw_arc(cv, 10, 10, 8, 8, 0, 90, &fill) == 0 && at(14, 14) &&
	          !at(5, 5) && !at(14, 5) && !at(5, 14),
	      "a filled arc from 0 to 90 is the slice down on the right");
	clear();
	tdraw_arc(cv, 10, 10, 8, 8, 0, 360, &fill);
	CHECK(at(5, 5) && at(14, 14), "an arc of 360 is the whole ellipse");
	clear();
	line.width = 1;
	tdraw_arc(cv, 10, 10, 8, 8, 180, 360, &line);
	CHECK(at(10, 2) && !at(10, 18) && !at(10, 10),
	      "a stroked arc from 180 to 360 is the upper half of a ring");

	clear();
	tdraw_rect(cv, 0, 0, 20, 20, 10, &fill);
	CHECK(!at(0, 0) && !at(19, 19) && at(10, 10) && at(10, 0),
	      "a rectangle rounded by half its side is a circle");

	static const float star[10] = {10, 1, 15, 19, 1, 7, 19, 7, 5, 19};
	clear();
	CHECK(tdraw_polygon(cv, star, 5, &fill) == 0 && at(10, 10),
	      "a star filled by the nonzero rule has its middle painted");
	static const float vee[6] = {2, 2, 10, 20, 18, 2};
	clear();
	line.width = 3;
	CHECK(tdraw_polyline(cv, vee, 3, &line) == 0 && at(10, 19) && !at(10, 10),
	      "a sharp polyline is stroked, open at its top");

	clear();
	fill.color = 0x00ffffff;
	tdraw_ellipse(cv, 10, 10, 6, 6, &fill);
	CHECK(painted() == 0, "a clear color paints nothing");
}

static void test_usage(void) {
	struct tdraw_pen fill = {0xffffffff, 0, 0}, nan = {0xffffffff, NAN, 0};
	static const float two[4] = {0, 0, 1, 1};

	CHECK(tdraw_line(cv, 0, 0, 5, 5, &fill) == TDRAW_USAGE &&
	          tdraw_polyline(cv, two, 2, &fill) == TDRAW_USAGE,
	      "a line without width is refused");
	CHECK(tdraw_polygon(cv, two, 2, &fill) == TDRAW_USAGE,
	      "a polygon of two points is refused");
	CHECK(tdraw_ellipse(cv, NAN, 0, 1, 1, &fill) == TDRAW_USAGE &&
	          tdraw_ellipse(cv, 0, 0, 1, 1, &nan) == TDRAW_USAGE &&
	          tdraw_rect(cv, 0, 0, 5, 5, -1, &fill) == TDRAW_USAGE &&
	          tdraw_rect(cv, 9000, 0, 5, 5, 0, &fill) == TDRAW_USAGE &&
	          tdraw_ellipse(cv, 0, 0, -1, 1, &fill) == TDRAW_USAGE,
	      "NaN, negative radii and coordinates past the limit are refused");
}

static uint32_t seed = 12345;

static float rnd(float lo, float hi) {
	seed = seed * 1103515245u + 12345u;
	return lo + (hi - lo) * (float)(seed >> 8) / (float)(1u << 24);
}

static void test_noise(void) {
	int bad = 0;

	for (int i = 0; i < 3000; i++) {
		struct tdraw_pen p = {0xff000000u | (uint32_t)i,
		                      rnd(0, 1) < 0.4f ? 0 : rnd(0.1f, 12),
		                      rnd(0, 1) < 0.5f};
		float xy[40];
		int n = 3 + (int)rnd(0, 17), r;

		for (int k = 0; k < 2 * n; k++)
			xy[k] = rnd(-200, 250);
		switch (i % 5) {
		case 0:
			r = tdraw_polygon(cv, xy, n, &p);
			break;
		case 1:
			r = p.width ? tdraw_polyline(cv, xy, n, &p) : 0;
			break;
		case 2:
			r = tdraw_ellipse(cv, xy[0], xy[1], rnd(0, 300), rnd(0, 300), &p);
			break;
		case 3:
			r = tdraw_arc(cv, xy[0], xy[1], rnd(0, 100), rnd(0, 100),
			              rnd(-720, 720), rnd(-720, 720), &p);
			break;
		default:
			r = tdraw_rect(cv, xy[0], xy[1], rnd(0, 300), rnd(0, 300),
			               rnd(0, 200), &p);
		}
		bad += r != 0;
	}
	CHECK(bad == 0, "shapes at random, inside and outside the canvas, paint");
}

int main(void) {
	test_canvas();
	test_fill();
	test_shapes();
	test_usage();
	test_noise();
	tdraw_canvas_free(cv);
	return test_summary();
}
