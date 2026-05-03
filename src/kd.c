#include <stdlib.h>
#include <math.h>
#include <float.h>
#include "kd.h"


// make_node — creates a single tree node

static kdnode_t *make_node(point_t p, int axis) {
    kdnode_t *node = (kdnode_t *)malloc(sizeof(kdnode_t));
    node->point   = p;
    node->axis    = axis;
    node->deleted = 0;
    node->left    = NULL;
    node->right   = NULL;
    return node;
}


// quickselect — finds median in-place in O(n)
// Replaces qsort — no longer O(n log n) per level


static void quickselect(point_t *pts, int lo, int hi, int mid, int axis) 
{
    while (lo < hi) 
    {
        int pivot_idx = (lo + hi) / 2;
        double pivot  = (axis == 0) ? pts[pivot_idx].x : pts[pivot_idx].y;
        int i = lo, j = hi;

        while (i <= j) 
        {
            while ((axis == 0 ? pts[i].x : pts[i].y) < pivot) i++;
            while ((axis == 0 ? pts[j].x : pts[j].y) > pivot) j--;
            if (i <= j)
            {
                point_t tmp = pts[i]; 
                pts[i] = pts[j]; 
                pts[j] = tmp;
                i++; j--;
            }
        }
        if (mid <= j) hi = j;
        else if (mid >= i) lo = i;
        else break;
    }
}

// build_recursive — builds the tree recursively    

static kdnode_t *build_recursive(point_t *pts, int lo, int hi, int depth) 
{
    if (lo > hi) return NULL;

    int axis = depth % 2; //which level 

    int mid  = (lo + hi) / 2; // mid

    quickselect(pts, lo, hi, mid, axis); //calls quick select

    kdnode_t *node = make_node(pts[mid], axis);

    node->left  = build_recursive(pts, lo, mid - 1, depth + 1);//left tree
    node->right = build_recursive(pts, mid + 1, hi,depth + 1);//right tree

    return node;
}


// kd_build — public function, call this to build the tree

kdnode_t *kd_build(point_t *pts, int n) 
{
    if (pts == NULL || n <= 0) 
    return NULL;

    point_t *copy = (point_t *)malloc(sizeof(point_t) * n);

    for (int i = 0; i < n; i++) 
    {
        copy[i] = pts[i];
    }

    kdnode_t *root = build_recursive(copy, 0, n - 1, 0);
    free(copy);
    return root;
}

// kd_free — free all memory


void kd_free(kdnode_t *root) 
{
    if (root == NULL) return;
    kd_free(root->left);
    kd_free(root->right);
    free(root);
}

// dist_squared — avoids sqrt during search


static double dist_squared(point_t a, point_t b)
 {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx*dx + dy*dy;
}


// nn_recursive — branch and bound nearest neighbor search


static void nn_recursive(kdnode_t *node, point_t query,point_t *best_point, double *best_dist) 
{
    if (node == NULL) return;

    if (!node->deleted) 
    {
        double d = dist_squared(node->point, query);
        if (d < *best_dist) 
        {
            *best_dist  = d;
            *best_point = node->point;
        }
    }

    double diff;
    if (node->axis == 0) 
    {
        diff = query.x - node->point.x;
    } else {
        diff = query.y - node->point.y;
    }

    kdnode_t *near_side = (diff < 0) ? node->left  : node->right;
    kdnode_t *far_side  = (diff < 0) ? node->right : node->left;

    nn_recursive(near_side, query, best_point, best_dist);

    if (diff * diff < *best_dist) 
    {
        nn_recursive(far_side, query, best_point, best_dist);
    }
}


// kd_nearest — public function, returns closest facility


point_t kd_nearest(kdnode_t *root, point_t query) 
{
    point_t best_point = {0.0, 0.0, -1};
    double  best_dist  = DBL_MAX;
    nn_recursive(root, query, &best_point, &best_dist);
    return best_point;
}

// max-heap for k-NN


typedef struct 
{
    double  dist;
    point_t pt;

} 
heap_entry_t;

static void sift_down(heap_entry_t *heap, int size, int i)
 {
    while (1) {
        int largest = i;
        int left    = 2 * i + 1;
        int right   = 2 * i + 2;

        if (left  < size && heap[left].dist  > heap[largest].dist) largest = left;
        if (right < size && heap[right].dist > heap[largest].dist) largest = right;

        if (largest == i) break;

        heap_entry_t tmp = heap[i];
        heap[i]          = heap[largest];
        heap[largest]    = tmp;
        i                = largest;
    }
}

static void sift_up(heap_entry_t *heap, int i)
 {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap[parent].dist >= heap[i].dist) break;

        heap_entry_t tmp  = heap[parent];
        heap[parent]      = heap[i];
        heap[i]           = tmp;
        i                 = parent;
    }
}

static void heap_try_insert(heap_entry_t *heap, int *heap_size,int k, double dist, point_t pt) 
{
    if (*heap_size < k)
    {
        heap[*heap_size].dist = dist;
        heap[*heap_size].pt   = pt;
        (*heap_size)++;
        sift_up(heap, *heap_size - 1);
    } 

    else if (dist < heap[0].dist)
    {
        heap[0].dist = dist;
        heap[0].pt   = pt;
        sift_down(heap, *heap_size, 0);
    }
}

// knn_recursive — branch and bound k-NN search


static void knn_recursive(kdnode_t *node, point_t query,heap_entry_t *heap, int *heap_size, int k) 
{
    if (node == NULL) return;

    if (!node->deleted) 
    {

        double d = dist_squared(node->point, query);
        heap_try_insert(heap, heap_size, k, d, node->point);
    }

    double diff;
    if (node->axis == 0)
    {
        diff = query.x - node->point.x;
    } 
    else 
    {
        diff = query.y - node->point.y;
    }

    kdnode_t *near_side = (diff < 0) ? node->left  : node->right;
    kdnode_t *far_side  = (diff < 0) ? node->right : node->left;

    knn_recursive(near_side, query, heap, heap_size, k);

    if (*heap_size < k || diff * diff < heap[0].dist) 
    {
        knn_recursive(far_side, query, heap, heap_size, k);
    }
}


// kd_knn — public function, returns k nearest facilities
// caller must free() the returned array


point_t *kd_knn(kdnode_t *root, point_t query, int k, int *out_count) 
{
    if (root == NULL || k <= 0) 
    {
        *out_count = 0;
        return NULL;
    }

    heap_entry_t *heap = (heap_entry_t *)malloc(sizeof(heap_entry_t) * k);
    int heap_size = 0;

    knn_recursive(root, query, heap, &heap_size, k);

    // insertion sort — sort nearest-first (heap is NOT sorted)
    for (int i = 1; i < heap_size; i++) 
    {
        heap_entry_t key = heap[i];
        int j = i - 1;
        while (j >= 0 && heap[j].dist > key.dist) 
        {
            heap[j + 1] = heap[j];
            j--;
        }
        heap[j + 1] = key;
    }

    *out_count = heap_size;
    point_t *results = (point_t *)malloc(sizeof(point_t) * heap_size);
    for (int i = 0; i < heap_size; i++) {
        results[i] = heap[i].pt;
    }

    free(heap);
    return results;
}