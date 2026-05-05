#ifndef VORONOI_H
#define VORONOI_H

#include "kd.h"

#define MAX_CELL_VERTS 1024

/*
 * voronoi_cell — compute the Voronoi cell for sites[site_idx].
 *
 * Algorithm: start with the bounding box as a polygon, then clip it
 * by the perpendicular bisector of (sites[site_idx], sites[j]) for
 * every other j.  What remains is exactly the Voronoi cell — all
 * points closer to sites[site_idx] than to any other site.
 *
 * Clipping uses Sutherland-Hodgman against each half-plane.
 *
 * Returns number of polygon vertices written into out_poly
 * (flat array: x0,y0, x1,y1, ...).  Returns 0 if degenerate.
 */
int voronoi_cell(int site_idx, point_t *sites, int n,
                 double xmin, double ymin, double xmax, double ymax,
                 double *out_poly);

/* Shoelace formula — area of a polygon stored as flat [x,y,...] array. */
double voronoi_poly_area(double *poly, int n);

#endif
