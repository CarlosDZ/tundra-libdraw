#include "canvas.c"
#include "files.c"
#include "font.c"

#include "bdf-write.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define W 64
#define H 48
#define WHITE 0xffffffff

static uint32_t px[W * H], want[W * H];
static struct tdraw_canvas *cv;
static char dir[64], sys[96], usr[96];
static const char *err;

static const uint8_t A[16] = {0x00, 0x18, 0x24, 0x42, 0x42, 0x7e, 0x42, 0x42,
                              0x42, 0x42, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00};
static const uint8_t SPACE[16];
static const uint8_t BIG_BOX[16] = {0xff, 0x81, 0x81, 0x81, 0x81, 0x81,
                                    0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
                                    0x81, 0x81, 0x81, 0xff};
static const uint8_t B[8] = {0xe0, 0x90, 0xe0, 0x90, 0xe0, 0x00, 0x00, 0xf0};
static const uint8_t REPL[8] = {0xf0, 0x90, 0x60, 0x60, 0x90, 0xf0, 0x00, 0x00};
static const uint8_t DOT[8] = {0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};

static void path_in(char *buf, size_t n, const char *name) {
	snprintf(buf, n, "%s/%s", dir, name);
}

static void write_text(const char *d, const char *name, const char *text) {
	char p[256];
	FILE *f;

	snprintf(p, sizeof(p), "%s/%s", d, name);
	if ((f = fopen(p, "w"))) {
		fputs(text, f);
		fclose(f);
	}
}

static void sys_fonts(const char *fmt) {
	char text[1024];

	snprintf(text, sizeof(text), fmt, dir, dir, dir);
	write_text(sys, "fonts", text);
}

static struct tdraw_font *open_sys(const char *use, int size) {
	return font_open_at(sys, usr, use, size, &err);
}

static void clear(void) {
	memset(px, 0, sizeof(px));
	memset(want, 0, sizeof(want));
}

static void put(int x0, int y0, const uint8_t *rows, int w, int h, int s) {
	for (int y = 0; y < h * s; y++)
		for (int x = 0; x < w * s; x++)
			if (rows[y / s] >> (7 - x / s) & 1 && x0 + x >= 0 && x0 + x < W &&
			    y0 + y >= 0 && y0 + y < H)
				want[(y0 + y) * W + x0 + x] = WHITE;
}

static int same(void) { return !memcmp(px, want, sizeof(px)); }

static int metrics_are(struct tdraw_font *f, int height, int ascent, int cell) {
	struct tdraw_font_metrics m = {0};

	if (!f)
		return 0;
	tdraw_font_metrics(f, &m);
	return m.height == height && m.ascent == ascent && m.cell == cell;
}

static int text(struct tdraw_font *f, int x, int y, const char *s) {
	return tdraw_text(cv, f, x, y, s, strlen(s), WHITE);
}

static void setup(void) {
	static const struct bdf_glyph big[] = {{'A', A}, {' ', SPACE}};
	static const struct bdf_glyph small[] = {{'B', B}, {0xfffd, REPL}};
	char p[128];
	const char *tmp = getenv("TMPDIR");

	snprintf(dir, sizeof(dir), "%s/tdraw-font-XXXXXX", tmp ? tmp : "/tmp");
	if (!mkdtemp(dir)) {
		perror("mkdtemp error");
		exit(1);
	}
	path_in(sys, sizeof(sys), "sys");
	path_in(usr, sizeof(usr), "usr");
	mkdir(sys, 0700);
	mkdir(usr, 0700);
	path_in(p, sizeof(p), "big.bdf");
	bdf_write(p, 8, 16, 12, big, 2);
	path_in(p, sizeof(p), "small.bdf");
	bdf_write(p, 4, 8, 6, small, 2);
}

