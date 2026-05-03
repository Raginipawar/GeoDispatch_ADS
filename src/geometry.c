#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "voronoi.h"   /* includes kd.h for point_t */

#define DEFAULT_THRESHOLD_SQ_M 5000000.0  /* 5 km^2 */
#define MAX_CLIP_VERTS 4096

/* Internal helpers */

typedef struct {
    double x;
    double y;
} vec2_t;

/* Sutherland-Hodgman helpers 
 *
 * edge_code: 0 = left, 1 = right, 2 = bottom, 3 = top
 */

static int is_inside(double px, double py, int edge_code,
                     double xmin, double ymin, double xmax, double ymax)
{
    switch (edge_code) {
        case 0: return px >= xmin;
        case 1: return px <= xmax;
        case 2: return py >= ymin;
        case 3: return py <= ymax;
    }
    return 0;
}

static vec2_t intersect_edge(vec2_t a, vec2_t b, int edge_code,
                             double xmin, double ymin,
                             double xmax, double ymax)
{
    vec2_t p = {0.0, 0.0};
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double t  = 0.0;

    switch (edge_code) {
        case 0: /* left   x = xmin */
            if (fabs(dx) > 1e-15) t = (xmin - a.x) / dx;
            p.x = xmin;
            p.y = a.y + t * dy;
            break;
        case 1: /* right  x = xmax */
            if (fabs(dx) > 1e-15) t = (xmax - a.x) / dx;
            p.x = xmax;
            p.y = a.y + t * dy;
            break;
        case 2: /* bottom y = ymin */
            if (fabs(dy) > 1e-15) t = (ymin - a.y) / dy;
            p.x = a.x + t * dx;
            p.y = ymin;
            break;
        case 3: /* top    y = ymax */
            if (fabs(dy) > 1e-15) t = (ymax - a.y) / dy;
            p.x = a.x + t * dx;
            p.y = ymax;
            break;
    }
    return p;
}

/*
 * Clip polygon (in_pts, in_n) against one axis-aligned edge.
 * Writes result into out_pts, returns new vertex count.
 */
static int clip_against_edge(const vec2_t *in_pts, int in_n,
                             vec2_t *out_pts,
                             int edge_code,
                             double xmin, double ymin,
                             double xmax, double ymax)
{
    int out_n = 0;
    if (in_n == 0) return 0;

    for (int i = 0; i < in_n; i++) {
        vec2_t a = in_pts[i];
        vec2_t b = in_pts[(i + 1) % in_n];

        int a_in = is_inside(a.x, a.y, edge_code, xmin, ymin, xmax, ymax);
        int b_in = is_inside(b.x, b.y, edge_code, xmin, ymin, xmax, ymax);

        if (a_in && b_in) {
            /* Both inside -> emit B */
            out_pts[out_n++] = b;
        } else if (a_in && !b_in) {
            /* A inside, B outside -> emit intersection */
            out_pts[out_n++] = intersect_edge(a, b, edge_code,
                                              xmin, ymin, xmax, ymax);
        } else if (!a_in && b_in) {
            /* A outside, B inside -> emit intersection then B */
            out_pts[out_n++] = intersect_edge(a, b, edge_code,
                                              xmin, ymin, xmax, ymax);
            out_pts[out_n++] = b;
        }
        /* Both outside -> emit nothing */

        if (out_n >= MAX_CLIP_VERTS - 2) break;
    }
    return out_n;
}

/*
 * Walk the face's outer_edge ring and collect origin vertices.
 */
static int collect_face_vertices(face_t *f, vec2_t *buf, int max_n)
{
    if (!f || !f->outer_edge) return 0;

    int n = 0;
    half_edge_t *start = f->outer_edge;
    half_edge_t *he    = start;
    int guard = 100000;
    do {
        if (he->origin && n < max_n) {
            buf[n].x = he->origin->x;
            buf[n].y = he->origin->y;
            n++;
        }
        he = he->next;
        if (--guard <= 0) break;
    } while (he && he != start && n < max_n);

    return n;
}

