/*
 * Copyright (C) 2012 Mail.RU
 * Copyright (C) 2010 Teodor Sigaev
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SPTREE_H_
#define _SPTREE_H_

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef SPTREE_NODE_SELF
/*
 * user could suggest pointer's storage himself
 */
typedef    uint32_t spnode_t;
#define    SPNIL (0xffffffff)
#define SPTREE_MIN_SIZE 64

typedef struct sptree_node_pointers {
    uint32_t    left;   /* sizeof(spnode_t) >= sizeof(sptree_node_pointers.left) !!! */
    uint32_t    right;
} sptree_node_pointers;

#define GET_SPNODE_LEFT(snp)        ( (snp)->left )
#define SET_SPNODE_LEFT(snp, v)     (snp)->left = (v)
#define GET_SPNODE_RIGHT(snp)       ( (snp)->right )
#define SET_SPNODE_RIGHT(snp, v)    (snp)->right = (v)

#endif /* SPTREE_NODE_SELF */

#ifndef alpha
#define    alpha                       ((double)0.75)
#endif
#define    COUNTALPHA(size)            floor(log((double)(size))/log((double)1.0/alpha))

#define    _GET_SPNODE_LEFT(n)         GET_SPNODE_LEFT( t->lrpointers + (n) )
#define    _SET_SPNODE_LEFT(n, v)      SET_SPNODE_LEFT( t->lrpointers + (n), (v) )
#define    _GET_SPNODE_RIGHT(n)        GET_SPNODE_RIGHT( t->lrpointers + (n) )
#define    _SET_SPNODE_RIGHT(n, v)     SET_SPNODE_RIGHT( t->lrpointers + (n), (v) )

#define    ITHELEM(t, i)               ( (char *) (t)->members + (t)->elemsize * (i) )
#define    ELEMIDX(t, e)               ( ((e) - (t)->members) / (t)->elemsize )

/*
 * makes definition of tree with methods, name should
 * be unique across all definitions.
 *
 * Methods:
 *   void sptree_NAME_init(sptree_NAME *tree, size_t elemsize, void *array,
 *                         spnode_t array_len, spnode_t array_size,
 *                         int (*compare)(const void *key, const void *elem, void *arg),
 *                         int (*elemcompare)(const void *e1, const void *e2, void *arg),
 *                         void *arg)
 *
 *   void sptree_NAME_replace(sptree_NAME *tree, void *value, void **p_oldvalue)
 *   void sptree_NAME_delete(sptree_NAME *tree, void *value)
 *   void* sptree_NAME_find(sptree_NAME *tree, void *key)
 *
 *   spnode_t sptree_NAME_walk(sptree_NAME *t, void* array, spnode_t limit, spnode_t offset)
 *   void sptree_NAME_walk_cb(sptree_NAME *t, int (*cb)(void* cb_arg, void* elem), void *cb_arg)
 *
 *   sptree_NAME_iterator* sptree_NAME_iterator_init(sptree_NAME *t)
 *   void sptree_NAME_iterator_init_set(sptree_NAME *t, sptree_NAME_iterator **iterator, void *start)
 *   sptree_NAME_iterator* sptree_NAME_iterator_reverse_init(sptree_NAME *t)
 *   void sptree_NAME_iterator_reverse_init_set(sptree_NAME *t, sptree_NAME_iterator **iterator, void *start)
 *   void sptree_NAME_iterator_free(sptree_NAME_iterator *i)
 *
 *   void* sptree_NAME_iterator_next(sptree_NAME_iterator *i)
 *   void* sptree_NAME_iterator_reverse_next(sptree_NAME_iterator *i)
 */

#define SPTREE_DEF(name, realloc, qsort_arg)                                              \
typedef int (*sptree_##name##_compare)(const void *, const void *, void *);               \
                                                                                          \
typedef struct sptree_##name {                                                            \
    void                    *members;                                                     \
    sptree_node_pointers    *lrpointers;                                                  \
                                                                                          \
    spnode_t                nmember;                                                      \
    spnode_t                ntotal;                                                       \
                                                                                          \
    sptree_##name##_compare compare;                                                      \
    sptree_##name##_compare elemcompare;                                                  \
    void*                   arg;                                                          \
    size_t                  elemsize;                                                     \
                                                                                          \
    spnode_t                root;                                                         \
    spnode_t                garbage_head;                                                 \
    spnode_t                size;                                                         \
    spnode_t                max_size;                                                     \
    spnode_t                max_depth;                                                    \
} sptree_##name;                                                                          \
                                                                                          \
