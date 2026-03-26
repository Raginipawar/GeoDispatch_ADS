#ifndef VORONOI_H
#define VORONOI_H

#include "kd.h"   


typedef struct vertex {
    double x, y;
    half_edge_t* incident_edge;
} vertex_t;

typedef struct half_edge {
    vertex_t          *origin;
    struct half_edge  *twin;
    struct half_edge  *next;
    struct half_edge  *prev;
    struct face       *face;
    int                is_infinite;
} half_edge_t;

typedef struct face {
    int           site_id;
    half_edge_t  *outer_edge;
    double        area;           
    int           is_underserved; 
} face_t;

typedef struct dcel {
    vertex_t** vertices;
    int nv;
    half_edge_t** edges;
    int ne;
    face_t** faces;
    int nf;
    
    // Internal capacities for dynamic array reallocation
    int max_v, max_e, max_f;
} dcel_t;


// Incremental insertion
void voronoi_insert_site(dcel_t* d, point_t new_site);


void   clip_to_bbox(dcel_t *d, double xmin, double ymin, double xmax, double ymax);
double cell_area(dcel_t *d, int face_id);
void   compute_all_areas(dcel_t *d);
int   *flag_underserved(dcel_t *d, double threshold, int *out_count);

#endif 
