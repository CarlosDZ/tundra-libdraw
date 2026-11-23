#include "art.c"
#include "canvas.c"
#include "files.c"
#include "theme.c"

#include "test.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define W 12
#define H 10

typedef const char *picture[H];

static picture base = {
    "kkkkkkkkkkkk", "k.w......w.k", "k..aa..tt..k", "k..aa..tt..k",
    "k..........k", "k.wwwwwwww.k", "k..........k", "kkkkkkkkkkkk",
    "..k......k..", ".kk......kk.",
};

static picture walk[3] = {
    {"kkkkkkkkkkkk", "k.w......w.k", "k..aa..tt..k", "k..aa..tt..k",
     "k..........k", "k.wwwwwwww.k", "k..........k", "kkkkkkkkkkkk",
     "..k.......k.", ".kk.......kk"},
    {"kkkkkkkkkkkk", "k.w......w.k", "k..aa..tt..k", "k..aa..tt..k",
     "k..........k", "k.kkkkkkww.k", "k..........k", "kkkkkkkkkkkk",
     "..k.......k.", ".kk.......kk"},
    {"kkkkkkkkkkkk", "k.w......w.k", "k..aa..ttaak", "k..aa..ttaak",
     "k..........k", "k.kkkkkkww.k", "k..........k", "kkkkkkkkkkkk",
     "..k.......k.", ".kk.......kk"},
};
static const int walk_ms[3] = {100, 200, 300};

static picture blink[3] = {
    {"kkkkkkkkkkkk", "k.w......w.k", "k..aa..tt..k", "k..aa..tt..k",
     "k..........k", "k.wwwwwwww.k", "k..........k", "kkkkkkkkkkkk",
     "aak......k..", ".kk......kk."},
    {"kkkkkkkkkkkk", "k.w......wak", "k..aa..tt..k", "k..aa..tt..k",
     "k..........k", "k.wwwwwwww.k", "k..........k", "kkkkkkkkkkkk",
     "aak......k..", ".kk......kk."},
    {"kkkkkkkkkkkk", "k.w......wak", "k..aa..tt..k", "k..aa..tt..k",
     "k..........k", "k.wwwwwwww.k", "k..........k", "kkkkkkkkkkkk",
     "aak......k..", ".kk......kk."},
};
static const int blink_ms[3] = {50, 70, 80};
static const int blink_set[3] = {0, 1, 0};

static char src[8192];
static size_t src_len;

static void add(const char *s) {
	size_t n = strlen(s);

	memcpy(src + src_len, s, n + 1);
	src_len += n;
}

static void add_picture(const char *block, const picture p, int ms, int set) {
	char line[64];

	snprintf(line, sizeof(line), "\n%s {\n", block);
	add(line);
	if (ms) {
		snprintf(line, sizeof(line), "\tms = %d\n", ms);
		add(line);
	}
	if (set)
		add("\tset = a color1 bf616a\n");
	for (int y = 0; y < H; y++) {
		snprintf(line, sizeof(line), "\trow = %s\n", p[y]);
		add(line);
	}
	add("}\n");
}

static void *toa;
static size_t toa_len;

static int compile(void) {
	char e[TTOA_ERR_MAX];

	free(toa);
	toa = NULL;
	if (ttoa_compile(src, src_len, NULL, NULL, &toa, &toa_len, e) < 0) {
		printf("# %s\n", e);
		return -1;
	}
	return 0;
}

static int sample(void) {
	src_len = 0;
	add("size = 12 10\n\npalette {\n\t. = clear\n\tk = 101820\n"
	    "\tw = ffffff 128\n\ta = accent ff9900\n\tt = color4 128 00ff00\n}\n");
	add_picture("base", base, 0, 0);
	add("\nanim {\n\tname = walk\n}\n");
	for (int i = 0; i < 3; i++)
		add_picture("frame", walk[i], walk_ms[i], 0);
	add("\nanim {\n\tname = blink\n\tloop = yes\n}\n");
	add_picture("frame", blink[0], blink_ms[0], blink_set[0]);
	add_picture("frame", blink[1], blink_ms[1], blink_set[1]);
	add("\nframe {\n\tms = 80\n}\n");
	return compile();
}