static spnode_t                                                                           \
sptree_##name##_mktree(sptree_##name *t, spnode_t depth, spnode_t start, spnode_t end) {  \
    spnode_t    half = ( (end + start) >> 1 ), tmp;                                       \
                                                                                          \
    if (depth > t->max_depth) t->max_depth = depth;                                       \
                                                                                          \
    if ( half == start ||                                                                 \
            ( tmp = sptree_##name##_mktree(t, depth+1, start, half)) == half )            \
        _SET_SPNODE_LEFT(half, SPNIL);                                                    \
    else                                                                                  \
        _SET_SPNODE_LEFT(half, tmp);                                                      \
    if ( half+1 >= end ||                                                                 \
            ( tmp = sptree_##name##_mktree(t, depth+1, half+1, end)) == half )            \
        _SET_SPNODE_RIGHT(half, SPNIL);                                                   \
    else                                                                                  \
        _SET_SPNODE_RIGHT(half, tmp);                                                     \
                                                                                          \
    return half;                                                                          \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
sptree_##name##_init(sptree_##name *t, size_t elemsize, void *m,                          \
                     spnode_t nm, spnode_t nt,                                            \
                     sptree_##name##_compare compare,                                     \
                     sptree_##name##_compare elemcompare,                                 \
                     void *arg) {                                                         \
    memset(t, 0, sizeof(*t));                                                             \
    t->members = m;                                                                       \
    t->max_size = t->size = t->nmember = nm;                                              \
    t->ntotal = (nt==0) ? nm : nt;                                                        \
    t->compare = compare != NULL ? compare : elemcompare;                                 \
    t->elemcompare = elemcompare != NULL ? elemcompare : compare;                         \
    t->arg = arg;                                                                         \
    t->elemsize = elemsize;                                                               \
    t->garbage_head = t->root = SPNIL;                                                    \
                                                                                          \
    if (t->ntotal == 0 || t->members == NULL) { /* from scratch */                        \
        if (t->ntotal == 0) {                                                             \
            t->members = NULL;                                                            \
            t->ntotal = SPTREE_MIN_SIZE;                                                  \
        }                                                                                 \
                                                                                          \
        if (t->members == NULL)                                                           \
            t->members = realloc(NULL, elemsize * t->ntotal);                             \
    }                                                                                     \
    t->lrpointers = (sptree_node_pointers *) realloc(NULL,                                \
                                sizeof(sptree_node_pointers) * t->ntotal);                \
                                                                                          \
    if (t->nmember == 1) {                                                                \
        t->root = 0;                                                                      \
        _SET_SPNODE_RIGHT(0, SPNIL);                                                      \
        _SET_SPNODE_LEFT(0, SPNIL);                                                       \
    } else if (t->nmember > 1)    {                                                       \
        qsort_arg(t->members, t->nmember, elemsize, t->elemcompare, t->arg);              \
        /* create tree */                                                                 \
        t->root = sptree_##name##_mktree(t, 1, 0, t->nmember);                            \
    }                                                                                     \
    if (t->members && t->lrpointers)                                                      \
        return 0;                                                                         \
    else if (t->members)                                                                  \
        return t->ntotal * sizeof(sptree_node_pointers);                                  \
    else if (t->lrpointers)                                                               \
        return t->ntotal * elemsize;                                                      \
    else                                                                                  \
        return t->ntotal * (sizeof(sptree_node_pointers) + elemsize);                     \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
sptree_##name##_destroy(sptree_##name *t) {                                               \
        if (t == NULL)    return;                                                         \
    t->members = realloc(t->members, 0);                                                  \
    t->lrpointers = (sptree_node_pointers *)realloc(t->lrpointers, 0);                    \
}                                                                                         \
                                                                                          \
/** Nodes in the garbage list have a loop on their right link. */                         \
static inline bool                                                                        \
sptree_##name##_node_is_deleted(const sptree_##name *t, spnode_t node) {                  \
                                                                                          \
    return _GET_SPNODE_RIGHT(node) == node;                                               \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
sptree_##name##_find(const sptree_##name *t, void *k) {                                   \
    spnode_t    node = t->root;                                                           \
    while(node != SPNIL) {                                                                \
    int r = t->compare(k, ITHELEM(t, node), t->arg);                                      \
        if (r > 0) {                                                                      \
            node = _GET_SPNODE_RIGHT(node);                                               \
        } else if (r < 0) {                                                               \
            node = _GET_SPNODE_LEFT(node);                                                \
        } else {                                                                          \
            return ITHELEM(t, node);                                                      \
        }                                                                                 \
    }                                                                                     \
    return NULL;                                                                          \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
sptree_##name##_first(const sptree_##name *t) {                                           \
    spnode_t    node = t->root;                                                           \
    spnode_t    first = SPNIL;                                                            \
    while (node != SPNIL) {                                                               \
            first = node;                                                                 \
            node = _GET_SPNODE_LEFT(node);                                                \
    }                                                                                     \
    if (first != SPNIL)                                                                   \
        return ITHELEM(t, first);                                                         \
    return NULL;                                                                          \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
sptree_##name##_last(const sptree_##name *t) {                                            \
    spnode_t    node = t->root;                                                           \
    spnode_t    last = SPNIL;                                                             \
    while (node != SPNIL) {                                                               \
            last = node;                                                                  \
            node = _GET_SPNODE_RIGHT(node);                                               \
    }                                                                                     \
    if (last != SPNIL)                                                                    \
        return ITHELEM(t, last);                                                          \
    return NULL;                                                                          \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
sptree_##name##_random(const sptree_##name *t, spnode_t rnd) {                            \
    for (spnode_t i = 0; i < t->size; i++, rnd++) {                                       \
        rnd %= t->nmember;                                                                \
        if (!sptree_##name##_node_is_deleted(t, rnd))                                     \
            return ITHELEM(t, rnd);                                                       \
                                                                                          \
    }                                                                                     \
                                                                                          \
    return NULL;                                                                          \
}                                                                                         \
static inline spnode_t                                                                    \
sptree_##name##_size_of_subtree(sptree_##name *t, spnode_t node) {                        \
    if (node == SPNIL)                                                                    \
        return 0;                                                                         \
    return 1 +                                                                            \
        sptree_##name##_size_of_subtree(t, _GET_SPNODE_LEFT(node)) +                      \
        sptree_##name##_size_of_subtree(t, _GET_SPNODE_RIGHT(node));                      \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
sptree_##name##_reserve_places(sptree_##name *t, spnode_t nreserve) {                     \
	spnode_t num_free = t->ntotal - t->size;                                          \
	if (num_free >= nreserve)                                                         \
		return 0;                                                                 \
	spnode_t new_ntotal = MAX(t->ntotal * 2, t->ntotal + nreserve - num_free);        \
	void *new_members = realloc(t->members, new_ntotal * t->elemsize);                \
	if (!new_members)                                                                 \
		return new_ntotal * t->elemsize;                                                                \
	t->members = new_members;                                                         \
	sptree_node_pointers *new_lrpointers = (sptree_node_pointers *)                   \
		realloc(t->lrpointers, new_ntotal * sizeof(sptree_node_pointers));        \
	if (!new_lrpointers)                                                              \
		return new_ntotal * sizeof(sptree_node_pointers);                                                                \
	t->lrpointers = new_lrpointers;                                                   \
	t->ntotal = new_ntotal;                                                           \
	return 0;                                                                         \
}                                                                                         \
                                                                                          \
static inline spnode_t                                                                    \
sptree_##name##_get_place(sptree_##name *t) {                                             \
    spnode_t    node;                                                                     \
    if (t->garbage_head != SPNIL) {                                                       \
        node = t->garbage_head;                                                           \
        t->garbage_head = _GET_SPNODE_LEFT(t->garbage_head);                              \
    } else {                                                                              \
        if (t->nmember >= t->ntotal) {                                                    \
            spnode_t new_ntotal = t->ntotal * 2;                                          \
            t->members = realloc(t->members, new_ntotal * t->elemsize);                   \
            t->lrpointers = (sptree_node_pointers *) realloc(t->lrpointers,               \
                                    new_ntotal * sizeof(sptree_node_pointers));           \
            t->ntotal = new_ntotal;                                                       \
        }                                                                                 \
                                                                                          \
        node = t->nmember;                                                                \
        t->nmember++;                                                                     \
    }                                                                                     \
    _SET_SPNODE_LEFT(node, SPNIL);                                                        \
    _SET_SPNODE_RIGHT(node, SPNIL);                                                       \
    return node;                                                                          \
}                                                                                         \
                                                                                          \
static inline spnode_t                                                                    \
sptree_##name##_flatten_tree(sptree_##name *t, spnode_t root, spnode_t head) {            \
    spnode_t    node;                                                                     \
    if (root == SPNIL)                                                                    \
        return head;                                                                      \
    node = sptree_##name##_flatten_tree(t, _GET_SPNODE_RIGHT(root), head);                \
    _SET_SPNODE_RIGHT(root, node);                                                        \
    return sptree_##name##_flatten_tree(t, _GET_SPNODE_LEFT(root), root);                 \
}                                                                                         \
                                                                                          \