static void test_bitmap(void) {
	struct tdraw_font *f;

	sys_fonts("mono = %s/big.bdf\nmono = %s/small.bdf\n");
	f = open_sys("mono", 16);
	CHECK(metrics_are(f, 16, 12, 8),
	      "a bitmap font at its height sets the line and the cell");
	clear();
	put(3, 2, A, 8, 16, 1);
	put(19, 2, A, 8, 16, 1);
	CHECK(f && text(f, 3, 2, "A A") == 0 && same(),
	      "each letter is its bitmap, to the pixel, a cell after the last");
	clear();
	put(0, 0, B, 4, 8, 2);
	CHECK(f && text(f, 0, 0, "B") == 0 && same(),
	      "a letter the first font lacks comes from the next, at its scale");
	clear();
	put(8, 0, BIG_BOX, 8, 16, 1);
	CHECK(f && text(f, 8, 0, "Z") == 0 && same(),
	      "a letter no font has is the box of the first");
	clear();
	put(0, 0, REPL, 4, 8, 2);
	put(8, 0, A, 8, 16, 1);
	CHECK(f &&
	          text(f, 0, 0,
	               "\xe2\x82"
	               "A") == 0 &&
	          same(),
	      "broken UTF-8 is one U+FFFD, and what follows is kept");
	CHECK(f && tdraw_text_width(f, "AB\xe2\x82", 4) == 24 &&
	          tdraw_text_width(f, "", 0) == 0,
	      "the width of a line counts cells");
	tdraw_font_free(f);

	f = open_sys("mono", 32);
	CHECK(metrics_are(f, 32, 24, 16),
	      "at twice its height, a line twice as tall");
	clear();
	put(0, 0, A, 8, 16, 2);
	CHECK(f && text(f, 0, 0, "A") == 0 && same(),
	      "and each pixel a square of two");
	tdraw_font_free(f);

	f = open_sys("mono", 24);
	CHECK(metrics_are(f, 24, 18, 12),
	      "a font whose height does not divide the size is skipped");
	tdraw_font_free(f);
	err = NULL;
	CHECK(!open_sys("mono", 10) && err && strstr(err, "fits"),
	      "a size no font fits is refused, and says why");
}

static void test_paint(void) {
	struct tdraw_font *f;
	int outside = 0, inside = 0;

	sys_fonts("mono = %s/big.bdf\n");
	f = open_sys("mono", 16);
	clear();
	tdraw_clip(cv, 0, 0, 4, 4);
	text(f, 0, 0, "A");
	tdraw_unclip(cv);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (px[y * W + x])
				*(x < 4 && y < 4 ? &inside : &outside) += 1;
	CHECK(inside && !outside, "text keeps to the clip");
	tdraw_fill(cv, 0, 0, W, H, 0xff000000);
	CHECK(tdraw_text(cv, f, 0, 0, "A", 1, 0x80ffffff) == 0 &&
	          px[5 * W + 1] == 0xff808080 && px[5 * W] == 0xff000000,
	      "a half transparent letter blends over what is there");
	clear();
	CHECK(text(f, -5, -7, "AAAA") == 0 && text(f, 60, 40, "AAAA") == 0 &&
	          text(f, -8192, 8192, "A") == 0 &&
	          tdraw_text(cv, f, 9000, 0, "A", 1, WHITE) == TDRAW_USAGE &&
	          tdraw_text(cv, f, 0, 0, "A", TDRAW_TEXT_MAX + 1, WHITE) ==
	              TDRAW_USAGE,
	      "text past the edges is cut; past the limits, refused");
	srand(7);
	for (int i = 0; i < 2000; i++) {
		char junk[64];
		size_t n = (size_t)rand() % sizeof(junk);

		for (size_t k = 0; k < n; k++)
			junk[k] = (char)rand();
		tdraw_text(cv, f, rand() % 80 - 8, rand() % 60 - 8, junk, n, WHITE);
		tdraw_text_width(f, junk, n);
	}
	CHECK(1, "random bytes are drawn without leaving the canvas");
	tdraw_font_free(f);
}