static uint32_t premul(uint32_t argb) {
	uint32_t a = argb >> 24, out = a << 24;

	for (int s = 0; s < 24; s += 8)
		out |= ((argb >> s & 0xff) * a + 127) / 255 << s;
	return out;
}

static uint32_t times(uint32_t argb, uint32_t alpha) {
	return ((argb >> 24) * alpha + 127) / 255 << 24 | (argb & 0xffffff);
}

static uint32_t th_accent, th_color4;

static uint32_t want(char ch, int set) {
	switch (ch) {
	case 'k':
		return 0xff101820;
	case 'w':
		return premul(0x80ffffff);
	case 'a':
		return set ? 0xffbf616a : th_accent ? th_accent : 0xffff9900;
	case 't':
		return premul(times(th_color4 ? th_color4 : 0xff00ff00, 128));
	}
	return 0;
}

static int shows(const struct tdraw_art *a, const picture p, int set) {
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (a->px[y * W + x] != want(p[y][x], set)) {
				printf("# (%d, %d) is %08x, not %08x\n", x, y, a->px[y * W + x],
				       want(p[y][x], set));
				return 0;
			}
	return 1;
}

static int bounds(struct tdraw_rect r, const picture p, int pset,
                  const picture q, int qset) {
	int x1 = W, y1 = H, x2 = 0, y2 = 0, cx2, cy2;

	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (want(p[y][x], pset) != want(q[y][x], qset)) {
				x1 = x < x1 ? x : x1;
				y1 = y < y1 ? y : y1;
				x2 = x + 1 > x2 ? x + 1 : x2;
				y2 = y + 1 > y2 ? y + 1 : y2;
			}
	if (x1 == W)
		return r.w == 0 && r.h == 0;
	cx2 = (x2 + 7) / 8 * 8 < W ? (x2 + 7) / 8 * 8 : W;
	cy2 = (y2 + 7) / 8 * 8 < H ? (y2 + 7) / 8 * 8 : H;
	return r.x <= x1 && r.y <= y1 && r.x + r.w >= x2 && r.y + r.h >= y2 &&
	       r.x >= (x1 & ~7) && r.y >= (y1 & ~7) && r.x + r.w <= cx2 &&
	       r.y + r.h <= cy2;
}

static int empty(struct tdraw_rect r) { return r.w == 0 && r.h == 0; }

static char dir[64];

static struct tdraw_theme *theme_with(const char *text) {
	char p[128];
	struct tdraw_theme *t;
	const char *err;
	FILE *f;

	snprintf(p, sizeof(p), "%s/theme", dir);
	if (!(f = fopen(p, "w")))
		return NULL;
	fputs(text, f);
	fclose(f);
	if (!(t = theme_open_at(dir, NULL)))
		return NULL;
	if (tdraw_theme_load(t, &err) < 0) {
		tdraw_theme_free(t);
		return NULL;
	}
	return t;
}

