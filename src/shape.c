#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <tundra/draw.h>

#define PI 3.14159265358979323846
#define FLAT_TOL 0.1
#define MITER_LIMIT 4.0
#define SEGS_MAX 1024

struct pt {
	double x, y;
};

struct path {
	struct pt *p;
	int n, cap;
	int *start;
	int nc, ccap;
};

struct edge {
	double x0, y0, x1, y1;
	int dir;
};

static void *room(void *p, int *cap, int need, size_t size) {
	int c = *cap ? *cap : 16;
	void *q;

	if (need <= *cap)
		return p;
	while (c < need)
		c *= 2;
	if (!(q = realloc(p, (size_t)c * size)))
		return NULL;
	*cap = c;
	return q;
}

static int add_point(struct path *pa, double x, double y) {
	struct pt *p = room(pa->p, &pa->cap, pa->n + 1, sizeof(*p));

	if (!p)
		return -1;
	pa->p = p;
	pa->p[pa->n++] = (struct pt){x, y};
	return 0;
}

static int new_contour(struct path *pa) {
	int *s = room(pa->start, &pa->ccap, pa->nc + 1, sizeof(*s));

	if (!s)
		return -1;
	pa->start = s;
	pa->start[pa->nc++] = pa->n;
	return 0;
}

static double signed_area(const struct pt *p, int n) {
	double a = 0;

	for (int i = 0; i < n; i++) {
		const struct pt *q = &p[(i + 1) % n];

		a += p[i].x * q->y - q->x * p[i].y;
	}
	return a / 2;
}

static int add_piece(struct path *pa, const struct pt *p, int n) {
	int rev = signed_area(p, n) < 0;

	if (new_contour(pa) < 0)
		return -1;
	for (int i = 0; i < n; i++) {
		const struct pt *q = &p[rev ? n - 1 - i : i];

		if (add_point(pa, q->x, q->y) < 0)
			return -1;
	}
	return 0;
}

static int segments(double r, double sweep, int min) {
	double step = r > FLAT_TOL ? 2 * acos(1 - FLAT_TOL / r) : PI / 2;
	double n = ceil(fabs(sweep) / step);

	return n < min ? min : n > SEGS_MAX ? SEGS_MAX : (int)n;
}

static int add_arc(struct path *pa, double cx, double cy, double rx, double ry,
                   double a, double b, int min) {
	int n = segments(rx > ry ? rx : ry, b - a, min);

	for (int i = 0; i <= n; i++) {
		double t = a + (b - a) * i / n;

		if (add_point(pa, cx + rx * cos(t), cy + ry * sin(t)) < 0)
			return -1;
	}
	return 0;
}

static int stroke(struct path *pa, const struct pt *pts, int n, int closed,
                  double w) {
	double hw = w / 2;
	struct pt *p = malloc(sizeof(*p) * (size_t)(n ? n : 1));
	int m = 0, r = -1;

	if (!p)
		return -1;
	for (int i = 0; i < n; i++)
		if (!m || pts[i].x != p[m - 1].x || pts[i].y != p[m - 1].y)
			p[m++] = pts[i];
	if (closed && m > 1 && p[0].x == p[m - 1].x && p[0].y == p[m - 1].y)
		m--;
	if (m < 2) {
		r = 0;
		goto out;
	}
	for (int i = 0; i < (closed ? m : m - 1); i++) {
		struct pt a = p[i], b = p[(i + 1) % m];
		double len = hypot(b.x - a.x, b.y - a.y);
		double dx = (b.x - a.x) / len, dy = (b.y - a.y) / len;
		double nx = -dy * hw, ny = dx * hw;

		if (!closed && i == 0) {
			a.x -= dx * hw;
			a.y -= dy * hw;
		}
		if (!closed && i == m - 2) {
			b.x += dx * hw;
			b.y += dy * hw;
		}
		struct pt quad[4] = {{a.x + nx, a.y + ny},
		                     {b.x + nx, b.y + ny},
		                     {b.x - nx, b.y - ny},
		                     {a.x - nx, a.y - ny}};

		if (add_piece(pa, quad, 4) < 0)
			goto out;
	}
	for (int i = closed ? 0 : 1; i < (closed ? m : m - 1); i++) {
		struct pt o = p[(i + m - 1) % m], v = p[i], q = p[(i + 1) % m];
		double la = hypot(v.x - o.x, v.y - o.y),
		       lb = hypot(q.x - v.x, q.y - v.y);
		double ax = (v.x - o.x) / la, ay = (v.y - o.y) / la;
		double bx = (q.x - v.x) / lb, by = (q.y - v.y) / lb;
		double cross = ax * by - ay * bx, dot = ax * bx + ay * by;
		double s = cross > 0 ? -hw : hw;
		struct pt join[4] = {v,
		                     {v.x - ay * s, v.y + ax * s},
		                     {0, 0},
		                     {v.x - by * s, v.y + bx * s}};
		int k = 3;

		if (fabs(cross) < 1e-12)
			continue;
		if (1 + dot > 1e-9 && sqrt(2 / (1 + dot)) <= MITER_LIMIT) {
			join[2] = (struct pt){v.x + (-ay - by) * s / (1 + dot),
			                      v.y + (ax + bx) * s / (1 + dot)};
			k = 4;
		} else {
			join[2] = join[3];
		}
		if (add_piece(pa, join, k) < 0)
			goto out;
	}
	r = 0;
out:
	free(p);
	return r;
}

