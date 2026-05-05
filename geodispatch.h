#ifndef GEODISPATCH_H
#define GEODISPATCH_H

#include "src/kd.h"
#include "src/voronoi.h"

/* One facility movement recorded during Lloyd's relaxation */
typedef struct {
    int    site_id;
    double from_x, from_y;
    double to_x,   to_y;
} facility_move_t;

/* Result returned by run_lloyds */
typedef struct {
    int              iterations_run;
    facility_move_t *moves;
    int              nmoves;
} lloyds_result_t;

/*
 * run_lloyds — iterative facility placement optimisation.
 *
 * Each iteration: compute each facility's Voronoi cell, move the
 * facility to the centroid of that cell, repeat until total movement
 * drops below threshold or max_iters is reached.
 */
lloyds_result_t *run_lloyds(point_t *pts, int n,
                             double xmin, double ymin,
                             double xmax, double ymax,
                             int max_iters, double threshold);

void free_lloyds_result(lloyds_result_t *res);

#endif