static void test_colors(void) {
	struct tdraw_theme *t1, *t2;
	struct tdraw_art *a;
	const char *err = NULL;
	int w = 0, h = 0;

	th_accent = th_color4 = 0;
	CHECK(want('w', 0) == 0x80808080 &&
	          premul(times(0xff5e81ac, 64)) == 0x4018202b,
	      "art: the model premultiplies as libdraw does");
	if (!(a = tdraw_art_open(toa, toa_len, NULL, &err))) {
		CHECK(0, "art: opens");
		printf("# %s\n", err);
		return;
	}
	tdraw_art_size(a, &w, &h);
	CHECK(w == W && h == H, "art: its size");
	CHECK(!strcmp(tdraw_art_anim(a, 0), "walk") &&
	          !strcmp(tdraw_art_anim(a, 1), "blink") && !tdraw_art_anim(a, 2) &&
	          !tdraw_art_anim(a, -1),
	      "art: its animations by name, in the order of the source");
	CHECK(shows(a, base, 0), "art: the base, with the RGB of the drawing");

	t1 = theme_with("accent = 88c0d0\ncolor4 = 5e81ac 128\n");
	t2 = theme_with("accent = a3be8c\ncolor4 = 5e81ac 128\nother = 000000\n");
	if (!t1 || !t2) {
		CHECK(0, "art: themes load");
		goto out;
	}
	th_accent = 0xff88c0d0;
	th_color4 = 0x805e81ac;
	CHECK(tdraw_art_theme(a, t1) == 1 && shows(a, base, 0),
	      "art: theme colors, their alpha times the entry's");
	CHECK(tdraw_art_theme(a, t1) == 0, "art: the same theme changes nothing");
	th_accent = 0xffa3be8c;
	CHECK(tdraw_art_theme(a, t2) == 1 && shows(a, base, 0),
	      "art: a theme reloaded repaints its colors");
	tdraw_art_free(a);
	a = tdraw_art_open(toa, toa_len, t2, &err);
	CHECK(a && shows(a, base, 0), "art: opens with a theme");
	th_accent = th_color4 = 0;
	CHECK(a && tdraw_art_theme(a, NULL) == 1 && shows(a, base, 0),
	      "art: without a theme, the RGB of the drawing again");
out:
	tdraw_theme_free(t1);
	tdraw_theme_free(t2);
	tdraw_art_free(a);
}

static void test_walk(void) {
	struct tdraw_art *a = tdraw_art_open(toa, toa_len, NULL, NULL);
	struct tdraw_rect r = {1, 1, 1, 1};
	int ms;

	th_accent = th_color4 = 0;
	if (!a) {
		CHECK(0, "walk: opens");
		return;
	}
	CHECK(tdraw_art_advance(a, 5, &r) == -1 && empty(r),
	      "walk: nothing plays after opening");
	CHECK(tdraw_art_play(a, "run", 1000) == TDRAW_USAGE,
	      "walk: an animation it lacks is refused");
	CHECK(tdraw_art_play(a, "walk", 1000) == 0, "walk: plays");
	ms = tdraw_art_advance(a, 990, &r);
	CHECK(ms == 10 && empty(r) && shows(a, base, 0),
	      "walk: before its start, the time left");
	ms = tdraw_art_advance(a, 1000, &r);
	CHECK(ms == 100 && shows(a, walk[0], 0) && bounds(r, base, 0, walk[0], 0),
	      "walk: its first frame, from the base");
	CHECK(r.x == 8 && r.y == 8 && r.w == 4 && r.h == 2,
	      "walk: a cell cut by both edges");
	ms = tdraw_art_advance(a, 1099, &r);
	CHECK(ms == 1 && empty(r) && shows(a, walk[0], 0),
	      "walk: a frame lasts its ms");
	ms = tdraw_art_advance(a, 1100, &r);
	CHECK(ms == 200 && shows(a, walk[1], 0) &&
	          bounds(r, walk[0], 0, walk[1], 0),
	      "walk: the second frame");
	ms = tdraw_art_advance(a, 1300, &r);
	CHECK(ms == -1 && shows(a, walk[2], 0) && bounds(r, walk[1], 0, walk[2], 0),
	      "walk: the last frame stays, and says so at once");
	CHECK(tdraw_art_advance(a, 99999, &r) == -1 && empty(r) &&
	          shows(a, walk[2], 0),
	      "walk: and nothing changes after it");

	tdraw_art_play(a, "walk", 0);
	CHECK(tdraw_art_advance(a, 1u << 31, &r) == -1 && shows(a, walk[2], 0) &&
	          bounds(r, walk[2], 0, walk[2], 0),
	      "walk: played again late, it ends as it was");
	tdraw_art_play(a, NULL, 5);
	CHECK(tdraw_art_advance(a, 5, &r) == -1 && shows(a, base, 0) &&
	          bounds(r, walk[2], 0, base, 0),
	      "walk: NULL brings the base back");
	tdraw_art_free(a);
}

