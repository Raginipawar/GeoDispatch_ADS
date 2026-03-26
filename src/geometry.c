#include <stdlib.h>
#include <math.h>
#include "voronoi.h"

#define DEFAULT_THRESHOLD 5000000.0   /* 5 km² */
#define MAX_VERTS 4096

typedef struct { double x, y; } vec2_t;

/* Sutherland-Hodgman helpers */

static int is_inside(double px, double py, int edge,
                     double xmin, double ymin, double xmax, double ymax)
{
    if (edge == 0) return px >= xmin;
    if (edge == 1) return px <= xmax;
    if (edge == 2) return py >= ymin;
    return py <= ymax;
}

static vec2_t intersect(vec2_t a, vec2_t b, int edge,
                        double xmin, double ymin, double xmax, double ymax)
{
    double dx = b.x - a.x, dy = b.y - a.y, t = 0;
    vec2_t p = {0, 0};
    if (edge <= 1) {
        double wall = (edge == 0) ? xmin : xmax;
        if (fabs(dx) > 1e-15) t = (wall - a.x) / dx;
        p.x = wall;
        p.y = a.y + t * dy;
    } else {
        double wall = (edge == 2) ? ymin : ymax;
        if (fabs(dy) > 1e-15) t = (wall - a.y) / dy;
        p.x = a.x + t * dx;
        p.y = wall;
    }
    return p;
}

static int clip_one_edge(const vec2_t *in, int in_n, vec2_t *out, int edge,
                         double xmin, double ymin, double xmax, double ymax)
{
    int out_n = 0;
    for (int i = 0; i < in_n; i++) {
        vec2_t a = in[i], b = in[(i + 1) % in_n];
        int a_in = is_inside(a.x, a.y, edge, xmin, ymin, xmax, ymax);
        int b_in = is_inside(b.x, b.y, edge, xmin, ymin, xmax, ymax);

        if (a_in && b_in)        out[out_n++] = b;
        else if (a_in && !b_in)  out[out_n++] = intersect(a, b, edge, xmin, ymin, xmax, ymax);
        else if (!a_in && b_in) {out[out_n++] = intersect(a, b, edge, xmin, ymin, xmax, ymax);
                                 out[out_n++] = b; }
    }
    return out_n;
}

/* DCEL helpers */

static int collect_verts(face_t *f, vec2_t *buf, int max)
{
    if (!f || !f->outer_edge) return 0;
    int n = 0;
    half_edge_t *start = f->outer_edge, *he = start;
    do {
        if (he->origin && n < max) {
            buf[n].x = he->origin->x;
            buf[n].y = he->origin->y;
            n++;
        }
        he = he->next;
    } while (he && he != start && n < max);
    return n;
}

static void rebuild_ring(dcel_t *d, face_t *f, const vec2_t *pts, int n)
{
    if (n < 3) return;

    vertex_t    **v = (vertex_t **)   malloc(n * sizeof(vertex_t *));
    half_edge_t **e = (half_edge_t **)malloc(n * sizeof(half_edge_t *));

    for (int i = 0; i < n; i++) {
        v[i] = (vertex_t *)malloc(sizeof(vertex_t));
        v[i]->x = pts[i].x;
        v[i]->y = pts[i].y;
        v[i]->incident_edge = NULL;

        e[i] = (half_edge_t *)malloc(sizeof(half_edge_t));
        e[i]->origin      = v[i];
        e[i]->face         = f;
        e[i]->twin         = NULL;
        e[i]->next         = NULL;
        e[i]->prev         = NULL;
        e[i]->is_infinite  = 0;
        v[i]->incident_edge = e[i];
    }
    for (int i = 0; i < n; i++) {
        e[i]->next = e[(i + 1) % n];
        e[i]->prev = e[(i - 1 + n) % n];
    }
    f->outer_edge = e[0];

    if (d->nv + n > d->max_v) {
        d->max_v = (d->nv + n) * 2;
        d->vertices = (vertex_t **)realloc(d->vertices, d->max_v * sizeof(vertex_t *));
    }
    if (d->ne + n > d->max_e) {
        d->max_e = (d->ne + n) * 2;
        d->edges = (half_edge_t **)realloc(d->edges, d->max_e * sizeof(half_edge_t *));
    }
    for (int i = 0; i < n; i++) {
        d->vertices[d->nv++] = v[i];
        d->edges[d->ne++]    = e[i];
    }

    free(v);
    free(e);
}