static int cmp_double(const void *a, const void *b) {
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

struct active {
	double x;
	const struct edge *e;
};

static int cmp_active(const void *a, const void *b) {
	return cmp_double(&((const struct active *)a)->x,
	                  &((const struct active *)b)->x);
}

static pixman_line_fixed_t line_of(const struct edge *e) {
	pixman_line_fixed_t l = {
	    {pixman_double_to_fixed(e->x0), pixman_double_to_fixed(e->y0)},
	    {pixman_double_to_fixed(e->x1), pixman_double_to_fixed(e->y1)}};

	return l;
}

static int cmp_edge(const void *a, const void *b) {
	return cmp_double(&((const struct edge *)a)->y0,
	                  &((const struct edge *)b)->y0);
}

static int sweep(const struct path *pa, pixman_trapezoid_t **out, int *nout) {
	struct edge *e = malloc(sizeof(*e) * (size_t)(pa->n ? pa->n : 1));
	struct active *act = NULL;
	double *ys = NULL;
	pixman_trapezoid_t *t = NULL;
	int ne = 0, ny = 0, ycap = 0, nt = 0, tcap = 0, na = 0, next = 0, r = -1;

	if (!e)
		return -1;
	for (int c = 0; c < pa->nc; c++) {
		int s = pa->start[c], end = c + 1 < pa->nc ? pa->start[c + 1] : pa->n;

		for (int i = s; i < end; i++) {
			struct pt a = pa->p[i], b = pa->p[i + 1 < end ? i + 1 : s];

			if (a.y == b.y)
				continue;
			e[ne++] = a.y < b.y ? (struct edge){a.x, a.y, b.x, b.y, 1}
			                    : (struct edge){b.x, b.y, a.x, a.y, -1};
		}
	}
	qsort(e, (size_t)ne, sizeof(*e), cmp_edge);
	if (!(ys = room(NULL, &ycap, 2 * ne + 1, sizeof(*ys))) ||
	    !(act = malloc(sizeof(*act) * (size_t)(ne ? ne : 1))))
		goto out;
	for (int i = 0; i < ne; i++) {
		ys[ny++] = e[i].y0;
		ys[ny++] = e[i].y1;
	}
	for (int i = 0; i < ne; i++)
		for (int j = i + 1; j < ne && e[j].y0 < e[i].y1; j++) {
			const struct edge *a = &e[i], *b = &e[j];
			double ax = a->x1 - a->x0, ay = a->y1 - a->y0;
			double bx = b->x1 - b->x0, by = b->y1 - b->y0;
			double d = ax * by - ay * bx, u, v;
			double *q;

			if (fabs(d) < 1e-12)
				continue;
			u = ((b->x0 - a->x0) * by - (b->y0 - a->y0) * bx) / d;
			v = ((b->x0 - a->x0) * ay - (b->y0 - a->y0) * ax) / d;
			if (u <= 0 || u >= 1 || v <= 0 || v >= 1)
				continue;
			if (!(q = room(ys, &ycap, ny + 1, sizeof(*ys))))
				goto out;
			ys = q;
			ys[ny++] = a->y0 + u * ay;
		}
	qsort(ys, (size_t)ny, sizeof(*ys), cmp_double);
	for (int k = 0; k + 1 < ny; k++) {
		double ya = ys[k], yb = ys[k + 1], ym = (ya + yb) / 2;
		pixman_fixed_t top = pixman_double_to_fixed(ya);
		pixman_fixed_t bottom = pixman_double_to_fixed(yb);
		const struct edge *left = NULL;
		int m = 0, wind = 0;

		for (int i = 0; i < na; i++)
			if (act[i].e->y1 > ym)
				act[m++] = act[i];
		na = m;
		for (; next < ne && e[next].y0 < ym; next++)
			if (e[next].y1 > ym)
				act[na++].e = &e[next];
		if (top >= bottom || na < 2)
			continue;
		for (int i = 0; i < na; i++) {
			const struct edge *a = act[i].e;

			act[i].x = a->x0 + (a->x1 - a->x0) * (ym - a->y0) / (a->y1 - a->y0);
		}
		qsort(act, (size_t)na, sizeof(*act), cmp_active);
		for (int i = 0; i < na; i++) {
			int was = wind;

			wind += act[i].e->dir;
			if (!was && wind)
				left = act[i].e;
			if (was && !wind) {
				pixman_trapezoid_t *q = room(t, &tcap, nt + 1, sizeof(*t));

				if (!q)
					goto out;
				t = q;
				t[nt++] = (pixman_trapezoid_t){top, bottom, line_of(left),
				                               line_of(act[i].e)};
			}
		}
	}
	*out = t;
	*nout = nt;
	t = NULL;
	r = 0;
out:
	free(e);
	free(act);
	free(ys);
	free(t);
	return r;
}

static int paint(struct tdraw_canvas *c, const struct path *pa,
                 const struct tdraw_pen *pen) {
	pixman_trapezoid_t *t = NULL;
	pixman_color_t pc = tdraw_color(pen->color);
	pixman_image_t *src;
	int nt = 0;

	if (sweep(pa, &t, &nt) < 0)
		return TDRAW_NOMEM;
	if (nt && (src = pixman_image_create_solid_fill(&pc))) {
		pixman_composite_trapezoids(PIXMAN_OP_OVER, src, c->img,
		                            pen->smooth ? PIXMAN_a8 : PIXMAN_a1, 0, 0,
		                            0, 0, nt, t);
		pixman_image_unref(src);
	} else if (nt) {
		free(t);
		return TDRAW_NOMEM;
	}
	free(t);
	return 0;
}

static int draw(struct tdraw_canvas *c, struct path *pa, int closed,
                const struct tdraw_pen *pen) {
	struct path st = {0};
	int r;

	if (!(pen->color >> 24))
		r = 0;
	else if (pen->width == 0)
		r = paint(c, pa, pen);
	else if (stroke(&st, pa->p, pa->n, closed, pen->width) < 0)
		r = TDRAW_NOMEM;
	else
		r = paint(c, &st, pen);
	free(st.p);
	free(st.start);
	free(pa->p);
	free(pa->start);
	return r;
}

static int coord_ok(double v) {
	return isfinite(v) && fabs(v) <= TDRAW_COORD_MAX;
}

static int pen_ok(const struct tdraw_pen *p) {
	return p && isfinite(p->width) && p->width >= 0 &&
	       p->width <= TDRAW_COORD_MAX;
}

TDRAW_API int tdraw_line(struct tdraw_canvas *c, float x0, float y0, float x1,
                         float y1, const struct tdraw_pen *p) {
	struct path pa = {0};

	if (!pen_ok(p) || p->width == 0 || !coord_ok(x0) || !coord_ok(y0) ||
	    !coord_ok(x1) || !coord_ok(y1))
		return TDRAW_USAGE;
	if (new_contour(&pa) < 0 || add_point(&pa, x0, y0) < 0 ||
	    add_point(&pa, x1, y1) < 0) {
		free(pa.p);
		free(pa.start);
		return TDRAW_NOMEM;
	}
	return draw(c, &pa, 0, p);
}

TDRAW_API int tdraw_rect(struct tdraw_canvas *c, float x, float y, float w,
                         float h, float radius, const struct tdraw_pen *p) {
	struct path pa = {0};
	double r = radius, x1 = (double)x + w, y1 = (double)y + h;
	int bad;

	if (!pen_ok(p) || !coord_ok(x) || !coord_ok(y) || !coord_ok(x1) ||
	    !coord_ok(y1) || !(w >= 0) || !(h >= 0) || !coord_ok(radius) ||
	    radius < 0)
		return TDRAW_USAGE;
	if (r > w / 2.0)
		r = w / 2.0;
	if (r > h / 2.0)
		r = h / 2.0;
	if (r <= 0)
		bad = new_contour(&pa) < 0 || add_point(&pa, x, y) < 0 ||
		      add_point(&pa, x1, y) < 0 || add_point(&pa, x1, y1) < 0 ||
		      add_point(&pa, x, y1) < 0;
	else
		bad = new_contour(&pa) < 0 ||
		      add_arc(&pa, x + r, y + r, r, r, PI, 1.5 * PI, 2) < 0 ||
		      add_arc(&pa, x1 - r, y + r, r, r, 1.5 * PI, 2 * PI, 2) < 0 ||
		      add_arc(&pa, x1 - r, y1 - r, r, r, 0, 0.5 * PI, 2) < 0 ||
		      add_arc(&pa, x + r, y1 - r, r, r, 0.5 * PI, PI, 2) < 0;
	if (bad) {
		free(pa.p);
		free(pa.start);
		return TDRAW_NOMEM;
	}
	return draw(c, &pa, 1, p);
}

TDRAW_API int tdraw_ellipse(struct tdraw_canvas *c, float cx, float cy,
                            float rx, float ry, const struct tdraw_pen *p) {
	struct path pa = {0};

	if (!pen_ok(p) || !coord_ok(cx) || !coord_ok(cy) || !coord_ok(rx) ||
	    !coord_ok(ry) || rx < 0 || ry < 0)
		return TDRAW_USAGE;
	if (new_contour(&pa) < 0 ||
	    add_arc(&pa, cx, cy, rx, ry, 0, 2 * PI, 8) < 0) {
		free(pa.p);
		free(pa.start);
		return TDRAW_NOMEM;
	}
	pa.n--;
	return draw(c, &pa, 1, p);
}

TDRAW_API int tdraw_arc(struct tdraw_canvas *c, float cx, float cy, float rx,
                        float ry, float from, float to,
                        const struct tdraw_pen *p) {
	struct path pa = {0};
	double a = from * PI / 180, b = to * PI / 180;
	int whole, bad;

	if (!pen_ok(p) || !coord_ok(cx) || !coord_ok(cy) || !coord_ok(rx) ||
	    !coord_ok(ry) || rx < 0 || ry < 0 || !isfinite(from) || !isfinite(to) ||
	    fabs(from) > 1e6 || fabs(to) > 1e6)
		return TDRAW_USAGE;
	whole = fabs(b - a) >= 2 * PI;
	if (whole)
		b = a + 2 * PI;
	bad = new_contour(&pa) < 0 ||
	      (!whole && p->width == 0 && add_point(&pa, cx, cy) < 0) ||
	      add_arc(&pa, cx, cy, rx, ry, a, b, 1) < 0;
	if (bad) {
		free(pa.p);
		free(pa.start);
		return TDRAW_NOMEM;
	}
	if (whole)
		pa.n--;
	return draw(c, &pa, whole || p->width == 0, p);
}

static int from_points(struct path *pa, const float *xy, int n) {
	if (new_contour(pa) < 0)
		return TDRAW_NOMEM;
	for (int i = 0; i < n; i++) {
		if (!coord_ok(xy[2 * i]) || !coord_ok(xy[2 * i + 1]))
			return TDRAW_USAGE;
		if (add_point(pa, xy[2 * i], xy[2 * i + 1]) < 0)
			return TDRAW_NOMEM;
	}
	return 0;
}

TDRAW_API int tdraw_polygon(struct tdraw_canvas *c, const float *xy, int n,
                            const struct tdraw_pen *p) {
	struct path pa = {0};
	int r;

	if (!pen_ok(p) || !xy || n < 3)
		return TDRAW_USAGE;
	if ((r = from_points(&pa, xy, n)) < 0) {
		free(pa.p);
		free(pa.start);
		return r;
	}
	return draw(c, &pa, 1, p);
}

TDRAW_API int tdraw_polyline(struct tdraw_canvas *c, const float *xy, int n,
                             const struct tdraw_pen *p) {
	struct path pa = {0};
	int r;

	if (!pen_ok(p) || p->width == 0 || !xy || n < 2)
		return TDRAW_USAGE;
	if ((r = from_points(&pa, xy, n)) < 0) {
		free(pa.p);
		free(pa.start);
		return r;
	}
	return draw(c, &pa, 0, p);
}