static void test_blink(void) {
	struct tdraw_art *a = tdraw_art_open(toa, toa_len, NULL, NULL);
	struct tdraw_rect r;
	uint64_t day = 86400000;
	int ms;

	th_accent = th_color4 = 0;
	if (!a) {
		CHECK(0, "blink: opens");
		return;
	}
	tdraw_art_play(a, "walk", 0);
	tdraw_art_advance(a, 100, NULL);
	tdraw_art_play(a, "blink", 200);
	ms = tdraw_art_advance(a, 200, &r);
	CHECK(ms == 50 && shows(a, blink[0], 0) &&
	          bounds(r, walk[1], 0, blink[0], 0),
	      "blink: another animation starts from the base");
	ms = tdraw_art_advance(a, 250, &r);
	CHECK(ms == 70 && shows(a, blink[1], 1) &&
	          bounds(r, blink[0], 0, blink[1], 1),
	      "blink: a frame that changes a color");
	ms = tdraw_art_advance(a, 320, &r);
	CHECK(ms == 80 && shows(a, blink[2], 0) &&
	          bounds(r, blink[1], 1, blink[2], 0),
	      "blink: and the color back");
	ms = tdraw_art_advance(a, 400, &r);
	CHECK(ms == 50 && shows(a, blink[0], 0) &&
	          bounds(r, blink[2], 0, blink[0], 0),
	      "blink: the loop comes back to its first frame");
	ms = tdraw_art_advance(a, 460, &r);
	CHECK(ms == 60 && shows(a, blink[1], 1), "blink: and goes on");

	tdraw_art_advance(a, 600, NULL);
	ms = tdraw_art_advance(a, 200 + 200 * (3650 * day / 200) + 125, &r);
	CHECK(ms == 75 && shows(a, blink[2], 0) &&
	          bounds(r, blink[0], 0, blink[2], 0),
	      "blink: woken late, at the frame due and with its colors");
	tdraw_art_free(a);
}

static void test_open_broken(void) {
	uint64_t *copy = malloc(toa_len + 8);
	const char *err = NULL;
	struct tdraw_art *a;

	if (!copy)
		return;
	memcpy((char *)copy + 1, toa, toa_len);
	a = tdraw_art_open((char *)copy + 1, toa_len, NULL, &err);
	CHECK(!a && err && strstr(err, "aligned"),
	      "open: refuses bytes not aligned to 8, saying why");
	memcpy(copy, toa, toa_len);
	err = NULL;
	a = tdraw_art_open(copy, toa_len - 8, NULL, &err);
	CHECK(!a && err && *err, "open: refuses a .toa cut short");
	CHECK(!tdraw_art_open(copy, 3, NULL, NULL), "open: err may be NULL");
	free(copy);
}

static const char *tile_p[8] = {
    "k.......", "kk......", "kkk.....", "kkkk....",
    "kkkkk...", "kkkkkk..", "kkkkkkk.", "kkkkkkkw",
};

static char flips[16][17];

static void test_flips(void) {
	struct tdraw_art *a;
	struct ttoa t;
	char line[64];
	int ok = 1;

	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 16; x++) {
			int tx = x < 8 ? x : 15 - x, ty = y < 8 ? y : 15 - y;

			flips[y][x] = tile_p[ty][tx];
		}
		flips[y][16] = 0;
	}
	src_len = 0;
	add("size = 16 16\npalette {\n\t. = clear\n\tk = 101820\n\tw = ffffff\n}\n"
	    "base {\n");
	for (int y = 0; y < 16; y++) {
		snprintf(line, sizeof(line), "\trow = %s\n", flips[y]);
		add(line);
	}
	add("}\n");
	if (compile() < 0 || ttoa_open(toa, toa_len, &t) < 0 ||
	    !(a = tdraw_art_open(toa, toa_len, NULL, NULL))) {
		CHECK(0, "flips: opens");
		return;
	}
	CHECK(t.ntiles == 1, "flips: the drawing keeps one tile");
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			char ch = flips[y][x];
			uint32_t v = ch == 'k' ? 0xff101820 : ch == 'w' ? 0xffffffff : 0;

			ok &= a->px[y * 16 + x] == v;
		}
	CHECK(ok, "flips: cells flipped as the map says");
	tdraw_art_free(a);
}

