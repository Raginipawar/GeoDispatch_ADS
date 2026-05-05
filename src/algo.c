#include <stdlib.h>
#include <math.h>
#include "geodispatch.h"

/*
 * run_lloyds — Lloyd's relaxation for facility placement.
 *
 * For each iteration:
 *   1. Compute each facility's Voronoi cell (half-plane intersection).
 *   2. Move the facility to the centroid of that cell.
 *   3. Stop when total movement < threshold or max_iters reached.
 *
 * Uses voronoi_cell() from voronoi.c — no DCEL required.
 */
lloyds_result_t *run_lloyds(point_t *pts, int n,
                             double xmin, double ymin,
                             double xmax, double ymax,
                             int max_iters, double threshold)
{
    lloyds_result_t *res = calloc(1, sizeof(lloyds_result_t));
    res->moves = calloc((size_t)max_iters * (size_t)n, sizeof(facility_move_t));

    double poly[MAX_CELL_VERTS * 2];

    for (int iter = 0; iter < max_iters; iter++) {
        double total_movement = 0.0;

        for (int i = 0; i < n; i++) {
            /* Step 1: Voronoi cell for facility i */
            int nv = voronoi_cell(i, pts, n, xmin, ymin, xmax, ymax, poly);
            if (nv < 3) continue;

            /* Step 2: centroid = average of polygon vertices */
            double cx = 0.0, cy = 0.0;
            for (int v = 0; v < nv; v++) {
                cx += poly[2*v];
                cy += poly[2*v + 1];
            }
            cx /= nv;
            cy /= nv;

            double dx   = cx - pts[i].x;
            double dy   = cy - pts[i].y;
            double dist = sqrt(dx*dx + dy*dy);
            if (dist < 0.1) continue;   /* already at centroid */

            /* Record move */
            facility_move_t *m = &res->moves[res->nmoves++];
            m->site_id = pts[i].id;
            m->from_x  = pts[i].x;
            m->from_y  = pts[i].y;
            m->to_x    = cx;
            m->to_y    = cy;

            /* Step 3: move facility */
            pts[i].x = cx;
            pts[i].y = cy;
            total_movement += dist;
        }

        res->iterations_run++;
        if (total_movement < threshold) break;
    }

    return res;
}

void free_lloyds_result(lloyds_result_t *res)
{
    if (!res) return;
    free(res->moves);
    free(res);
}