static inline spnode_t                                                                    \
sptree_##name##_build_tree(sptree_##name *t, spnode_t node, spnode_t size) {              \
    spnode_t    tmp;                                                                      \
    if (size == 0) {                                                                      \
        _SET_SPNODE_LEFT(node, SPNIL);                                                    \
        return node;                                                                      \
    }                                                                                     \
    spnode_t root = sptree_##name##_build_tree(t,                                         \
                node, ceil(((double)size-1.0)/2.0));                                      \
    spnode_t list = sptree_##name##_build_tree(t,                                         \
                _GET_SPNODE_RIGHT(root), floor(((double)size-1.0)/2.0));                  \
    tmp = _GET_SPNODE_LEFT(list);                                                         \
    _SET_SPNODE_RIGHT(root, tmp);                                                         \
    _SET_SPNODE_LEFT(list, root);                                                         \
                                                                                          \
    return list;                                                                          \
}                                                                                         \
                                                                                          \
static inline spnode_t                                                                    \
sptree_##name##_balance(sptree_##name *t, spnode_t node, spnode_t size) {                 \
    spnode_t fake = sptree_##name##_get_place(t);                                         \
    spnode_t z;                                                                           \
                                                                                          \
    z = sptree_##name##_flatten_tree(t, node, fake);                                      \
    sptree_##name##_build_tree(t, z, size);                                               \
                                                                                          \
    z = _GET_SPNODE_LEFT(fake);                                                           \
    _SET_SPNODE_LEFT(fake, t->garbage_head);                                              \
    /*                                                                                    \
     * Loop back on the right link indicates that the node                                \
     * is in the garbage list.                                                            \
     */                                                                                   \
    _SET_SPNODE_RIGHT(fake, fake);                                                        \
    t->garbage_head = fake;                                                               \
    return z;                                                                             \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