#define CW 40
#define CH 40

static uint32_t cpx[CW * CH];

static int painted(const struct tdraw_art *a, int x0, int y0, int s, int flip) {
	for (int y = 0; y < CH; y++)
		for (int x = 0; x < CW; x++) {
			int u = x - x0, v = y - y0;
			uint32_t want = 0;

			if (u >= 0 && v >= 0 && u < W * s && v < H * s) {
				u /= s;
				v /= s;
				u = flip & TDRAW_HFLIP ? W - 1 - u : u;
				v = flip & TDRAW_VFLIP ? H - 1 - v : v;
				want = a->px[v * W + u];
			}
			if (cpx[y * CW + x] != want) {
				printf("# (%d, %d) is %08x, not %08x\n", x, y, cpx[y * CW + x],
				       want);
				return 0;
			}
		}
	return 1;
}

static void test_paint(void) {
	struct tdraw_canvas *c = tdraw_canvas_new(cpx, CW, CH, CW * 4);
	struct tdraw_art *a;
	static const struct {
		int x, y, s, flip;
		const char *desc;
	} cases[] = {
	    {3, 2, 1, 0, "paint: at scale 1"},
	    {1, 4, 3, 0, "paint: at scale 3, a square per pixel"},
	    {2, 1, 1, TDRAW_HFLIP, "paint: flipped horizontally"},
	    {0, 3, 2, TDRAW_VFLIP, "paint: flipped vertically, at scale 2"},
	    {5, 0, 3, TDRAW_HFLIP | TDRAW_VFLIP, "paint: flipped both ways"},
	    {-7, 30, 2, TDRAW_HFLIP, "paint: cut by the canvas"},
	};

	th_accent = th_color4 = 0;
	if (sample() < 0 || !c || !(a = tdraw_art_open(toa, toa_len, NULL, NULL))) {
		CHECK(0, "paint: opens");
		tdraw_canvas_free(c);
		return;
	}
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(cpx, 0, sizeof(cpx));
		CHECK(tdraw_art(c, a, cases[i].x, cases[i].y, cases[i].s,
		                cases[i].flip) == 0 &&
		          painted(a, cases[i].x, cases[i].y, cases[i].s, cases[i].flip),
		      cases[i].desc);
	}
	tdraw_art(c, a, 0, 0, 2, TDRAW_HFLIP);
	memset(cpx, 0, sizeof(cpx));
	tdraw_art(c, a, 0, 0, 1, 0);
	CHECK(painted(a, 0, 0, 1, 0), "paint: no transform outlives its call");
	CHECK(tdraw_art(c, a, 0, 0, 0, 0) == TDRAW_USAGE &&
	          tdraw_art(c, a, 0, 0, 65, 0) == TDRAW_USAGE &&
	          tdraw_art(c, a, 0, 0, 1, 4) == TDRAW_USAGE &&
	          tdraw_art(c, a, 8190, 0, 1, 0) == TDRAW_USAGE,
	      "paint: a scale, a flip or a place out of range is refused");
	tdraw_art_free(a);
	tdraw_canvas_free(c);
}

int main(void) {
	const char *tmp = getenv("TMPDIR");

	alarm(60);
	snprintf(dir, sizeof(dir), "%s/tdraw-art-XXXXXX", tmp ? tmp : "/tmp");
	if (!mkdtemp(dir)) {
		perror("mkdtemp error");
		return 1;
	}
	if (sample() < 0) {
		CHECK(0, "the sample compiles");
		return test_summary();
	}
	test_colors();
	test_walk();
	test_blink();
	test_open_broken();
	test_flips();
	test_paint();
	free(toa);
	{
		char p[128];

		snprintf(p, sizeof(p), "%s/theme", dir);
		unlink(p);
		rmdir(dir);
	}
	return test_summary();
}
