/*
 * GeoDispatch — CLI entry point
 *
 * Reads facilities from stdin:
 *   first line  = n
 *   next n lines = "x y id"
 *
 * Commands:
 *   nearest  <qx> <qy>
 *   knn      <qx> <qy> <k>
 *   optimise <iterations> <threshold_metres>
 *   coverage
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "geodispatch.h"

#define MAX_FACILITIES 8192
#define PI  3.14159265358979323846
#define LAT0 18.5204
#define LON0 73.8567
#define R    6371000.0

static double cos_lat0;

static void init_proj(void) {
    cos_lat0 = cos(LAT0 * PI / 180.0);
}

static void xy_to_latlon(double x, double y, double *lat, double *lon) {
    *lat = (y / R) * (180.0 / PI) + LAT0;
    *lon = (x / (cos_lat0 * R)) * (180.0 / PI) + LON0;
}

static int read_facilities(point_t *pts, int max_n) {
    int n = 0;
    if (scanf("%d", &n) != 1 || n <= 0) return 0;
    if (n > max_n) n = max_n;
    for (int i = 0; i < n; i++)
        if (scanf("%lf %lf %d", &pts[i].x, &pts[i].y, &pts[i].id) != 3)
            return i;
    return n;
}

static void compute_bbox(point_t *pts, int n,
                         double *xmin, double *ymin,
                         double *xmax, double *ymax) {
    *xmin = *ymin =  1e15;
    *xmax = *ymax = -1e15;
    for (int i = 0; i < n; i++) {
        if (pts[i].x < *xmin) *xmin = pts[i].x;
        if (pts[i].y < *ymin) *ymin = pts[i].y;
        if (pts[i].x > *xmax) *xmax = pts[i].x;
        if (pts[i].y > *ymax) *ymax = pts[i].y;
    }
}

/* ── nearest ─────────────────────────────────────────────────────── */

static void cmd_nearest(point_t *pts, int n, double qx, double qy) {
    kdnode_t *root = kd_build(pts, n);
    point_t q = {qx, qy, -1};
    point_t res = kd_nearest(root, q);
    kd_free(root);
    printf("{\"id\":%d}", res.id);
}

/* ── knn ─────────────────────────────────────────────────────────── */

static void cmd_knn(point_t *pts, int n, double qx, double qy, int k) {
    kdnode_t *root = kd_build(pts, n);
    point_t q = {qx, qy, -1};
    int count = 0;
    point_t *res = kd_knn(root, q, k, &count);
    kd_free(root);
    printf("[");
    for (int i = 0; i < count; i++) {
        if (i) printf(",");
        printf("{\"id\":%d}", res[i].id);
    }
    printf("]");
    if (res) free(res);
}

/* ── optimise (Lloyd's relaxation) ──────────────────────────────── */

static void cmd_optimise(point_t *pts, int n, int iters, double thresh) {
    double xmin, ymin, xmax, ymax;
    compute_bbox(pts, n, &xmin, &ymin, &xmax, &ymax);
    double pad = 8000.0;

    lloyds_result_t *res = run_lloyds(pts, n,
                                      xmin - pad, ymin - pad,
                                      xmax + pad, ymax + pad,
                                      iters, thresh);
    printf("[");
    for (int i = 0; i < res->nmoves; i++) {
        if (i) printf(",");
        facility_move_t *m = &res->moves[i];
        double flat, flon, tlat, tlon;
        xy_to_latlon(m->from_x, m->from_y, &flat, &flon);
        xy_to_latlon(m->to_x,   m->to_y,   &tlat, &tlon);
        printf("{\"id\":%d,"
               "\"from_x\":%.4f,\"from_y\":%.4f,"
               "\"to_x\":%.4f,\"to_y\":%.4f,"
               "\"from_lat\":%.6f,\"from_lon\":%.6f,"
               "\"to_lat\":%.6f,\"to_lon\":%.6f}",
               m->site_id,
               m->from_x, m->from_y,
               m->to_x,   m->to_y,
               flat, flon, tlat, tlon);
    }
    printf("]");
    free_lloyds_result(res);
}

