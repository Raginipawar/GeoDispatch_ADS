#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "voronoi.h"

#define EPSILON 1e-9

dcel_t* dcel_create() {
    dcel_t* d = calloc(1, sizeof(dcel_t));
    d->max_v = d->max_e = d->max_f = 12000;
    d->vertices = calloc(d->max_v, sizeof(vertex_t*));
    d->edges = calloc(d->max_e, sizeof(half_edge_t*));
    d->faces = calloc(d->max_f, sizeof(face_t*));
    return d;
}

vertex_t* dcel_add_vertex(dcel_t* d, double x, double y) {
    if(d->nv == d->max_v) { d->max_v *= 2; d->vertices = realloc(d->vertices, sizeof(vertex_t*) * d->max_v); }
    vertex_t* v = calloc(1, sizeof(vertex_t)); v->x = x; v->y = y;
    d->vertices[d->nv++] = v; return v;
}

half_edge_t* dcel_add_edge(dcel_t* d) {
    if(d->ne == d->max_e) { d->max_e *= 2; d->edges = realloc(d->edges, sizeof(half_edge_t*) * d->max_e); }
    half_edge_t* e = calloc(1, sizeof(half_edge_t));
    d->edges[d->ne++] = e; return e;
}

face_t* dcel_add_face(dcel_t* d, int site_id) {
    if(d->nf == d->max_f) { d->max_f *= 2; d->faces = realloc(d->faces, sizeof(face_t*) * d->max_f); }
    face_t* f = calloc(1, sizeof(face_t)); f->site_id = site_id;
    d->faces[d->nf++] = f; return f;
}

void voronoi_free(dcel_t* d) {
    if(!d) return;
    for(int i=0; i<d->nv; i++) free(d->vertices[i]);
    for(int i=0; i<d->ne; i++) free(d->edges[i]);
    for(int i=0; i<d->nf; i++) free(d->faces[i]);
    free(d->vertices); free(d->edges); free(d->faces); free(d);
}

face_t** dcel_neighbours(dcel_t* d, int site_id, int* out_count) {
    if(!d || !out_count) return NULL;
    face_t* f = NULL;
    for(int i=0; i<d->nf; i++) { if(d->faces[i]->site_id == site_id) { f = d->faces[i]; break; } }
    if(!f || !f->outer_edge) { *out_count = 0; return NULL; }
    face_t** nbrs = calloc(d->nf, sizeof(face_t*));
    int count = 0; half_edge_t* start = f->outer_edge;
    half_edge_t* curr = start;
    if(!curr) { *out_count=0; return nbrs; }
    do {
        if(curr->twin && curr->twin->face) {
            int exists = 0;
            for(int i=0; i<count; i++) { if(nbrs[i] == curr->twin->face) { exists = 1; break; } }
            if(!exists){ nbrs[count++] = curr->twin->face; }
        }
        curr = curr->next;
    } while(curr && curr != start);
    *out_count = count; return nbrs;
}

void voronoi_insert_site(dcel_t* d, point_t new_site) { (void)d; (void)new_site; }

// ============================================================================
// FORTUNE'S ALGORITHM O(N LOG N) IMPLEMENTATION
// ============================================================================

typedef struct arc arc_t;
typedef struct event {
    double x, y;
    int is_circle; // 0=site, 1=circle
    point_t site;
    arc_t *arc;
    int valid;
} event_t;

struct arc {
    point_t site;
    arc_t *prev, *next;
    arc_t *left, *right, *parent;
    int height;
    event_t *circle_event;
    half_edge_t *s0, *s1;
};

// Priority Queue for Events (Min-Heap by x, then y)
typedef struct {
    event_t **events;
    int size, capacity;
} event_queue_t;

void eq_push(event_queue_t *q, event_t *e) {
    if(q->size == q->capacity) {
        q->capacity = q->capacity ? q->capacity * 2 : 1024;
        q->events = realloc(q->events, sizeof(event_t*) * q->capacity);
    }
    int i = q->size++;
    while(i > 0) {
        int p = (i-1)/2;
        if(q->events[p]->x < e->x || (fabs(q->events[p]->x - e->x) < EPSILON && q->events[p]->y < e->y)) break;
        q->events[i] = q->events[p];
        i = p;
    }
    q->events[i] = e;
}