/*
 * Rebuild a face's half-edge ring from a clipped polygon.
 * Allocates new vertices and half-edges, appends them to the DCEL.
 *
 * NOTE: twin pointers are left NULL here. P3/P5 must re-stitch
 * twin linkages across adjacent faces during integration.
 */
static void rebuild_face_ring(dcel_t *d, face_t *f,
                              const vec2_t *pts, int n)
{
    if (n < 3) return;

    /* Allocate new vertices */
    vertex_t **new_verts = (vertex_t **)malloc((size_t)n * sizeof(vertex_t *));
    if (!new_verts) return;
    for (int i = 0; i < n; i++) {
        new_verts[i] = (vertex_t *)malloc(sizeof(vertex_t));
        new_verts[i]->x = pts[i].x;
        new_verts[i]->y = pts[i].y;
        new_verts[i]->incident_edge = NULL;
    }

    /* Allocate new half-edges for this face ring */
    half_edge_t **new_edges = (half_edge_t **)malloc((size_t)n * sizeof(half_edge_t *));
    if (!new_edges) {
        for (int i = 0; i < n; i++) free(new_verts[i]);
        free(new_verts);
        return;
    }
    for (int i = 0; i < n; i++) {
        new_edges[i] = (half_edge_t *)malloc(sizeof(half_edge_t));
        new_edges[i]->origin      = new_verts[i];
        new_edges[i]->face        = f;
        new_edges[i]->twin        = NULL;
        new_edges[i]->is_infinite = 0;
        new_verts[i]->incident_edge = new_edges[i];
    }

    /* Link next / prev in circular order */
    for (int i = 0; i < n; i++) {
        new_edges[i]->next = new_edges[(i + 1) % n];
        new_edges[i]->prev = new_edges[(i - 1 + n) % n];
    }

    f->outer_edge = new_edges[0];

    /* Grow DCEL arrays and append new elements */
    int old_nv = d->nv;
    int old_ne = d->ne;
    d->vertices = (vertex_t **)realloc(d->vertices,
                      (size_t)(old_nv + n) * sizeof(vertex_t *));
    d->edges    = (half_edge_t **)realloc(d->edges,
                      (size_t)(old_ne + n) * sizeof(half_edge_t *));
    for (int i = 0; i < n; i++) {
        d->vertices[old_nv + i] = new_verts[i];
        d->edges[old_ne + i]    = new_edges[i];
    }
    d->nv += n;
    d->ne += n;

    free(new_verts);
    free(new_edges);
}


/* PUBLIC API */

/*
 * clip_to_bbox — Sutherland-Hodgman clipping of every DCEL face
 *
 * Clips all faces to the axis-aligned rectangle [xmin,xmax] x [ymin,ymax].
 * After this call every face is a closed bounded polygon and all
 * is_infinite flags are cleared.
 *
 * Complexity: O(V_total) where V_total = sum of per-face vertex counts.
 */
void clip_to_bbox(dcel_t *d, double xmin, double ymin,
                  double xmax, double ymax)
{
    if (!d) return;

    vec2_t buf_a[MAX_CLIP_VERTS];
    vec2_t buf_b[MAX_CLIP_VERTS];

    for (int fi = 0; fi < d->nf; fi++) {
        face_t *f = d->faces[fi];
        if (!f || !f->outer_edge) continue;

        int n = collect_face_vertices(f, buf_a, MAX_CLIP_VERTS);
        if (n < 3) continue;

        /* Clip against 4 edges: left, right, bottom, top */
        vec2_t *src = buf_a;
        vec2_t *dst = buf_b;

        for (int edge = 0; edge < 4; edge++) {
            int new_n = clip_against_edge(src, n, dst, edge,
                                          xmin, ymin, xmax, ymax);
            vec2_t *tmp = src; src = dst; dst = tmp;
            n = new_n;
            if (n < 3) break;
        }

        if (n < 3) continue;

        /* src now holds the final clipped polygon */
        rebuild_face_ring(d, f, src, n);
    }
}