static void test_files(void) {
	struct tdraw_font *f;
	char home[96], p[128];

	sys_fonts("mono = %s/big.bdf\n");
	snprintf(p, sizeof(p), "mono = %s/small.bdf\n", dir);
	write_text(usr, "fonts", p);
	f = open_sys("mono", 24);
	CHECK(metrics_are(f, 24, 18, 12),
	      "a use in the user's file replaces the system's list");
	tdraw_font_free(f);
	snprintf(p, sizeof(p), "ui = %s/small.bdf\n", dir);
	write_text(usr, "fonts", p);
	CHECK(!open_sys("mono", 24) && (f = open_sys("mono", 16)) &&
	          metrics_are(f, 16, 12, 8),
	      "and leaves the other uses alone");
	tdraw_font_free(f);
	snprintf(p, sizeof(p), "%s/fonts", usr);
	unlink(p);

	sys_fonts("mono = /nonexistent/font.bdf\nmono = %s/big.bdf\n");
	f = open_sys("mono", 16);
	CHECK(metrics_are(f, 16, 12, 8), "a font that does not open is skipped");
	tdraw_font_free(f);

	err = NULL;
	write_text(sys, "fonts", "# fonts\nmono {\n}\n");
	CHECK(!open_sys("mono", 16) && err && strstr(err, "sys/fonts:2:"),
	      "a block is refused, with its line");
	write_text(sys, "fonts", "mono = big.bdf\n");
	CHECK(!open_sys("mono", 16) && strstr(err, ":1: not an absolute path"),
	      "so is a relative path");
	write_text(sys, "fonts", "\n\nmono\n");
	CHECK(!open_sys("mono", 16) && strstr(err, ":3:"),
	      "and a line libconf does not take");
	sys_fonts("ui = %s/big.bdf\n");
	CHECK(!open_sys("mono", 16) && strstr(err, "no fonts for mono"),
	      "a use without fonts is refused");
	CHECK(!open_sys("", 16) && !open_sys("a b", 16) && !open_sys(NULL, 16) &&
	          !open_sys("ui", 0) && !open_sys("ui", TDRAW_FONT_SIZE_MAX + 1),
	      "a use that is no name, or a size out of 1 to 256, is refused");

	snprintf(home, sizeof(home), "%s/home", dir);
	snprintf(p, sizeof(p), "%s/.config/tundra", home);
	mkdir(home, 0700);
	snprintf(p, sizeof(p), "%s/.config", home);
	mkdir(p, 0700);
	snprintf(p, sizeof(p), "%s/.config/tundra", home);
	mkdir(p, 0700);
	{
		char line[128];

		snprintf(line, sizeof(line), "mono = %s/big.bdf\n", dir);
		write_text(p, "fonts", line);
	}
	setenv("HOME", home, 1);
	f = tdraw_font_open("mono", 16, &err);
	CHECK(metrics_are(f, 16, 12, 8), "tdraw_font_open reads ~/.config/tundra");
	tdraw_font_free(f);
	unsetenv("HOME");
	CHECK(!tdraw_font_open("mono", 16, NULL),
	      "without HOME, only the system's file counts");
}

static void test_cache(void) {
	enum { N = 20000 };
	static struct bdf_glyph many[N];
	static char line[N * 3];
	struct tdraw_font *f;
	char p[128];

	for (int i = 0; i < N; i++) {
		uint32_t cp = 0x4e00 + (uint32_t)i;

		many[i] = (struct bdf_glyph){cp, DOT};
		line[3 * i] = (char)(0xe0 | cp >> 12);
		line[3 * i + 1] = (char)(0x80 | (cp >> 6 & 0x3f));
		line[3 * i + 2] = (char)(0x80 | (cp & 0x3f));
	}
	path_in(p, sizeof(p), "many.bdf");
	bdf_write(p, 4, 8, 6, many, N);
	sys_fonts("mono = %s/many.bdf\n");
	f = open_sys("mono", 8);
	clear();
	put(0, 0, DOT, 4, 8, 1);
	CHECK(f && tdraw_text(cv, f, 0, 0, line, sizeof(line), WHITE) == 0 &&
	          (memset(px, 0, sizeof(px)),
	           tdraw_text(cv, f, 0, 0, line, 3, WHITE) == 0) &&
	          same() && tdraw_text(cv, f, 0, 0, line, sizeof(line), WHITE) == 0,
	      "past the limit of the cache, a letter that left it is drawn again");
	tdraw_font_free(f);
}

static int ink_within(int x0, int y0, int x1, int y1) {
	int ink = 0, gray = 0;

	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			uint32_t a = px[y * W + x] >> 24;

			if (!a)
				continue;
			if (x < x0 || x >= x1 || y < y0 || y >= y1)
				return 0;
			ink = 1;
			gray |= a < 255;
		}
	return ink && gray;
}