sptree_##name##_replace(sptree_##name *t, void *v, void **p_old) {                        \
    spnode_t    node, depth = 0;                                                          \
    spnode_t    path[ t->max_depth + 2];                                                  \
                                                                                          \
    if (t->root == SPNIL) {                                                               \
        _SET_SPNODE_LEFT(0, SPNIL);                                                       \
        _SET_SPNODE_RIGHT(0, SPNIL);                                                      \
        memcpy(t->members, v, t->elemsize);                                               \
        t->root = 0;                                                                      \
        t->garbage_head = SPNIL;                                                          \
        t->nmember = 1;                                                                   \
        t->size=1;                                                                        \
        if (p_old)                                                                        \
            *p_old = NULL;                                                                \
        return 0;                                                                         \
    } else {                                                                              \
        spnode_t    parent = t->root;                                                     \
                                                                                          \
        for(;;)    {                                                                      \
            int r = t->elemcompare(v, ITHELEM(t, parent), t->arg);                        \
            if (r==0) {                                                                   \
                if (p_old)                                                                \
                    memcpy(*p_old, ITHELEM(t, parent), t->elemsize);                      \
                memcpy(ITHELEM(t, parent), v, t->elemsize);                               \
                return 0;                                                                 \
            }                                                                             \
            path[depth] = parent;                                                         \
            depth++;                                                                      \
            if (r>0) {                                                                    \
                if (_GET_SPNODE_RIGHT(parent) == SPNIL) {                                 \
                    /* extra element can be needed for current balance implementation*/   \
                    int reserve_result = sptree_##name##_reserve_places(t, 2);            \
                    if (reserve_result)            \
                        return reserve_result;                                            \
                    node = sptree_##name##_get_place(t);                                  \
                    memcpy(ITHELEM(t, node), v, t->elemsize);                             \
                    _SET_SPNODE_RIGHT(parent, node);                                      \
                    break;                                                                \
                } else {                                                                  \
                    parent = _GET_SPNODE_RIGHT(parent);                                   \
                }                                                                         \
            } else {                                                                      \
                if (_GET_SPNODE_LEFT(parent) == SPNIL) {                                  \
                    /* extra element can be needed for current balance implementation*/   \
                    int reserve_result = sptree_##name##_reserve_places(t, 2);            \
                    if (reserve_result)                                                   \
                        return reserve_result;        	                                  \
                    node = sptree_##name##_get_place(t);                                  \
                    memcpy(ITHELEM(t, node), v, t->elemsize);                             \
                    _SET_SPNODE_LEFT(parent, node);                                       \
                    break;                                                                \
                } else {                                                                  \
                    parent = _GET_SPNODE_LEFT(parent);                                    \
                }                                                                         \
            }                                                                             \
        }                                                                                 \
    }                                                                                     \
    if (p_old)                                                                            \
        *p_old = NULL;                                                                    \
                                                                                          \
    t->size++;                                                                            \
    if ( t->size > t->max_size )                                                          \
        t->max_size = t->size;                                                            \
    if ( depth > t->max_depth )                                                           \
        t->max_depth = depth;                                                             \
                                                                                          \
    if ( (double)depth > COUNTALPHA(t->size)) {                                           \
        spnode_t    parent;                                                               \
        spnode_t    i, size = 1 ;                                                         \
                                                                                          \
        path[depth] = node;                                                               \
                                                                                          \
        for (i = 1; ; i++) {                                                              \
            if (i < depth) {                                                              \
                parent = path[ depth - i ];                                               \
                size += 1 + sptree_##name##_size_of_subtree( t,                           \
                    _GET_SPNODE_RIGHT(parent) == path[depth - i + 1] ?                    \
                        _GET_SPNODE_LEFT(parent) :  _GET_SPNODE_RIGHT(parent));           \
                if ((double)i > COUNTALPHA(size)) {                                       \
                    spnode_t n = sptree_##name##_balance(t, parent, size);                \
                    spnode_t pp = path[  depth - i - 1 ];                                 \
                    if (_GET_SPNODE_LEFT(pp) == parent)                                   \
                        _SET_SPNODE_LEFT(pp, n);                                          \
                    else                                                                  \
                        _SET_SPNODE_RIGHT(pp, n);                                         \
                    break;                                                                \
                }                                                                         \
            } else {                                                                      \
                t->root = sptree_##name##_balance(t, t->root, t->size);                   \
                t->max_size = t->size;                                                    \
                break;                                                                    \
            }                                                                             \
        }                                                                                 \
    }                                                                                     \
    return 0;                                                                             \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