event_t* eq_pop(event_queue_t *q) {
    if(q->size == 0) return NULL;
    event_t *res = q->events[0];
    event_t *e = q->events[--q->size];
    int i = 0;
    while(i*2 + 1 < q->size) {
        int left = i*2 + 1, right = i*2 + 2, minc = left;
        if(right < q->size && (q->events[right]->x < q->events[left]->x || (fabs(q->events[right]->x - q->events[left]->x) < EPSILON && q->events[right]->y < q->events[left]->y))) {
            minc = right;
        }
        if(e->x < q->events[minc]->x || (fabs(e->x - q->events[minc]->x) < EPSILON && e->y < q->events[minc]->y)) break;
        q->events[i] = q->events[minc];
        i = minc;
    }
    q->events[i] = e;
    return res;
}

// AVL Tree specific fields and balancing
int arc_height(arc_t* a) { return a ? a->height : 0; }
int max_h(int a, int b) { return a > b ? a : b; }
void update_h(arc_t* a) { if(a) a->height = 1 + max_h(arc_height(a->left), arc_height(a->right)); }
int bfactor(arc_t* a) { return a ? arc_height(a->left) - arc_height(a->right) : 0; }

void rotate_left(arc_t** root, arc_t* x) {
    arc_t* y = x->right; x->right = y->left;
    if(y->left) y->left->parent = x;
    y->parent = x->parent;
    if(!x->parent) *root = y;
    else if(x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x; x->parent = y;
    update_h(x); update_h(y);
}

void rotate_right(arc_t** root, arc_t* x) {
    arc_t* y = x->left; x->left = y->right;
    if(y->right) y->right->parent = x;
    y->parent = x->parent;
    if(!x->parent) *root = y;
    else if(x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x; x->parent = y;
    update_h(x); update_h(y);
}

void balance(arc_t** root, arc_t* n) {
    while(n) {
        update_h(n);
        int bf = bfactor(n);
        if(bf > 1) {
            if(bfactor(n->left) < 0) rotate_left(root, n->left);
            rotate_right(root, n);
        } else if(bf < -1) {
            if(bfactor(n->right) > 0) rotate_right(root, n->right);
            rotate_left(root, n);
        }
        n = n->parent;
    }
}

// Get the intersection y-coordinate of parabolas of two arcs
double get_y_intersect(arc_t* a1, arc_t* a2, double sweep_x) {
    if(!a1 || !a2) return -1e9;
    point_t f1 = a1->site, f2 = a2->site;
    if(fabs(f1.x - f2.x) < EPSILON) return (f1.y + f2.y)/2.0;

    double dp1 = 2 * (sweep_x - f1.x);
    double dp2 = 2 * (sweep_x - f2.x);
    
    double a = 1.0/dp1 - 1.0/dp2;
    double b = -2.0*(f1.y/dp1 - f2.y/dp2);
    double c = (f1.y*f1.y + f1.x*f1.x - sweep_x*sweep_x)/dp1 - (f2.y*f2.y + f2.x*f2.x - sweep_x*sweep_x)/dp2;
    
    if(fabs(a) < EPSILON) return -c/b;
    double disc = b*b - 4*a*c;
    if(disc < 0) return 1e9;
    double sq = sqrt(disc);
    double y1 = (-b + sq)/(2*a);
    double y2 = (-b - sq)/(2*a);
    
    if(f1.y < f2.y) return (y1 > y2) ? y1 : y2;
    return (y1 < y2) ? y1 : y2;
}

// Find arc enclosing y
arc_t* find_arc(arc_t* root, double y, double sweep_x) {
    arc_t *node = root, *res = NULL;
    while(node) {
        double y_bottom = node->prev ? get_y_intersect(node->prev, node, sweep_x) : -1e9;
        double y_top = node->next ? get_y_intersect(node, node->next, sweep_x) : 1e9;
        if(y < y_bottom) node = node->left;
        else if(y > y_top) node = node->right;
        else { res = node; break; }
    }
    // If exact tie due to precision, gracefully fallback
    if(!res) { res = root; while(res && res->next) res = res->next; }
    return res;
}

// Replace an arc in the BST maintaining order (a_bot acts as previous a)
void avl_insert_after(arc_t** root, arc_t* existing, arc_t* new_arc) {
    new_arc->left = new_arc->right = NULL;
    new_arc->height = 1;
    if(!existing->right) {
        existing->right = new_arc;
        new_arc->parent = existing;
    } else {
        arc_t* succ = existing->right;
        while(succ->left) succ = succ->left;
        succ->left = new_arc;
        new_arc->parent = succ;
    }
    balance(root, new_arc);
}

void avl_delete(arc_t** root, arc_t* n) {
    arc_t *repl = NULL, *p = NULL;
    if(!n->left || !n->right) {
        repl = n->left ? n->left : n->right;
        p = n->parent;
        if(repl) repl->parent = p;
        if(!p) *root = repl;
        else if(n == p->left) p->left = repl;
        else p->right = repl;
    } else {
        repl = n->right;
        while(repl->left) repl = repl->left;
        p = repl->parent;
        if(p != n) {
            p->left = repl->right;
            if(repl->right) repl->right->parent = p;
            repl->right = n->right;
            if(repl->right) repl->right->parent = repl;
        } else {
            p = repl;
        }
        repl->parent = n->parent;
        if(!n->parent) *root = repl;
        else if(n == n->parent->left) n->parent->left = repl;
        else n->parent->right = repl;
        repl->left = n->left;
        if(repl->left) repl->left->parent = repl;
    }
    balance(root, p);
}

// Circumcenter math
int circle_center(point_t a, point_t b, point_t c, double *cx, double *cy, double *rad) {
    double d = 2 * (a.x*(b.y - c.y) + b.x*(c.y - a.y) + c.x*(a.y - b.y));
    if(fabs(d) < EPSILON) return 0;
    *cx = ((a.x*a.x + a.y*a.y)*(b.y - c.y) + (b.x*b.x + b.y*b.y)*(c.y - a.y) + (c.x*c.x + c.y*c.y)*(a.y - b.y))/d;
    *cy = ((a.x*a.x + a.y*a.y)*(c.x - b.x) + (b.x*b.x + b.y*b.y)*(a.x - c.x) + (c.x*c.x + c.y*c.y)*(b.x - a.x))/d;
    *rad = sqrt((*cx - a.x)*(*cx - a.x) + (*cy - a.y)*(*cy - a.y));
    return 1;
}

// Context to avoid global variables and state leaks across requests
typedef struct {
    event_queue_t event_q;
    arc_t* beach_line;
    dcel_t* sweep_dcel;
    face_t** dcel_faces;
} sweep_ctx_t;

void check_circle_event(sweep_ctx_t* ctx, arc_t* a) {
    if(a->circle_event) {
        a->circle_event->valid = 0;
        a->circle_event = NULL;
    }
    if(!a->prev || !a->next) return;
    double cx, cy, rad;
    if(circle_center(a->prev->site, a->site, a->next->site, &cx, &cy, &rad)) {
        double sweep_event_x = cx + rad;
        if(sweep_event_x >= a->site.x - EPSILON) {
            event_t* e = calloc(1, sizeof(event_t));
            e->x = sweep_event_x; e->y = cy;
            e->is_circle = 1; e->arc = a; e->valid = 1;
            a->circle_event = e;
            eq_push(&ctx->event_q, e);
        }
    }
}

void handle_site(sweep_ctx_t* ctx, event_t* e) {
    point_t p = e->site;
    if(!ctx->beach_line) {
        ctx->beach_line = calloc(1, sizeof(arc_t));
        ctx->beach_line->site = p; ctx->beach_line->height = 1;
        return;
    }
    arc_t *target = find_arc(ctx->beach_line, p.y, p.x);
    if(target->circle_event) target->circle_event->valid = 0;
    
    arc_t *a_new = calloc(1, sizeof(arc_t)); a_new->site = p;
    arc_t *a_bot = calloc(1, sizeof(arc_t)); a_bot->site = target->site;
    
    a_bot->next = target->next; if(a_bot->next) a_bot->next->prev = a_bot;
    a_bot->prev = a_new; a_new->next = a_bot;
    a_new->prev = target; target->next = a_new;
    
    avl_insert_after(&ctx->beach_line, target, a_new);
    avl_insert_after(&ctx->beach_line, a_new, a_bot);
    
    half_edge_t *s0_tw = dcel_add_edge(ctx->sweep_dcel);
    half_edge_t *s0 = dcel_add_edge(ctx->sweep_dcel);
    s0->twin = s0_tw; s0_tw->twin = s0;
    s0->face = ctx->dcel_faces[target->site.id]; s0_tw->face = ctx->dcel_faces[a_new->site.id];
    target->s1 = s0; a_new->s0 = s0_tw;
    
    half_edge_t *s1_tw = dcel_add_edge(ctx->sweep_dcel);
    half_edge_t *s1 = dcel_add_edge(ctx->sweep_dcel);
    s1->twin = s1_tw; s1_tw->twin = s1;
    s1->face = ctx->dcel_faces[a_new->site.id]; s1_tw->face = ctx->dcel_faces[a_bot->site.id];
    a_new->s1 = s1; a_bot->s0 = s1_tw;
    
    a_bot->s1 = target->s1; 
    
    check_circle_event(ctx, target);
    check_circle_event(ctx, a_bot);
}

void handle_circle(sweep_ctx_t* ctx, event_t* e) {
    arc_t *arc = e->arc;
    if(arc->prev && arc->prev->circle_event) arc->prev->circle_event->valid = 0;
    if(arc->next && arc->next->circle_event) arc->next->circle_event->valid = 0;
    
    double cx, cy, rad;
    if(circle_center(arc->prev->site, arc->site, arc->next->site, &cx, &cy, &rad)) {
        vertex_t* v = dcel_add_vertex(ctx->sweep_dcel, cx, cy);
        
        if(arc->prev->s1) arc->prev->s1->origin = v;
        if(arc->s0) arc->s0->origin = v;
        if(arc->s1) arc->s1->origin = v;
        if(arc->next->s0) arc->next->s0->origin = v;
        
        half_edge_t *s_tw = dcel_add_edge(ctx->sweep_dcel);
        half_edge_t *s = dcel_add_edge(ctx->sweep_dcel);
        s->twin = s_tw; s_tw->twin = s;
        s->face = ctx->dcel_faces[arc->prev->site.id];
        s_tw->face = ctx->dcel_faces[arc->next->site.id];
        s->origin = v;
        
        arc->prev->s1 = s;
        arc->next->s0 = s_tw;
    }
    
    if(arc->prev) arc->prev->next = arc->next;
    if(arc->next) arc->next->prev = arc->prev;
    
    avl_delete(&ctx->beach_line, arc);
    
    arc_t* prev_arc = arc->prev;
    arc_t* next_arc = arc->next;
    free(arc);
    
    if(prev_arc) check_circle_event(ctx, prev_arc);
    if(next_arc) check_circle_event(ctx, next_arc);
}

dcel_t* voronoi_build(point_t* sites, int n) {
    sweep_ctx_t ctx = {0};
    ctx.sweep_dcel = dcel_create();
    if(n <= 0) return ctx.sweep_dcel;
    
    ctx.dcel_faces = calloc(n, sizeof(face_t*));
    for(int i=0; i<n; i++) ctx.dcel_faces[i] = dcel_add_face(ctx.sweep_dcel, sites[i].id);
    
    for(int i=0; i<n; i++) {
        event_t *e = calloc(1, sizeof(event_t));
        e->x = sites[i].x; e->y = sites[i].y;
        e->is_circle = 0; e->site = sites[i]; e->valid = 1;
        eq_push(&ctx.event_q, e);
    }
    
    while(ctx.event_q.size > 0) {
        event_t *e = eq_pop(&ctx.event_q);
        if(e->valid) {
            if(e->is_circle == 0) handle_site(&ctx, e);
            else handle_circle(&ctx, e);
        }
        free(e);
    }
    
    free(ctx.dcel_faces);
    if(ctx.event_q.events) free(ctx.event_q.events);
    
    arc_t *curr = ctx.beach_line;
    if (curr) {
        while(curr->left) curr = curr->left;
        while(curr) {
            arc_t *next = curr->next;
            free(curr);
            curr = next;
        }
    }
    
    for(int i=0; i<ctx.sweep_dcel->ne; i++) {
        half_edge_t *he = ctx.sweep_dcel->edges[i];
        if(!he->origin) he->is_infinite = 1;
    }
    
    for(int i=0; i<ctx.sweep_dcel->nf; i++) {
        face_t* f = ctx.sweep_dcel->faces[i];
        half_edge_t* first = NULL;
        for(int e=0; e<ctx.sweep_dcel->ne; e++) {
            if(ctx.sweep_dcel->edges[e]->face == f) { first = ctx.sweep_dcel->edges[e]; break; }
        }
        if(!first) continue;
        f->outer_edge = first;
        
        half_edge_t** face_edges = calloc(ctx.sweep_dcel->ne, sizeof(half_edge_t*));
        int fecount = 0;
        for(int e=0; e<ctx.sweep_dcel->ne; e++) {
            if(ctx.sweep_dcel->edges[e]->face == f) face_edges[fecount++] = ctx.sweep_dcel->edges[e];
        }
        for(int e1=0; e1<fecount; e1++) {
            for(int e2=0; e2<fecount; e2++) {
                if(e1 != e2 && face_edges[e1]->twin && face_edges[e1]->twin->origin != NULL && face_edges[e1]->twin->origin == face_edges[e2]->origin) {
                    face_edges[e1]->next = face_edges[e2];
                    face_edges[e2]->prev = face_edges[e1];
                    break;
                }
            }
        }
        free(face_edges);
    }
    return ctx.sweep_dcel;
}