static void test_vector(void) {
	struct tdraw_font *f;
	struct tdraw_font_metrics m = {0};
	char conf[512];

	snprintf(conf, sizeof(conf),
	         "mono = %s/big.bdf\nmono = %s/DejaVuSansMono.ttf\n"
	         "ui = %s/DejaVuSansMono.ttf\nsans = %s/DejaVuSans.ttf\n",
	         dir, TEST_FONT_DIR, TEST_FONT_DIR, TEST_FONT_DIR);
	write_text(sys, "fonts", conf);
	f = open_sys("mono", 16);
	clear();
	CHECK(f && text(f, 8, 0, "\xc3\xa9") == 0 && ink_within(8, 0, 16, 16),
	      "a letter from a vector font fits in the cell, smoothed");
	tdraw_font_free(f);

	f = open_sys("ui", 20);
	if (f)
		tdraw_font_metrics(f, &m);
	clear();
	CHECK(f && m.height == 20 && m.ascent > 10 && m.ascent < 20 && m.cell > 5 &&
	          m.cell < 20 && tdraw_text_width(f, "abc", 3) == 3 * m.cell &&
	          text(f, 0, 0, "Hg") == 0 && ink_within(0, 0, 2 * m.cell, 20),
	      "a vector font alone keeps its letters within the line");
	tdraw_font_free(f);

	f = open_sys("sans", 20);
	if (f)
		tdraw_font_metrics(f, &m);
	CHECK(f && m.cell == 0 &&
	          tdraw_text_width(f, "il", 2) < tdraw_text_width(f, "mm", 2),
	      "a proportional one measures each letter");
	tdraw_font_free(f);
	if (!f)
		printf("# DejaVu Sans and Sans Mono are needed in %s\n", TEST_FONT_DIR);
}

static int exact(const char *use, int size) {
	return font_exact_at(sys, usr, use, size, &err);
}

static void test_exact(void) {
	struct tdraw_font *f;
	char conf[512];
	int r;

	sys_fonts("ui = %s/big.bdf\nui = %s/small.bdf\n");
	r = exact("ui", 33);
	f = open_sys("ui", r);
	CHECK(r == 32 && metrics_are(f, 32, 24, 16),
	      "the largest line drawn to the pixel, a bitmap font at its scale");
	tdraw_font_free(f);
	CHECK(exact("ui", 45) == 40 && exact("ui", 15) == 8 && exact("ui", 7) == 0,
	      "from any of the bitmap fonts, and none below the smallest");
	snprintf(conf, sizeof(conf),
	         "ui = %s/big.bdf\nui = %s/DejaVuSansMono.ttf\n"
	         "sans = %s/DejaVuSans.ttf\nsans = %s/big.bdf\n",
	         dir, TEST_FONT_DIR, TEST_FONT_DIR, dir);
	write_text(sys, "fonts", conf);
	CHECK(exact("ui", 20) == 16 && exact("ui", 15) == 0 &&
	          exact("sans", 40) == 0,
	      "below the bitmap font, or after a vector one, nothing is exact");
	sys_fonts("ui = %s/none.bdf\nui = %s/big.bdf\n");
	CHECK(exact("ui", 33) == 32, "a font that does not open is skipped");
	write_text(sys, "fonts", "ui {\n}\n");
	CHECK(exact("ui", 24) == TDRAW_USAGE && strstr(err, "fonts:1:") &&
	          exact("ui", 0) == TDRAW_USAGE,
	      "a broken fonts file or a size out of range is an error");
}

static void cleanup(void) {
	static const char *names[] = {"big.bdf",
	                              "small.bdf",
	                              "many.bdf",
	                              "sys/fonts",
	                              "usr/fonts",
	                              "home/.config/tundra/fonts",
	                              "home/.config/tundra",
	                              "home/.config",
	                              "home",
	                              "sys",
	                              "usr"};
	char p[128];

	for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
		path_in(p, sizeof(p), names[i]);
		remove(p);
	}
	rmdir(dir);
}

int main(void) {
	cv = tdraw_canvas_new(px, W, H, W * 4);
	setup();
	test_bitmap();
	test_paint();
	test_files();
	test_cache();
	test_vector();
	test_exact();
	cleanup();
	tdraw_canvas_free(cv);
	return test_summary();
}