/*
 * cell_area — Shoelace formula for a single Voronoi cell
 *
 * Walks the face's half-edge ring, collects vertices, computes
 * the area, stores it in face->area, and returns it.
 *
 * Units: square metres (coords are metric Cartesian from P2's projection).
 * Complexity: O(v) where v = vertex count of the cell.
 */
double cell_area(dcel_t *d, int face_id)
{
    if (!d || face_id < 0 || face_id >= d->nf) return 0.0;

    face_t *f = d->faces[face_id];
    if (!f || !f->outer_edge) {
        if (f) f->area = 0.0;
        return 0.0;
    }

    vec2_t verts[MAX_CLIP_VERTS];
    int n = collect_face_vertices(f, verts, MAX_CLIP_VERTS);
    if (n < 3) {
        f->area = 0.0;
        return 0.0;
    }

    /* Shoelace: area = 0.5 * |sum_i (x_i * y_{i+1} - x_{i+1} * y_i)| */
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        sum += verts[i].x * verts[j].y;
        sum -= verts[j].x * verts[i].y;
    }
    f->area = fabs(sum) * 0.5;
    return f->area;
}

/*
 * compute_all_areas — populate face->area for every face in the DCEL.
 * Call once after clip_to_bbox().
 */
void compute_all_areas(dcel_t *d)
{
    if (!d) return;
    for (int i = 0; i < d->nf; i++) {
        cell_area(d, i);
    }
}


/* Max-heap helpers for underserved ranking */

typedef struct {
    int    face_id;
    double area;
} heap_entry_area_t;

static void sift_down_area(heap_entry_area_t *h, int n, int i)
{
    while (1) {
        int largest = i;
        int l = 2 * i + 1;
        int r = 2 * i + 2;
        if (l < n && h[l].area > h[largest].area) largest = l;
        if (r < n && h[r].area > h[largest].area) largest = r;
        if (largest == i) break;
        heap_entry_area_t tmp = h[i];
        h[i] = h[largest];
        h[largest] = tmp;
        i = largest;
    }
}

static void build_max_heap_area(heap_entry_area_t *h, int n)
{
    for (int i = n / 2 - 1; i >= 0; i--) {
        sift_down_area(h, n, i);
    }
}

/*
 * flag_underserved — Identify and rank cells exceeding the area threshold
 *
 * For every face: if face->area > threshold, sets face->is_underserved = 1,
 * otherwise sets it to 0.
 *
 * Returns a malloc'd array of face IDs sorted largest-area-first
 * (extracted via heap-sort from a max-heap). Caller must free().
 *
 * Complexity: O(n) scan + O(m log m) heap-sort, m = underserved count.
 */
int *flag_underserved(dcel_t *d, double threshold, int *out_count)
{
    if (!d || !out_count) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    if (threshold <= 0.0) threshold = DEFAULT_THRESHOLD_SQ_M;

    /* First pass: collect underserved faces */
    heap_entry_area_t *heap = (heap_entry_area_t *)malloc(
                                  (size_t)d->nf * sizeof(heap_entry_area_t));
    if (!heap) { *out_count = 0; return NULL; }
    int m = 0;

    for (int i = 0; i < d->nf; i++) {
        face_t *f = d->faces[i];
        if (!f) continue;

        if (f->area > threshold) {
            f->is_underserved = 1;
            heap[m].face_id = i;
            heap[m].area    = f->area;
            m++;
        } else {
            f->is_underserved = 0;
        }
    }

    if (m == 0) {
        free(heap);
        *out_count = 0;
        return NULL;
    }

    /* Build max-heap, then extract in descending order (heap-sort) */
    build_max_heap_area(heap, m);

    int *result = (int *)malloc((size_t)m * sizeof(int));
    int size = m;
    for (int i = 0; i < m; i++) {
        result[i] = heap[0].face_id;
        heap[0] = heap[size - 1];
        size--;
        if (size > 0) sift_down_area(heap, size, 0);
    }

    free(heap);
    *out_count = m;
    return result;
}