sptree_##name##_delete(sptree_##name *t, void *k) {                                       \
    spnode_t    node = t->root;                                                           \
    spnode_t    parent = SPNIL;                                                           \
    int            lr = 0;                                                                \
    while(node != SPNIL) {                                                                \
        int r = t->elemcompare(k, ITHELEM(t, node), t->arg);                              \
        if (r > 0) {                                                                      \
            parent = node;                                                                \
            node = _GET_SPNODE_RIGHT(node);                                               \
            lr = +1;                                                                      \
        } else if (r < 0) {                                                               \
            parent = node;                                                                \
            node = _GET_SPNODE_LEFT(node);                                                \
            lr = -1;                                                                      \
        } else {/* found */                                                               \
            if (_GET_SPNODE_LEFT(node) == SPNIL && _GET_SPNODE_RIGHT(node) == SPNIL) {    \
                if ( parent == SPNIL )                                                    \
                    t->root = SPNIL;                                                      \
                else if (lr <0)                                                           \
                    _SET_SPNODE_LEFT(parent, SPNIL);                                      \
                else                                                                      \
                    _SET_SPNODE_RIGHT(parent, SPNIL);                                     \
            } else if (_GET_SPNODE_LEFT(node) == SPNIL) {                                 \
                spnode_t    child = _GET_SPNODE_RIGHT(node);                              \
                if (parent == SPNIL) t->root = child;                                     \
                else if (lr <0) _SET_SPNODE_LEFT(parent, child);                          \
                else _SET_SPNODE_RIGHT(parent, child);                                    \
            } else if (_GET_SPNODE_RIGHT(node) == SPNIL) {                                \
                spnode_t    child = _GET_SPNODE_LEFT(node);                               \
                if (parent == SPNIL) t->root = child;                                     \
                else if (lr <0) _SET_SPNODE_LEFT(parent, child);                          \
                else _SET_SPNODE_RIGHT(parent, child);                                    \
            } else {                                                                      \
                spnode_t    todel = _GET_SPNODE_LEFT(node);                               \
                                                                                          \
                parent = SPNIL;                                                           \
                for(;;) {                                                                 \
                    if ( _GET_SPNODE_RIGHT(todel) != SPNIL ) {                            \
                        parent = todel;                                                   \
                        todel = _GET_SPNODE_RIGHT(todel);                                 \
                    } else                                                                \
                        break;                                                            \
                }                                                                         \
                memcpy(ITHELEM(t, node), ITHELEM(t, todel), t->elemsize);                 \
                if (parent != SPNIL)                                                      \
                    _SET_SPNODE_RIGHT(parent, _GET_SPNODE_LEFT(todel));                   \
                else                                                                      \
                    _SET_SPNODE_LEFT(node, _GET_SPNODE_LEFT(todel));                      \
                node = todel; /* node to delete */                                        \
            }                                                                             \
                                                                                          \
            _SET_SPNODE_LEFT(node, t->garbage_head);                                      \
            /*                                                                            \
             * Loop back on the right link indicates that the node                        \
             * is in the garbage list.                                                    \
             */                                                                           \
            _SET_SPNODE_RIGHT(node, node);                                                \
            t->garbage_head = node;                                                       \
                                                                                          \
            break;                                                                        \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    if (node == SPNIL) /* not found */                                                    \
        return;                                                                           \
                                                                                          \
    t->size --;                                                                           \
    if ( t->size > 0 && (double)t->size < alpha * t->max_size ) {                         \
        t->root = sptree_##name##_balance(t, t->root, t->size);                           \
        t->max_size = t->size;                                                            \
    }                                                                                     \
}                                                                                         \
                                                                                          \
static inline spnode_t                                                                    \
sptree_##name##_walk(sptree_##name *t, void* array, spnode_t limit, spnode_t offset) {    \
    int         level = 0;                                                                \
    spnode_t    count= 0,                                                                 \
                node,                                                                     \
                stack[ t->max_depth + 1 ];                                                \
                                                                                          \
    if (t->root == SPNIL) return 0;                                                       \
    stack[0] = t->root;                                                                   \
                                                                                          \
    while( (node = _GET_SPNODE_LEFT( stack[level] )) != SPNIL ) {                         \
        level++;                                                                          \
        stack[level] = node;                                                              \
    }                                                                                     \
                                                                                          \
    while( count < offset + limit && level >= 0 ) {                                       \
                                                                                          \
        if (count >= offset)                                                              \
             memcpy((char *) array + (count-offset) * t->elemsize,                        \
                    ITHELEM(t, stack[level]), t->elemsize);                               \
        count++;                                                                          \
                                                                                          \
        node = _GET_SPNODE_RIGHT( stack[level] );                                         \
        level--;                                                                          \
        while( node != SPNIL ) {                                                          \
            level++;                                                                      \
            stack[level] = node;                                                          \
            node = _GET_SPNODE_LEFT( stack[level] );                                      \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    return (count > offset) ? count - offset : 0;                                         \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
sptree_##name##_walk_cb(sptree_##name *t, int (*cb)(void*, void*), void *cb_arg ) {       \
    int         level = 0;                                                                \
    spnode_t    node,                                                                     \
                stack[ t->max_depth + 1 ];                                                \
                                                                                          \
    if (t->root == SPNIL) return;                                                         \
    stack[0] = t->root;                                                                   \
                                                                                          \
    while( (node = _GET_SPNODE_LEFT( stack[level] )) != SPNIL ) {                         \
        level++;                                                                          \
        stack[level] = node;                                                              \
    }                                                                                     \
                                                                                          \
    while( level >= 0 ) {                                                                 \
        if ( cb(cb_arg, ITHELEM(t, stack[level])) == 0 )                                  \
             return;                                                                      \
                                                                                          \
        node = _GET_SPNODE_RIGHT( stack[level] );                                         \
        level--;                                                                          \
        while( node != SPNIL ) {                                                          \
            level++;                                                                      \
            stack[level] = node;                                                          \
            node = _GET_SPNODE_LEFT( stack[level] );                                      \
        }                                                                                 \
    }                                                                                     \
}                                                                                         \
                                                                                          \
typedef struct sptree_##name##_iterator {                                                 \
    const sptree_##name        *t;                                                        \
    int                  level;                                                           \
    spnode_t             max_depth;                                                       \
    spnode_t             stack[0];                                                        \
} sptree_##name##_iterator;                                                               \
                                                                                          \
static inline sptree_##name##_iterator *                                                  \
sptree_##name##_iterator_alloc(sptree_##name *t) {                                        \
    sptree_##name##_iterator *i = (sptree_##name##_iterator *)                            \
        realloc(NULL, sizeof(*i) + sizeof(spnode_t) * (t->max_depth + 1));                \
    if (i) {                                                                              \
        i->t = t;                                                                         \
        i->level = 0;                                                                     \
        i->stack[0] = t->root;                                                            \
    }                                                                                     \
    return i;                                                                             \
}                                                                                         \
                                                                                          \