/* clip_to_bbox */

void clip_to_bbox(dcel_t *d, double xmin, double ymin, double xmax, double ymax)
{
    if (!d) return;
    vec2_t a[MAX_VERTS], b[MAX_VERTS];

    for (int fi = 0; fi < d->nf; fi++) {
        face_t *f = d->faces[fi];
        if (!f || !f->outer_edge) continue;

        int n = collect_verts(f, a, MAX_VERTS);
        if (n < 3) continue;

        vec2_t *src = a, *dst = b;
        for (int edge = 0; edge < 4; edge++) {
            n = clip_one_edge(src, n, dst, edge, xmin, ymin, xmax, ymax);
            vec2_t *tmp = src; src = dst; dst = tmp;
            if (n < 3) break;
        }
        if (n >= 3) rebuild_ring(d, f, src, n);
    }
}

/* cell_area (Shoelace) */

double cell_area(dcel_t *d, int face_id)
{
    if (!d || face_id < 0 || face_id >= d->nf) return 0.0;
    face_t *f = d->faces[face_id];
    if (!f || !f->outer_edge) return 0.0;

    vec2_t v[MAX_VERTS];
    int n = collect_verts(f, v, MAX_VERTS);
    if (n < 3) { f->area = 0; return 0; }

    double sum = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        sum += v[i].x * v[j].y - v[j].x * v[i].y;
    }
    f->area = fabs(sum) * 0.5;
    return f->area;
}

void compute_all_areas(dcel_t *d)
{
    if (!d) return;
    for (int i = 0; i < d->nf; i++) cell_area(d, i);
}

/* flag_underserved (max-heap ranked) */

typedef struct { int id; double area; } heap_t;

static void sift_down(heap_t *h, int n, int i)
{
    while (1) {
        int big = i, l = 2*i+1, r = 2*i+2;
        if (l < n && h[l].area > h[big].area) big = l;
        if (r < n && h[r].area > h[big].area) big = r;
        if (big == i) return;
        heap_t tmp = h[i]; h[i] = h[big]; h[big] = tmp;
        i = big;
    }
}

int *flag_underserved(dcel_t *d, double threshold, int *out_count)
{
    if (!d || !out_count) { if (out_count) *out_count = 0; return NULL; }
    if (threshold <= 0) threshold = DEFAULT_THRESHOLD;

    heap_t *heap = (heap_t *)malloc(d->nf * sizeof(heap_t));
    int m = 0;

    for (int i = 0; i < d->nf; i++) {
        face_t *f = d->faces[i];
        if (!f) continue;
        if (f->area > threshold) {
            f->is_underserved = 1;
            heap[m].id   = i;
            heap[m].area = f->area;
            m++;
        } else {
            f->is_underserved = 0;
        }
    }
    if (m == 0) { free(heap); *out_count = 0; return NULL; }

    /* build max-heap — O(m) */
    for (int i = m/2 - 1; i >= 0; i--) sift_down(heap, m, i);

    /* extract sorted — O(m log m) */
    int *result = (int *)malloc(m * sizeof(int));
    int sz = m;
    for (int i = 0; i < m; i++) {
        result[i] = heap[0].id;
        heap[0] = heap[--sz];
        if (sz > 0) sift_down(heap, sz, 0);
    }

    free(heap);
    *out_count = m;
    return result;
}