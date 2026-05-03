/*
 * GeoDispatch — CLI entry point (P5)
 *
 * Usage:  geodispatch <command> [args]
 *   Facilities are fed via stdin:
 *     first line  = n (count)
 *     next n lines = "x y id"
 *
 * Commands:
 *   nearest <qx> <qy>
 *   knn     <qx> <qy> <k>
 *   optimise <iterations> <threshold_metres>
 *   coverage
 *
 * All output is JSON on stdout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "geodispatch.h"

#define MAX_FACILITIES 8192
#define PI 3.14159265358979323846

/* Pune equirectangular projection constants (must match data_loader) */
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

/* ── stdin reader ────────────────────────────────────────────────── */

static int read_facilities(point_t *pts, int max_n) {
    int n = 0;
    if (scanf("%d", &n) != 1 || n <= 0) return 0;
    if (n > max_n) n = max_n;
    for (int i = 0; i < n; i++) {
        if (scanf("%lf %lf %d", &pts[i].x, &pts[i].y, &pts[i].id) != 3)
            return i;
    }
    return n;
}

/* ── bbox helper ─────────────────────────────────────────────────── */

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

/* ── commands ────────────────────────────────────────────────────── */

static void cmd_nearest(point_t *pts, int n, double qx, double qy) {
    kdnode_t *root = kd_build(pts, n);
    point_t q = {qx, qy, -1};
    point_t res = kd_nearest(root, q);
    kd_free(root);
    printf("{\"id\":%d}", res.id);
}

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

static void cmd_optimise(point_t *pts, int n, int iters, double thresh) {
    double xmin, ymin, xmax, ymax;
    compute_bbox(pts, n, &xmin, &ymin, &xmax, &ymax);
    double pad = 8000.0;

    kdnode_t *root = kd_build(pts, n);
    dcel_t   *dcel = voronoi_build(pts, n);
    clip_to_bbox(dcel, xmin - pad, ymin - pad, xmax + pad, ymax + pad);
    compute_all_areas(dcel);

    lloyds_result_t *res = run_lloyds(&dcel, &root, pts, n, iters, thresh);

    printf("[");
    for (int i = 0; i < res->nmoves; i++) {
        if (i) printf(",");
        facility_move_t *m = &res->moves[i];
        double flat, flon, tlat, tlon;
        xy_to_latlon(m->from.x, m->from.y, &flat, &flon);
        xy_to_latlon(m->to.x,   m->to.y,   &tlat, &tlon);
        printf("{\"id\":%d,"
               "\"from_x\":%.4f,\"from_y\":%.4f,"
               "\"to_x\":%.4f,\"to_y\":%.4f,"
               "\"from_lat\":%.6f,\"from_lon\":%.6f,"
               "\"to_lat\":%.6f,\"to_lon\":%.6f}",
               m->site_id,
               m->from.x, m->from.y,
               m->to.x,   m->to.y,
               flat, flon, tlat, tlon);
    }
    printf("]");

    free_lloyds_result(res);
    voronoi_free(dcel);
    kd_free(root);
}

static void cmd_coverage(point_t *pts, int n) {
    double xmin, ymin, xmax, ymax;
    compute_bbox(pts, n, &xmin, &ymin, &xmax, &ymax);
    double pad = 8000.0;

    dcel_t *dcel = voronoi_build(pts, n);
    clip_to_bbox(dcel, xmin - pad, ymin - pad, xmax + pad, ymax + pad);
    compute_all_areas(dcel);

    /* Adaptive threshold: mean + 0.8 * std */
    double mean_a = 0.0, std_a = 0.0;
    int valid = 0;
    for (int i = 0; i < dcel->nf; i++) {
        if (dcel->faces[i] && dcel->faces[i]->area > 0.0) {
            mean_a += dcel->faces[i]->area;
            valid++;
        }
    }
    if (valid > 0) mean_a /= valid;
    for (int i = 0; i < dcel->nf; i++) {
        if (dcel->faces[i] && dcel->faces[i]->area > 0.0) {
            double d = dcel->faces[i]->area - mean_a;
            std_a += d * d;
        }
    }
    if (valid > 0) std_a = sqrt(std_a / valid);

    int uc = 0;
    int *under = flag_underserved(dcel, mean_a + 0.8 * std_a, &uc);
    if (under) free(under);

    coverage_map_t *cmap = get_coverage_map(dcel);

    printf("[");
    int first = 1;
    if (cmap) {
        for (int i = 0; i < cmap->ncells; i++) {
            coverage_cell_t *cell = &cmap->cells[i];
            if (cell->num_points < 3) continue;

            if (!first) printf(",");
            first = 0;

            printf("{\"site_id\":%d,\"area\":%.2f,\"is_underserved\":%d,\"polygon\":[",
                   cell->site_id, cell->area, cell->is_underserved);

            for (int j = 0; j < cell->num_points; j++) {
                double lat, lon;
                xy_to_latlon(cell->polygon_coords[j * 2],
                             cell->polygon_coords[j * 2 + 1],
                             &lat, &lon);
                if (j) printf(",");
                printf("[%.6f,%.6f]", lon, lat);   /* GeoJSON: [lon, lat] */
            }
            /* Close the ring */
            {
                double lat, lon;
                xy_to_latlon(cell->polygon_coords[0],
                             cell->polygon_coords[1],
                             &lat, &lon);
                printf(",[%.6f,%.6f]", lon, lat);
            }
            printf("]}");
        }
        free_coverage_map(cmap);
    }
    printf("]");

    voronoi_free(dcel);
}

/* ── main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: geodispatch <nearest|knn|optimise|coverage> [args]\n");
        return 1;
    }

    init_proj();

    point_t *pts = (point_t *)malloc(MAX_FACILITIES * sizeof(point_t));
    if (!pts) { fprintf(stderr, "out of memory\n"); return 1; }

    int n = read_facilities(pts, MAX_FACILITIES);
    if (n <= 0) { printf("{}"); free(pts); return 0; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "nearest") == 0 && argc >= 4) {
        cmd_nearest(pts, n, atof(argv[2]), atof(argv[3]));

    } else if (strcmp(cmd, "knn") == 0 && argc >= 5) {
        cmd_knn(pts, n, atof(argv[2]), atof(argv[3]), atoi(argv[4]));

    } else if (strcmp(cmd, "optimise") == 0 && argc >= 4) {
        cmd_optimise(pts, n, atoi(argv[2]), atof(argv[3]));

    } else if (strcmp(cmd, "coverage") == 0) {
        cmd_coverage(pts, n);

    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        printf("{}");
        free(pts);
        return 1;
    }

    free(pts);
    return 0;
}