static inline sptree_##name##_iterator *                                                  \
sptree_##name##_iterator_init(sptree_##name *t) {                                         \
    sptree_##name##_iterator *i;                                                          \
    spnode_t node;                                                                        \
                                                                                          \
    if (t->root == SPNIL) return NULL;                                                    \
    i = sptree_##name##_iterator_alloc(t);                                                \
    if (!i)                                                                               \
        return i;                                                                         \
                                                                                          \
    while( (node = _GET_SPNODE_LEFT( i->stack[i->level] )) != SPNIL ) {                   \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
    }                                                                                     \
                                                                                          \
    return i;                                                                             \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
sptree_##name##_iterator_init_set(const sptree_##name *t, sptree_##name##_iterator **i,   \
                                  void *k) {                                              \
    spnode_t node;                                                                        \
    int      lastLevelEq = -1, cmp;                                                       \
                                                                                          \
    if ((*i) == NULL || t->max_depth > (*i)->max_depth) {                                 \
        sptree_##name##_iterator *new_i;                                                  \
        new_i = (sptree_##name##_iterator *) realloc(*i, sizeof(**i) +                    \
                                       sizeof(spnode_t) * (t->max_depth + 31));           \
        if (!new_i)                                                                       \
            return sizeof(**i) + sizeof(spnode_t) * (t->max_depth + 31);                  \
        *i = new_i;                                                                       \
    }                                                                                     \
                                                                                          \
    (*i)->t = t;                                                                          \
    (*i)->level = -1;                                                                     \
    if (t->root == SPNIL) {                                                               \
            (*i)->max_depth = 0; /* valgrind points out it's used in the check above ^.*/ \
            return 0;                                                                     \
    }                                                                                     \
                                                                                          \
    (*i)->max_depth = t->max_depth;                                                       \
    (*i)->stack[0] = t->root;                                                             \
                                                                                          \
    node = t->root;                                                                       \
    while(node != SPNIL) {                                                                \
        cmp = t->compare(k, ITHELEM(t, node), t->arg);                                    \
                                                                                          \
        (*i)->level++;                                                                    \
        (*i)->stack[(*i)->level] = node;                                                  \
                                                                                          \
        if (cmp > 0) {                                                                    \
            (*i)->level--; /* exclude current node from path, ie "mark as visited" */     \
            node = _GET_SPNODE_RIGHT(node);                                               \
        } else if (cmp < 0) {                                                             \
            node = _GET_SPNODE_LEFT(node);                                                \
        } else {                                                                          \
            lastLevelEq = (*i)->level;                                                    \
            node = _GET_SPNODE_LEFT(node); /* one way iterator: from left to right */     \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    if (lastLevelEq >= 0)                                                                 \
        (*i)->level = lastLevelEq;                                                        \
    return 0;                                                                             \
}                                                                                         \
                                                                                          \
static inline sptree_##name##_iterator *                                                  \
sptree_##name##_iterator_reverse_init(sptree_##name *t) {                                 \
    sptree_##name##_iterator *i;                                                          \
    spnode_t node;                                                                        \
                                                                                          \
    if (t->root == SPNIL) return NULL;                                                    \
    i = sptree_##name##_iterator_alloc(t);                                                \
    if (!i)                                                                               \
        return i;                                                                         \
                                                                                          \
    while( (node = _GET_SPNODE_RIGHT( i->stack[i->level] )) != SPNIL ) {                  \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
    }                                                                                     \
                                                                                          \
    return i;                                                                             \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
sptree_##name##_iterator_reverse_init_set(const sptree_##name *t,                         \
                                          sptree_##name##_iterator **i, void *k) {        \
    spnode_t node;                                                                        \
    int      lastLevelEq = -1, cmp;                                                       \
                                                                                          \
    if ((*i) == NULL || t->max_depth > (*i)->max_depth) {                                 \
        sptree_##name##_iterator *new_i;                                                  \
        new_i = (sptree_##name##_iterator *) realloc(*i, sizeof(**i) +                    \
                                       sizeof(spnode_t) * (t->max_depth + 31));           \
        if (!new_i)                                                                       \
            return sizeof(**i) + sizeof(spnode_t) * (t->max_depth + 31);                  \
        *i = new_i;                                                                       \
    }                                                                                     \
                                                                                          \
    (*i)->t = t;                                                                          \
    (*i)->level = -1;                                                                     \
    if (t->root == SPNIL) {                                                               \
            (*i)->max_depth = 0;                                                          \
            return 0;                                                                     \
    }                                                                                     \
                                                                                          \
    (*i)->max_depth = t->max_depth;                                                       \
    (*i)->stack[0] = t->root;                                                             \
                                                                                          \
    node = t->root;                                                                       \
    while(node != SPNIL) {                                                                \
        cmp = t->compare(k, ITHELEM(t, node), t->arg);                                    \
                                                                                          \
        (*i)->level++;                                                                    \
        (*i)->stack[(*i)->level] = node;                                                  \
                                                                                          \
        if (cmp < 0) {                                                                    \
            (*i)->level--;                                                                \
            node = _GET_SPNODE_LEFT(node);                                                \
        } else if (cmp > 0) {                                                             \
            node = _GET_SPNODE_RIGHT(node);                                               \
        } else {                                                                          \
            lastLevelEq = (*i)->level;                                                    \
            node = _GET_SPNODE_RIGHT(node);                                               \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    if (lastLevelEq >= 0)                                                                 \
        (*i)->level = lastLevelEq;                                                        \
    return 0;                                                                             \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
sptree_##name##_iterator_free(sptree_##name##_iterator *i) {                              \
    if (i == NULL)    return;                                                             \
    i = (sptree_##name##_iterator *)realloc(i, 0);                                        \
}                                                                                         \
                                                                                          \
/**                                                                                       \
 * Get the last node on the iterator stack, check                                         \
 * if the node is not deleted.                                                            \
 */                                                                                       \
static inline spnode_t                                                                    \
sptree_##name##_iterator_next_node(sptree_##name##_iterator *i) {                         \
                                                                                          \
    while (i->level >= 0) {                                                               \
        spnode_t return_node = i->stack[i->level--];                                      \
        if (! sptree_##name##_node_is_deleted(i->t, return_node))                         \
            return return_node;                                                           \
    }                                                                                     \
    return SPNIL;                                                                         \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
sptree_##name##_iterator_next(sptree_##name##_iterator *i) {                              \
                                                                                          \
    if (i == NULL)  return NULL;                                                          \
                                                                                          \
    const sptree_##name *t = i->t;                                                        \
    spnode_t returnNode = sptree_##name##_iterator_next_node(i);                          \
                                                                                          \
    if (returnNode == SPNIL) return NULL;                                                 \
                                                                                          \
    spnode_t node = _GET_SPNODE_RIGHT(returnNode);                                        \
    while (node != SPNIL) {                                                               \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
        node = _GET_SPNODE_LEFT(i->stack[i->level]);                                      \
    }                                                                                     \
                                                                                          \
    return ITHELEM(t, returnNode);                                                        \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
sptree_##name##_iterator_reverse_next(sptree_##name##_iterator *i) {                      \
                                                                                          \
    if (i == NULL)  return NULL;                                                          \
                                                                                          \
    const sptree_##name *t = i->t;                                                        \
    spnode_t returnNode = sptree_##name##_iterator_next_node(i);                          \
                                                                                          \
    if (returnNode == SPNIL) return NULL;                                                 \
                                                                                          \
    spnode_t node = _GET_SPNODE_LEFT(returnNode);                                         \
    while (node != SPNIL) {                                                               \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
        node = _GET_SPNODE_RIGHT(i->stack[i->level]);                                     \
    }                                                                                     \
    return ITHELEM(t, returnNode);                                                        \
}
/*
 * vim: ts=4 sts=4 et
 */
#endif

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
