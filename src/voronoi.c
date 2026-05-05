#include <math.h>
#include <string.h>
#include "voronoi.h"

/*
 * Sutherland-Hodgman clip against one half-plane: a*x + b*y <= c
 * Keeps points on the inside; computes intersections at the boundary.
 * Returns new vertex count.
 */
static int sh_clip(double *in, int n, double *out,
                   double a, double b, double c)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        double ix = in[2*i],            iy = in[2*i + 1];
        double jx = in[2*((i+1)%n)],   jy = in[2*((i+1)%n) + 1];
        double si  = a*ix + b*iy;
        double sj  = a*jx + b*jy;

        if (si <= c) {
            out[2*m] = ix;  out[2*m+1] = iy;  m++;
        }
        if ((si <= c) != (sj <= c) && fabs(sj - si) > 1e-15) {
            double t = (c - si) / (sj - si);
            out[2*m]   = ix + t*(jx - ix);
            out[2*m+1] = iy + t*(jy - iy);
            m++;
        }
    }
    return m;
}

int voronoi_cell(int site_idx, point_t *sites, int n,
                 double xmin, double ymin, double xmax, double ymax,
                 double *out_poly)
{
    static double buf0[MAX_CELL_VERTS * 2];
    static double buf1[MAX_CELL_VERTS * 2];

    double sx = sites[site_idx].x;
    double sy = sites[site_idx].y;

    /* Initialise polygon to bounding box */
    buf0[0]=xmin; buf0[1]=ymin;
    buf0[2]=xmax; buf0[3]=ymin;
    buf0[4]=xmax; buf0[5]=ymax;
    buf0[6]=xmin; buf0[7]=ymax;
    int k = 4;
    double *cur = buf0, *nxt = buf1;

    for (int j = 0; j < n && k >= 3; j++) {
        if (j == site_idx) continue;

        /*
         * Perpendicular bisector of sites[site_idx] and sites[j]:
         *   normal  n = (sites[j] - sites[site_idx])
         *   midpoint m = average of the two sites
         *   half-plane (closer to site_idx): n · (x - m) <= 0
         *   i.e.  nx*x + ny*y  <=  nx*mx + ny*my
         */
        double nx = sites[j].x - sx;
        double ny = sites[j].y - sy;
        double mx = (sx + sites[j].x) * 0.5;
        double my = (sy + sites[j].y) * 0.5;
        double c  = nx*mx + ny*my;

        int k2 = sh_clip(cur, k, nxt, nx, ny, c);
        if (k2 >= 3) {
            double *tmp = cur; cur = nxt; nxt = tmp;
            k = k2;
        } else if (k2 == 0) {
            return 0;
        }
    }

    if (k < 3) return 0;
    memcpy(out_poly, cur, 2 * (size_t)k * sizeof(double));
    return k;
}

double voronoi_poly_area(double *poly, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        s += poly[2*i] * poly[2*j+1] - poly[2*j] * poly[2*i+1];
    }
    return fabs(s) * 0.5;
}