/* ── coverage map ────────────────────────────────────────────────── */

static void cmd_coverage(point_t *pts, int n) {
    double xmin, ymin, xmax, ymax;
    compute_bbox(pts, n, &xmin, &ymin, &xmax, &ymax);
    double pad = 8000.0;
    double bx0=xmin-pad, by0=ymin-pad, bx1=xmax+pad, by1=ymax+pad;

    double  poly[MAX_CELL_VERTS * 2];
    double *areas  = calloc(n, sizeof(double));
    int    *npts   = calloc(n, sizeof(int));
    double **polys = calloc(n, sizeof(double *));

    for (int i = 0; i < n; i++) {
        int k = voronoi_cell(i, pts, n, bx0, by0, bx1, by1, poly);
        if (k < 3) continue;
        double a = voronoi_poly_area(poly, k);
        if (a < 1e4) continue;
        areas[i] = a;
        npts[i]  = k;
        polys[i] = malloc(k * 2 * sizeof(double));
        memcpy(polys[i], poly, k * 2 * sizeof(double));
    }

    /* underserved threshold: mean + 0.8 * std */
    double mean_a = 0.0; int valid = 0;
    for (int i = 0; i < n; i++) if (areas[i] > 0) { mean_a += areas[i]; valid++; }
    if (valid > 0) mean_a /= valid;
    double std_a = 0.0;
    for (int i = 0; i < n; i++) if (areas[i] > 0) { double d=areas[i]-mean_a; std_a+=d*d; }
    if (valid > 0) std_a = sqrt(std_a / valid);
    double thresh = mean_a + 0.8 * std_a;

    printf("[");
    int first = 1;
    for (int i = 0; i < n; i++) {
        if (!polys[i]) continue;
        if (!first) printf(",");
        first = 0;
        printf("{\"site_id\":%d,\"area\":%.2f,\"is_underserved\":%d,\"polygon\":[",
               pts[i].id, areas[i], areas[i] > thresh ? 1 : 0);
        for (int j = 0; j < npts[i]; j++) {
            double lat, lon;
            xy_to_latlon(polys[i][2*j], polys[i][2*j+1], &lat, &lon);
            if (j) printf(",");
            printf("[%.6f,%.6f]", lon, lat);
        }
        { double lat, lon;
          xy_to_latlon(polys[i][0], polys[i][1], &lat, &lon);
          printf(",[%.6f,%.6f]", lon, lat); }
        printf("]}");
        free(polys[i]);
    }
    printf("]");

    free(areas); free(npts); free(polys);
}

/* ── main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: geodispatch <nearest|knn|optimise|coverage> [args]\n");
        return 1;
    }

    init_proj();

    point_t *pts = malloc(MAX_FACILITIES * sizeof(point_t));
    if (!pts) { fprintf(stderr, "out of memory\n"); return 1; }

    int n = read_facilities(pts, MAX_FACILITIES);
    if (n <= 0) { printf("{}"); free(pts); return 0; }

    const char *cmd = argv[1];

    if      (strcmp(cmd, "nearest")  == 0 && argc >= 4)
        cmd_nearest(pts, n, atof(argv[2]), atof(argv[3]));
    else if (strcmp(cmd, "knn")      == 0 && argc >= 5)
        cmd_knn(pts, n, atof(argv[2]), atof(argv[3]), atoi(argv[4]));
    else if (strcmp(cmd, "optimise") == 0 && argc >= 4)
        cmd_optimise(pts, n, atoi(argv[2]), atof(argv[3]));
    else if (strcmp(cmd, "coverage") == 0)
        cmd_coverage(pts, n);
    else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        printf("{}");
        free(pts); return 1;
    }

    free(pts);
    return 0;
}
