#ifndef KD_H
#define KD_H

// A single point — one facility on the map
typedef struct 
{
    double x;   // Cartesian x (in metres, from Nikhil's data loader)
    double y;   // Cartesian y (in metres)
    int    id;  // facility ID (matches pune_facilities.json)
} point_t;

// One node in the KD-tree  
typedef struct kdnode {
    point_t        point;    // the facility stored at this node
    int            axis;     // 0 = we split on x here, 1 = we split on y
    int            deleted;  // Nikhil sets this to 1 when facility goes offline
    struct kdnode *left;
    struct kdnode *right;
} kdnode_t;


kdnode_t *kd_build(point_t *pts, int n);
point_t   kd_nearest(kdnode_t *root, point_t query);
point_t  *kd_knn(kdnode_t *root, point_t query, int k, int *out_count);
void      kd_free(kdnode_t *root);


void   kd_delete(kdnode_t *root, int point_id);
void   kd_insert(kdnode_t **root, point_t p);
void   kd_rebalance(kdnode_t **root);
double kd_dead_ratio(kdnode_t *root);

#endif