#include "files.c"
#include "theme.c"

#include "test.h"

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char dir[64], sys[96], usr[96], late[96];
static const char *err;

static void path_in(char *buf, size_t n, const char *name) {
	snprintf(buf, n, "%s/%s", dir, name);
}

static void save(const char *d, const char *name, const char *text) {
	char tmp[160], p[160];
	FILE *f;

	snprintf(tmp, sizeof(tmp), "%s/.%s.tmp", d, name);
	snprintf(p, sizeof(p), "%s/%s", d, name);
	if ((f = fopen(tmp, "w"))) {
		fputs(text, f);
		fclose(f);
		rename(tmp, p);
	}
}

static void overwrite(const char *d, const char *name, const char *text) {
	char p[160];
	FILE *f;

	snprintf(p, sizeof(p), "%s/%s", d, name);
	if ((f = fopen(p, "w"))) {
		fputs(text, f);
		fclose(f);
	}
}

static void drop(const char *d, const char *name) {
	char p[160];

	snprintf(p, sizeof(p), "%s/%s", d, name);
	unlink(p);
}

static int ready(const struct tdraw_theme *t) {
	struct pollfd p = {tdraw_theme_fd(t), POLLIN, 0};

	return poll(&p, 1, 2000) == 1;
}

static int parse_fails(const char *text, const char *where) {
	struct color c[8];
	char e[DRAW_ERR_MAX] = "";
	int n = 0;

	return theme_parse("t", text, strlen(text), c, &n, e) && strstr(e, where);
}

static void test_parse(void) {
	struct color c[8];
	char e[DRAW_ERR_MAX];
	int n = 0;
	const char *ok = "accent = 88c0d0\nborder = 4C566A 128\nclear = 000000 0\n";

	CHECK(!theme_parse("t", ok, strlen(ok), c, &n, e) && n == 3 &&
	          !strcmp(c[0].name, "accent") && c[0].argb == 0xff88c0d0 &&
	          c[1].argb == 0x804c566a && c[2].argb == 0,
	      "a theme is RRGGBB, with an alpha of 0 to 255 if it says so");
	CHECK(parse_fails("a = 88c0d\n", "t:1:") &&
	          parse_fails("a = 88c0dg\n", "t:1:") &&
	          parse_fails("a = 88c0d0x\n", "t:1:") &&
	          parse_fails("\na = 88c0d0 256\n", "t:2:") &&
	          parse_fails("a = 88c0d0 -1\n", "t:1:") &&
	          parse_fails("a = 88c0d0 12 3\n", "t:1:"),
	      "a color that is not six hex digits and an alpha is refused");
	CHECK(parse_fails("a = 111111\na = 222222\n", "t:2: a twice") &&
	          parse_fails("a {\n}\n", "t:1:") && parse_fails("a\n", "t:1:"),
	      "and so are a name twice, a block and a line libconf does not take");
}

static void test_shipped(void) {
	static const char *names[] = {
	    "accent",  "alert", "background", "borealis.fallen",
	    "border",  "dim",   "focus",      "ok",
	    "surface", "text",  "warn"};
	struct color c[64];
	char e[DRAW_ERR_MAX] = "", *text = NULL, want[16];
	size_t len = 0;
	int n = 0, found = 0;

	if (draw_read_conf(TEST_DATA, "theme", &text, &len, e) || !text ||
	    theme_parse(TEST_DATA "/theme", text, len, c, &n, e)) {
		CHECK(0, "the theme installed parses");
		printf("# %s\n", e);
		free(text);
		return;
	}
	for (int i = 0; i < 16; i++) {
		snprintf(want, sizeof(want), "color%d", i);
		for (int k = 0; k < n; k++)
			found += !strcmp(c[k].name, want);
	}
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		for (int k = 0; k < n; k++)
			found += !strcmp(c[k].name, names[i]);
	CHECK(n == 27 && found == 27,
	      "the theme installed has color0 to color15 and the common names");
	free(text);
}

static void test_layers(void) {
	struct tdraw_theme *t;

	overwrite(sys, "theme", "accent = 111111\ntext = 222222\n");
	overwrite(usr, "theme", "accent = 333333\ncolor4 = 5e81ac\n");
	t = theme_open_at(sys, usr);
	CHECK(t && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff333333 &&
	          tdraw_theme_color(t, "text", 0) == 0xff222222 &&
	          tdraw_theme_color(t, "color4", 0) == 0xff5e81ac,
	      "the user's theme replaces the system's color by color");
	CHECK(tdraw_theme_color(t, "nope", 0x12345678) == 0x12345678 &&
	          tdraw_theme_color(NULL, "text", 7) == 7 &&
	          tdraw_theme_color(t, NULL, 7) == 7,
	      "a color the theme lacks is the caller's");
	CHECK(t && tdraw_theme_load(t, &err) == 0,
	      "loading the same files again changes nothing");
	tdraw_theme_free(t);
}

static void test_watch(void) {
	struct tdraw_theme *t = theme_open_at(sys, usr);

	CHECK(t && tdraw_theme_fd(t) >= 0 && tdraw_theme_load(t, &err) == 1,
	      "a theme gives a descriptor to watch");
	save(usr, "theme", "accent = 444444\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff444444,
	      "a theme saved by renaming is seen at once");
	overwrite(usr, "theme", "accent = 444444\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 0,
	      "the same colors written again are no change");
	overwrite(usr, "theme", "accent = 44444\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == TDRAW_USAGE &&
	          strstr(err, "usr/theme:1:") &&
	          tdraw_theme_color(t, "accent", 0) == 0xff444444,
	      "a broken theme keeps the colors as they were, and says why");
	overwrite(usr, "theme", "accent = 555555\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff555555,
	      "and once mended, it is read");
	overwrite(usr, "fonts", "# nothing\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 0,
	      "another file of the directory is no change");
	drop(usr, "theme");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff111111,
	      "without the user's theme, the system's is back");
	save(sys, "theme", "accent = 666666\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff666666,
	      "a change to the system's theme is seen too");
	drop(usr, "fonts");
	tdraw_theme_free(t);
}

static void test_late_dir(void) {
	struct tdraw_theme *t = theme_open_at(sys, late);

	CHECK(t && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff666666,
	      "a user directory that does not exist is no error");
	mkdir(late, 0700);
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 0,
	      "when it is made, it is seen");
	save(late, "theme", "accent = 777777\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff777777,
	      "and so is a theme put in it");
	drop(late, "theme");
	rmdir(late);
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff666666,
	      "deleted, the system's theme is back");
	mkdir(late, 0700);
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 0,
	      "made again, it is watched again");
	save(late, "theme", "accent = 888888\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff888888,
	      "with its theme");
	drop(late, "theme");
	rmdir(late);
	tdraw_theme_free(t);
}

static void test_deep_dir(void) {
	char top[128], mid[160], low[192];
	struct tdraw_theme *t;

	snprintf(top, sizeof(top), "%s/deep", dir);
	snprintf(mid, sizeof(mid), "%s/a", top);
	snprintf(low, sizeof(low), "%s/b", mid);
	t = theme_open_at(sys, low);
	CHECK(t && tdraw_theme_load(t, &err) == 1,
	      "a user directory two levels missing is no error");
	mkdir(top, 0700);
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 0,
	      "the first level made is seen");
	mkdir(mid, 0700);
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 0, "and the next");
	mkdir(low, 0700);
	save(low, "theme", "accent = 999999\n");
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff999999,
	      "and a theme put in the last one");
	drop(low, "theme");
	rmdir(low);
	rmdir(mid);
	CHECK(ready(t) && tdraw_theme_load(t, &err) == 1 &&
	          tdraw_theme_color(t, "accent", 0) == 0xff666666,
	      "deleted with the one above it, the system's theme is back");
	mkdir(mid, 0700);
	mkdir(low, 0700);
	save(low, "theme", "accent = aaaaaa\n");
	for (int i = 0; i < 3 && tdraw_theme_color(t, "accent", 0) != 0xffaaaaaa;
	     i++)
		if (ready(t))
			tdraw_theme_load(t, &err);
	CHECK(tdraw_theme_color(t, "accent", 0) == 0xffaaaaaa,
	      "and both made again are watched again");
	drop(low, "theme");
	rmdir(low);
	rmdir(mid);
	rmdir(top);
	tdraw_theme_free(t);
}

static void test_public(void) {
	struct tdraw_theme *t;
	char home[96], p[128];

	snprintf(home, sizeof(home), "%s/home", dir);
	mkdir(home, 0700);
	snprintf(p, sizeof(p), "%s/.config", home);
	mkdir(p, 0700);
	snprintf(p, sizeof(p), "%s/.config/tundra", home);
	mkdir(p, 0700);
	overwrite(p, "theme", "alert = bf616a\n");
	setenv("HOME", home, 1);
	t = tdraw_theme_open();
	CHECK(t && tdraw_theme_load(t, NULL) == 1 &&
	          tdraw_theme_color(t, "alert", 0) == 0xffbf616a,
	      "tdraw_theme_open reads ~/.config/tundra");
	tdraw_theme_free(t);
	drop(p, "theme");
	rmdir(p);
	snprintf(p, sizeof(p), "%s/.config", home);
	rmdir(p);
	rmdir(home);
	unsetenv("HOME");
	t = tdraw_theme_open();
	CHECK(t && tdraw_theme_load(t, &err) == 0 &&
	          tdraw_theme_color(t, "alert", 3) == 3,
	      "without any file, the theme is empty");
	tdraw_theme_free(t);
}

int main(void) {
	const char *tmp = getenv("TMPDIR");

	snprintf(dir, sizeof(dir), "%s/tdraw-theme-XXXXXX", tmp ? tmp : "/tmp");
	if (!mkdtemp(dir)) {
		perror("mkdtemp error");
		return 1;
	}
	path_in(sys, sizeof(sys), "sys");
	path_in(usr, sizeof(usr), "usr");
	path_in(late, sizeof(late), "late");
	mkdir(sys, 0700);
	mkdir(usr, 0700);
	test_parse();
	test_shipped();
	test_layers();
	test_watch();
	test_late_dir();
	test_deep_dir();
	test_public();
	drop(sys, "theme");
	rmdir(sys);
	rmdir(usr);
	rmdir(dir);
	return test_summary();
}
