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

#ifndef _AVLTREE_H_
#define _AVLTREE_H_

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>

#include <third_party/qsort_arg.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef AVLTREE_NODE_SELF
/*
 * user could suggest pointer's storage himself
 */
typedef u_int32_t avlnode_t;
#define AVLNIL (0x7fffffff)
#define AVLINDEXMASK (0x7fffffff)
#define AVLFLAGMASK (0x80000000)
#define AVLMAXDEPTH (48)

typedef struct avltree_node_pointers {
    u_int32_t    left;   /* sizeof(avlnode_t) >= sizeof(avltree_node_pointers.left) !!! */
    u_int32_t    right;
} avltree_node_pointers;

#define GET_AVLNODE_LEFT(snp) ((snp)->left & AVLINDEXMASK)
#define SET_AVLNODE_LEFT(snp, v) ((snp)->left = (((snp)->left & AVLFLAGMASK) | (v)))
#define GET_AVLNODE_RIGHT(snp) ((snp)->right & AVLINDEXMASK)
#define SET_AVLNODE_RIGHT(snp, v) ((snp)->right = (((snp)->right & AVLFLAGMASK) | (v)))
#define GET_AVLNODE_BALANCE(snp) (((snp)->right >> 31) ? 1 : ((snp)->left >> 31) ? -1 : 0)
#define SET_AVLNODE_BALANCE(snp, v) ((snp)->left = (((snp)->left & AVLINDEXMASK) | ((v) < 0 ? AVLFLAGMASK : 0))), ((snp)->right = (((snp)->right & AVLINDEXMASK) | ((v) > 0 ? AVLFLAGMASK : 0)))

#endif /* AVLTREE_NODE_SELF */

#define _GET_AVLNODE_LEFT(n) GET_AVLNODE_LEFT( t->lrpointers + (n) )
#define _SET_AVLNODE_LEFT(n, v) SET_AVLNODE_LEFT( t->lrpointers + (n), (v) )
#define _GET_AVLNODE_RIGHT(n) GET_AVLNODE_RIGHT( t->lrpointers + (n) )
#define _SET_AVLNODE_RIGHT(n, v) SET_AVLNODE_RIGHT( t->lrpointers + (n), (v) )
#define _GET_AVLNODE_BALANCE(n) GET_AVLNODE_BALANCE( t->lrpointers + (n) )
#define _SET_AVLNODE_BALANCE(n, v) SET_AVLNODE_BALANCE( t->lrpointers + (n), (v) )

#define AVLITHELEM(t, i) ( (char *) (t)->members + (t)->elemsize * (i) )
#define AVLELEMIDX(t, e) ( ((e) - (t)->members) / (t)->elemsize )

/*
 * makes definition of tree with methods, name should
 * be unique across all definitions.
 *
 * Methods:
 *   void avltree_NAME_init(avltree_NAME *tree, size_t elemsize, void *array,
 *                         avlnode_t array_len, avlnode_t array_size,
 *                         int (*compare)(const void *key, const void *elem, void *arg),
 *                         int (*elemcompare)(const void *e1, const void *e2, void *arg),
 *                         void *arg)
 *
 *   void avltree_NAME_replace(avltree_NAME *tree, void *value, void **p_oldvalue)
 *   void avltree_NAME_delete(avltree_NAME *tree, void *value)
 *   void* avltree_NAME_find(avltree_NAME *tree, void *key)
 *
 *   avlnode_t avltree_NAME_walk(avltree_NAME *t, void* array, avlnode_t limit, avlnode_t offset)
 *   void avltree_NAME_walk_cb(avltree_NAME *t, int (*cb)(void* cb_arg, void* elem), void *cb_arg)
 *
 *   avltree_NAME_iterator* avltree_NAME_iterator_init(avltree_NAME *t)
 *   void avltree_NAME_iterator_init_set(avltree_NAME *t, avltree_NAME_iterator **iterator, void *start)
 *   avltree_NAME_iterator* avltree_NAME_iterator_reverse_init(avltree_NAME *t)
 *   void avltree_NAME_iterator_reverse_init_set(avltree_NAME *t, avltree_NAME_iterator **iterator, void *start)
 *   void avltree_NAME_iterator_free(avltree_NAME_iterator *i)
 *
 *   void* avltree_NAME_iterator_next(avltree_NAME_iterator *i)
 *   void* avltree_NAME_iterator_reverse_next(avltree_NAME_iterator *i)
 */

#define AVL_DEF(name, realloc)                                                            \
typedef struct avl_##name {                                                               \
    void                    *members;                                                     \
    avltree_node_pointers    *lrpointers;                                                 \
                                                                                          \
    avlnode_t                nmember;                                                     \
    avlnode_t                ntotal;                                                      \
                                                                                          \
    int                     (*compare)(const void *key, const void *elem, void *);        \
    int                     (*elemcompare)(const void *e1, const void *e2, void *);       \
    void*                   arg;                                                          \
    size_t                  elemsize;                                                     \
                                                                                          \
    avlnode_t                root;                                                        \
    avlnode_t                garbage_head;                                                \
    avlnode_t                size;                                                        \
    avlnode_t                max_size;                                                    \
    avlnode_t                max_depth;                                                   \
} avl_##name;                                                                             \
                                                                                          \
static avlnode_t                                                                          \
avl_##name##_mktree(avl_##name *t, avlnode_t depth,                                       \
		      avlnode_t start, avlnode_t end, int* height) {                      \
    avlnode_t    half = ( (end + start) >> 1 ), tmp;                                      \
    int lh = 0, rh = 0;                                                                   \
                                                                                          \
    if (depth > t->max_depth) t->max_depth = depth;                                       \
                                                                                          \
    if ( half == start ||                                                                 \
            ( tmp = avl_##name##_mktree(t, depth+1, start, half, &lh)) == half )          \
        _SET_AVLNODE_LEFT(half, AVLNIL);                                                  \
    else                                                                                  \
        _SET_AVLNODE_LEFT(half, tmp);                                                     \
    if ( half+1 >= end ||                                                                 \
            ( tmp = avl_##name##_mktree(t, depth+1, half+1, end, &rh)) == half )          \
        _SET_AVLNODE_RIGHT(half, AVLNIL);                                                 \
    else                                                                                  \
        _SET_AVLNODE_RIGHT(half, tmp);                                                    \
                                                                                          \
    _SET_AVLNODE_BALANCE(half, rh - lh);                                                  \
                                                                                          \
	if(height) {                                                                      \
		*height = (lh > rh ? lh : rh) + 1;                                        \
	}                                                                                 \
                                                                                          \
    return half;                                                                          \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
avl_##name##_height_of_subtree(avl_##name *t, avlnode_t node) {                           \
    if (node == AVLNIL)                                                                   \
	    return 0;                                                                     \
	int l = avl_##name##_height_of_subtree(t, _GET_AVLNODE_LEFT(node));               \
	int r = avl_##name##_height_of_subtree(t, _GET_AVLNODE_RIGHT(node));              \
    return 1 + (l > r ? l : r);                                                           \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
avl_##name##_check_subtree(avl_##name *t, avlnode_t node) {                               \
    if (node == AVLNIL)                                                                   \
	    return 0;                                                                     \
	if(_GET_AVLNODE_LEFT(node) != AVLNIL) {                                           \
		void* l = AVLITHELEM(t, _GET_AVLNODE_LEFT(node));                         \
		void* c = AVLITHELEM(t, node);                                            \
		if(t->elemcompare(l, c, t->arg) >= 0) {                                   \
			return 1;                                                         \
		}                                                                         \
	}                                                                                 \
	if(_GET_AVLNODE_RIGHT(node) != AVLNIL) {                                          \
		void* r = AVLITHELEM(t, _GET_AVLNODE_RIGHT(node));                        \
		void* c = AVLITHELEM(t, node);                                            \
		if(t->elemcompare(c, r, t->arg) >= 0) {                                   \
			return 2;                                                         \
		}                                                                         \
	}                                                                                 \
	int lh = avl_##name##_height_of_subtree(t, _GET_AVLNODE_LEFT(node));              \
	int rh = avl_##name##_height_of_subtree(t, _GET_AVLNODE_RIGHT(node));             \
	if(rh - lh != _GET_AVLNODE_BALANCE(node)) {                                       \
		return 4;                                                                 \
	}                                                                                 \
	avlnode_t l = avl_##name##_check_subtree(t, _GET_AVLNODE_LEFT(node));             \
	avlnode_t r = avl_##name##_check_subtree(t, _GET_AVLNODE_RIGHT(node));            \
    return l | r;                                                                         \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
avl_##name##_init(avl_##name *t, size_t elemsize, void *m,                                \
                     avlnode_t nm, avlnode_t nt,                                          \
                     int (*compare)(const void *, const void *, void *),                  \
                     int (*elemcompare)(const void *, const void *, void *),              \
                     void *arg) {                                                         \
    memset(t, 0, sizeof(*t));                                                             \
    t->members = m;                                                                       \
    t->max_size = t->size = t->nmember = nm;                                              \
    t->ntotal = (nt==0) ? nm : nt;                                                        \
    t->compare = compare != NULL ? compare : elemcompare;                                 \
    t->elemcompare = elemcompare != NULL ? elemcompare : compare;                         \
    t->arg = arg;                                                                         \
    t->elemsize = elemsize;                                                               \
    t->garbage_head = t->root = AVLNIL;                                                   \
                                                                                          \
    if (t->ntotal == 0 || t->members == NULL) { /* from scratch */                        \
        if (t->ntotal == 0) {                                                             \
            t->members = NULL;                                                            \
            t->ntotal = 64;                                                               \
        }                                                                                 \
                                                                                          \
        if (t->members == NULL)                                                           \
            t->members = realloc(NULL, elemsize * t->ntotal);                             \
    }                                                                                     \
    t->lrpointers = (avltree_node_pointers *) realloc(NULL,                               \
                                sizeof(avltree_node_pointers) * t->ntotal);               \
                                                                                          \
    if (t->nmember == 1) {                                                                \
        t->root = 0;                                                                      \
        _SET_AVLNODE_RIGHT(0, AVLNIL);                                                    \
        _SET_AVLNODE_LEFT(0, AVLNIL);                                                     \
    } else if (t->nmember > 1)    {                                                       \
        qsort_arg(t->members, t->nmember, elemsize, t->elemcompare, t->arg);              \
        /* create tree */                                                                 \
        t->root = avl_##name##_mktree(t, 1, 0, t->nmember, 0);                            \
        /*avl_##name##_check_subtree(t, t->root);*/                                       \
    }                                                                                     \
    if (t->members && t->lrpointers)                                                      \
        return 0;                                                                         \
    else if (t->members)                                                                  \
        return t->ntotal * sizeof(avltree_node_pointers);                                 \
    else if (t->lrpointers)                                                               \
        return t->ntotal * elemsize;                                                      \
    else                                                                                  \
        return t->ntotal * (sizeof(avltree_node_pointers) + elemsize);                    \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
avl_##name##_destroy(avl_##name *t) {                                                     \
        if (t == NULL)    return;                                                         \
    t->members = realloc(t->members, 0);                                                  \
    t->lrpointers = (avltree_node_pointers *)realloc(t->lrpointers, 0);                   \
}                                                                                         \
                                                                                          \
/** Nodes in the garbage list have a loop on their right link. */                         \
static inline bool                                                                        \
avl_##name##_node_is_deleted(const avl_##name *t, avlnode_t node) {                       \
                                                                                          \
    return _GET_AVLNODE_RIGHT(node) == node;                                              \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
avl_##name##_find(const avl_##name *t, void *k) {                                         \
    avlnode_t    node = t->root;                                                          \
    while(node != AVLNIL) {                                                               \
    int r = t->compare(k, AVLITHELEM(t, node), t->arg);                                   \
        if (r > 0) {                                                                      \
            node = _GET_AVLNODE_RIGHT(node);                                              \
        } else if (r < 0) {                                                               \
            node = _GET_AVLNODE_LEFT(node);                                               \
        } else {                                                                          \
            return AVLITHELEM(t, node);                                                   \
        }                                                                                 \
    }                                                                                     \
    return NULL;                                                                          \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
avl_##name##_first(const avl_##name *t) {                                                 \
    avlnode_t    node = t->root;                                                          \
    avlnode_t    first = AVLNIL;                                                          \
    while (node != AVLNIL) {                                                              \
            first = node;                                                                 \
            node = _GET_AVLNODE_LEFT(node);                                               \
    }                                                                                     \
    if (first != AVLNIL)                                                                  \
        return AVLITHELEM(t, first);                                                      \
    return NULL;                                                                          \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
avl_##name##_last(const avl_##name *t) {                                                  \
    avlnode_t    node = t->root;                                                          \
    avlnode_t    last = AVLNIL;                                                           \
    while (node != AVLNIL) {                                                              \
            last = node;                                                                  \
            node = _GET_AVLNODE_RIGHT(node);                                              \
    }                                                                                     \
    if (last != AVLNIL)                                                                   \
        return AVLITHELEM(t, last);                                                       \
    return NULL;                                                                          \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
avl_##name##_random(const avl_##name *t, avlnode_t rnd) {                                 \
    for (avlnode_t i = 0; i < t->size; i++, rnd++) {                                      \
        rnd %= t->nmember;                                                                \
        if (!avl_##name##_node_is_deleted(t, rnd))                                        \
            return AVLITHELEM(t, rnd);                                                    \
                                                                                          \
    }                                                                                     \
                                                                                          \
    return NULL;                                                                          \
}                                                                                         \
static inline avlnode_t                                                                   \
avl_##name##_size_of_subtree(avl_##name *t, avlnode_t node) {                             \
    if (node == AVLNIL)                                                                   \
        return 0;                                                                         \
    return 1 +                                                                            \
        avl_##name##_size_of_subtree(t, _GET_AVLNODE_LEFT(node)) +                        \
        avl_##name##_size_of_subtree(t, _GET_AVLNODE_RIGHT(node));                        \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
avl_##name##_reserve_places(avl_##name *t, avlnode_t nreserve) {                          \
	avlnode_t num_free = t->ntotal - t->size;                                         \
	if (num_free >= nreserve)                                                         \
		return 0;                                                                 \
	avlnode_t new_ntotal = MAX(t->ntotal * 2, t->ntotal + nreserve - num_free);       \
	void *new_members = realloc(t->members, new_ntotal * t->elemsize);                \
	if (!new_members)                                                                 \
		return new_ntotal * t->elemsize;                                          \
	t->members = new_members;                                                         \
	avltree_node_pointers *new_lrpointers = (avltree_node_pointers *)                 \
	realloc(t->lrpointers, new_ntotal * sizeof(avltree_node_pointers));               \
	if (!new_lrpointers)                                                              \
		return new_ntotal * sizeof(avltree_node_pointers);                        \
	t->lrpointers = new_lrpointers;                                                   \
	t->ntotal = new_ntotal;                                                           \
	return 0;                                                                         \
}                                                                                         \
                                                                                          \
static inline avlnode_t                                                                   \
avl_##name##_get_place(avl_##name *t) {                                                   \
    avlnode_t    node;                                                                    \
    if (t->garbage_head != AVLNIL) {                                                      \
        node = t->garbage_head;                                                           \
        t->garbage_head = _GET_AVLNODE_LEFT(t->garbage_head);                             \
    } else {                                                                              \
        if (t->nmember >= t->ntotal) {                                                    \
            avlnode_t new_ntotal = t->ntotal * 2;                                         \
            t->members = realloc(t->members, new_ntotal * t->elemsize);                   \
            t->lrpointers = (avltree_node_pointers *) realloc(t->lrpointers,              \
                            new_ntotal * sizeof(avltree_node_pointers));                  \
            t->ntotal = new_ntotal;                                                       \
        }                                                                                 \
                                                                                          \
        node = t->nmember;                                                                \
        t->nmember++;                                                                     \
    }                                                                                     \
    _SET_AVLNODE_LEFT(node, AVLNIL);                                                      \
    _SET_AVLNODE_RIGHT(node, AVLNIL);                                                     \
    _SET_AVLNODE_BALANCE(node, 0);                                                        \
    return node;                                                                          \
}                                                                                         \
                                                                                          \
static inline bool                                                                        \
avl_##name##_rotate_left(avl_##name *t, avlnode_t parent, avlnode_t *new_parent) {        \
	avlnode_t node = _GET_AVLNODE_RIGHT(parent);                                      \
	if(_GET_AVLNODE_BALANCE(node) > 0) {                                              \
		_SET_AVLNODE_BALANCE(parent, 0);                                          \
		_SET_AVLNODE_BALANCE(node, 0);                                            \
		_SET_AVLNODE_RIGHT(parent, _GET_AVLNODE_LEFT(node));                      \
		_SET_AVLNODE_LEFT(node, parent);                                          \
		*new_parent = node;                                                       \
		return true;                                                              \
	} else if(_GET_AVLNODE_BALANCE(node) == 0) {                                      \
		_SET_AVLNODE_BALANCE(parent, 1);                                          \
		_SET_AVLNODE_BALANCE(node, -1);                                           \
		_SET_AVLNODE_RIGHT(parent, _GET_AVLNODE_LEFT(node));                      \
		_SET_AVLNODE_LEFT(node, parent);                                          \
		*new_parent = node;                                                       \
		return false;                                                             \
	} else {                                                                          \
		avlnode_t l = _GET_AVLNODE_LEFT(node);                                    \
		avlnode_t ll = _GET_AVLNODE_LEFT(l);                                      \
		avlnode_t lr = _GET_AVLNODE_RIGHT(l);                                     \
		int l_balance = _GET_AVLNODE_BALANCE(l);                                  \
		_SET_AVLNODE_BALANCE(l, 0);                                               \
		_SET_AVLNODE_BALANCE(node, l_balance < 0 ? 1 : 0);                        \
		_SET_AVLNODE_BALANCE(parent, l_balance > 0 ? -1 : 0);                     \
		_SET_AVLNODE_RIGHT(parent, ll);                                           \
		_SET_AVLNODE_LEFT(node, lr);                                              \
		_SET_AVLNODE_LEFT(l, parent);                                             \
		_SET_AVLNODE_RIGHT(l, node);                                              \
		*new_parent = l;                                                          \
		return true;                                                              \
	}                                                                                 \
}                                                                                         \
                                                                                          \
static inline bool                                                                        \
avl_##name##_rotate_right(avl_##name *t, avlnode_t parent, avlnode_t *new_parent) {       \
	avlnode_t node = _GET_AVLNODE_LEFT(parent);                                       \
	if(_GET_AVLNODE_BALANCE(node) < 0) {                                              \
		_SET_AVLNODE_BALANCE(parent, 0);                                          \
		_SET_AVLNODE_BALANCE(node, 0);                                            \
		_SET_AVLNODE_LEFT(parent, _GET_AVLNODE_RIGHT(node));                      \
		_SET_AVLNODE_RIGHT(node, parent);                                         \
		*new_parent = node;                                                       \
		return true;                                                              \
	} else if(_GET_AVLNODE_BALANCE(node) == 0) {                                      \
		_SET_AVLNODE_BALANCE(parent, -1);                                         \
		_SET_AVLNODE_BALANCE(node, 1);                                            \
		_SET_AVLNODE_LEFT(parent, _GET_AVLNODE_RIGHT(node));                      \
		_SET_AVLNODE_RIGHT(node, parent);                                         \
		*new_parent = node;                                                       \
		return false;                                                             \
	} else {                                                                          \
		avlnode_t r = _GET_AVLNODE_RIGHT(node);                                   \
		avlnode_t rl = _GET_AVLNODE_LEFT(r);                                      \
		avlnode_t rr = _GET_AVLNODE_RIGHT(r);                                     \
		int r_balance = _GET_AVLNODE_BALANCE(r);                                  \
		_SET_AVLNODE_BALANCE(r, 0);                                               \
		_SET_AVLNODE_BALANCE(node, r_balance > 0 ? -1 : 0);                       \
		_SET_AVLNODE_BALANCE(parent, r_balance < 0 ? 1 : 0);                      \
		_SET_AVLNODE_LEFT(parent, rr);                                            \
		_SET_AVLNODE_RIGHT(node, rl);                                             \
		_SET_AVLNODE_RIGHT(r, parent);                                            \
		_SET_AVLNODE_LEFT(r, node);                                               \
		*new_parent = r;                                                          \
		return true;                                                              \
	}                                                                                 \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
avl_##name##_replace(avl_##name *t, void *v, void **p_old) {                              \
    avlnode_t    node, depth = 0;                                                         \
    avlnode_t    path[ AVLMAXDEPTH ];                                                     \
                                                                                          \
    if (t->root == AVLNIL) {                                                              \
        _SET_AVLNODE_LEFT(0, AVLNIL);                                                     \
        _SET_AVLNODE_RIGHT(0, AVLNIL);                                                    \
        _SET_AVLNODE_BALANCE(0, 0);                                                       \
        memcpy(t->members, v, t->elemsize);                                               \
        t->root = 0;                                                                      \
        t->garbage_head = AVLNIL;                                                         \
        t->nmember = 1;                                                                   \
        t->size=1;                                                                        \
        if (p_old)                                                                        \
            *p_old = NULL;                                                                \
        return 0;                                                                         \
    } else {                                                                              \
        avlnode_t    parent = t->root;                                                    \
                                                                                          \
        for(;;)    {                                                                      \
            int r = t->elemcompare(v, AVLITHELEM(t, parent), t->arg);                     \
            if (r==0) {                                                                   \
                if (p_old)                                                                \
                    memcpy(*p_old, AVLITHELEM(t, parent), t->elemsize);                   \
                memcpy(AVLITHELEM(t, parent), v, t->elemsize);                            \
                return 0;                                                                 \
            }                                                                             \
            path[depth] = parent;                                                         \
            depth++;                                                                      \
            if (r>0) {                                                                    \
                if (_GET_AVLNODE_RIGHT(parent) == AVLNIL) {                               \
                    int reserve_result = avl_##name##_reserve_places(t, 1);               \
                    if (reserve_result)                                                   \
                        return reserve_result;                                            \
                    node = avl_##name##_get_place(t);                                     \
                    memcpy(AVLITHELEM(t, node), v, t->elemsize);                          \
                    _SET_AVLNODE_RIGHT(parent, node);                                     \
                    break;                                                                \
                } else {                                                                  \
                    parent = _GET_AVLNODE_RIGHT(parent);                                  \
                }                                                                         \
            } else {                                                                      \
                if (_GET_AVLNODE_LEFT(parent) == AVLNIL) {                                \
                    int reserve_result = avl_##name##_reserve_places(t, 1);               \
                    if (reserve_result)                                                   \
                        return reserve_result;                                            \
                    node = avl_##name##_get_place(t);                                     \
                    memcpy(AVLITHELEM(t, node), v, t->elemsize);                          \
                    _SET_AVLNODE_LEFT(parent, node);                                      \
                    break;                                                                \
                } else {                                                                  \
                    parent = _GET_AVLNODE_LEFT(parent);                                   \
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
	path[depth] = node;                                                               \
    while(depth > 0) {                                                                    \
        avlnode_t node = path[depth];                                                     \
        avlnode_t parent = path[depth - 1];                                               \
        if(_GET_AVLNODE_RIGHT(parent) == node) {                                          \
        	if(_GET_AVLNODE_BALANCE(parent) < 0) {                                    \
        		_SET_AVLNODE_BALANCE(parent, 0);                                  \
        		break;                                                            \
        	} else if(_GET_AVLNODE_BALANCE(parent) == 0) {                            \
        		_SET_AVLNODE_BALANCE(parent, 1);                                  \
        	} else {                                                                  \
        		bool should_break =                                               \
        			avl_##name##_rotate_left(t, parent, path + depth - 1);    \
        		if(depth > 1) {                                                   \
        			if(_GET_AVLNODE_LEFT(path[depth-2]) == parent) {          \
        				_SET_AVLNODE_LEFT(path[depth-2], path[depth-1]);  \
        			} else {                                                  \
        				_SET_AVLNODE_RIGHT(path[depth-2], path[depth-1]); \
        			}                                                         \
        		}                                                                 \
        		if(should_break) {                                                \
        			break;                                                    \
        		}                                                                 \
        	}                                                                         \
        } else {                                                                          \
        	if(_GET_AVLNODE_BALANCE(parent) > 0) {                                    \
        		_SET_AVLNODE_BALANCE(parent, 0);                                  \
        		break;                                                            \
        	} else if(_GET_AVLNODE_BALANCE(parent) == 0) {                            \
        		_SET_AVLNODE_BALANCE(parent, -1);                                 \
        	} else {                                                                  \
        		bool should_break =                                               \
        			avl_##name##_rotate_right(t, parent, path + depth - 1);   \
        		if(depth > 1) {                                                   \
        			if(_GET_AVLNODE_LEFT(path[depth-2]) == parent) {          \
        				_SET_AVLNODE_LEFT(path[depth-2], path[depth-1]);  \
        			} else {                                                  \
        				_SET_AVLNODE_RIGHT(path[depth-2], path[depth-1]); \
        			}                                                         \
        		}                                                                 \
        		if(should_break) {                                                \
        			break;                                                    \
        		}                                                                 \
        	}                                                                         \
        }                                                                                 \
        depth--;                                                                          \
    }                                                                                     \
    t->root = path[0];                                                                    \
    /*avl_##name##_check_subtree(t, t->root);*/                                           \
    return 0;                                                                             \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
avl_##name##_delete(avl_##name *t, void *k) {                                             \
    avlnode_t    node = t->root;                                                          \
    avlnode_t    parent = AVLNIL;                                                         \
    avlnode_t    depth = 0;                                                               \
    avlnode_t    path[AVLMAXDEPTH];                                                       \
    int            lr = 0;                                                                \
    while(node != AVLNIL) {                                                               \
        path[depth++] = node;                                                             \
        int r = t->elemcompare(k, AVLITHELEM(t, node), t->arg);                           \
        if (r > 0) {                                                                      \
            parent = node;                                                                \
            node = _GET_AVLNODE_RIGHT(node);                                              \
            lr = +1;                                                                      \
        } else if (r < 0) {                                                               \
            parent = node;                                                                \
            node = _GET_AVLNODE_LEFT(node);                                               \
            lr = -1;                                                                      \
        } else {/* found */                                                               \
            if (_GET_AVLNODE_LEFT(node) == AVLNIL && _GET_AVLNODE_RIGHT(node) == AVLNIL) {\
                path[depth-1] = AVLNIL;                                                   \
                if ( parent == AVLNIL )                                                   \
                    t->root = AVLNIL;                                                     \
                else if (lr <0)                                                           \
                    _SET_AVLNODE_LEFT(parent, AVLNIL);                                    \
                else                                                                      \
                    _SET_AVLNODE_RIGHT(parent, AVLNIL);                                   \
            } else if (_GET_AVLNODE_LEFT(node) == AVLNIL) {                               \
                avlnode_t    child = _GET_AVLNODE_RIGHT(node);                            \
                path[depth-1] = child;                                                    \
                if (parent == AVLNIL) t->root = child;                                    \
                else if (lr <0) _SET_AVLNODE_LEFT(parent, child);                         \
                else _SET_AVLNODE_RIGHT(parent, child);                                   \
            } else if (_GET_AVLNODE_RIGHT(node) == AVLNIL) {                              \
                avlnode_t    child = _GET_AVLNODE_LEFT(node);                             \
                path[depth-1] = child;                                                    \
                if (parent == AVLNIL) t->root = child;                                    \
                else if (lr <0) _SET_AVLNODE_LEFT(parent, child);                         \
                else _SET_AVLNODE_RIGHT(parent, child);                                   \
            } else {                                                                      \
                avlnode_t todel;                                                          \
                if (_GET_AVLNODE_BALANCE(node) >= 0) {                                    \
                    todel = _GET_AVLNODE_RIGHT(node);                                     \
            	    path[depth++] = todel;                                                \
                    parent = AVLNIL;                                                      \
                    lr = 1;                                                               \
                    for(;;) {                                                             \
                        if ( _GET_AVLNODE_LEFT(todel) != AVLNIL ) {                       \
                            parent = todel;                                               \
                            todel = _GET_AVLNODE_LEFT(todel);                             \
                    	    path[depth++] = todel;                                        \
                            lr = -1;                                                      \
                        } else                                                            \
                            break;                                                        \
                    }                                                                     \
                    memcpy(AVLITHELEM(t, node), AVLITHELEM(t, todel), t->elemsize);       \
                    if (parent != AVLNIL)                                                 \
                        _SET_AVLNODE_LEFT(parent, _GET_AVLNODE_RIGHT(todel));             \
                    else                                                                  \
                        _SET_AVLNODE_RIGHT(node, _GET_AVLNODE_RIGHT(todel));              \
                } else {                                                                  \
                    todel = _GET_AVLNODE_LEFT(node);                                      \
            	    path[depth++] = todel;                                                \
                    parent = AVLNIL;                                                      \
                    lr = -1;                                                              \
                    for(;;) {                                                             \
                        if ( _GET_AVLNODE_RIGHT(todel) != AVLNIL ) {                      \
                            parent = todel;                                               \
                            todel = _GET_AVLNODE_RIGHT(todel);                            \
                    	    path[depth++] = todel;                                        \
                            lr = 1;                                                       \
                        } else                                                            \
                            break;                                                        \
                    }                                                                     \
                    memcpy(AVLITHELEM(t, node), AVLITHELEM(t, todel), t->elemsize);       \
                    if (parent != AVLNIL)                                                 \
                        _SET_AVLNODE_RIGHT(parent, _GET_AVLNODE_LEFT(todel));             \
                    else                                                                  \
                        _SET_AVLNODE_LEFT(node, _GET_AVLNODE_LEFT(todel));                \
                }                                                                         \
                node = todel; /* node to delete */                                        \
            }                                                                             \
                                                                                          \
            _SET_AVLNODE_LEFT(node, t->garbage_head);                                     \
            /*                                                                            \
             * Loop back on the right link indicates that the node                        \
             * is in the garbage list.                                                    \
             */                                                                           \
            _SET_AVLNODE_RIGHT(node, node);                                               \
            t->garbage_head = node;                                                       \
                                                                                          \
            break;                                                                        \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    if (node == AVLNIL) /* not found */                                                   \
        return;                                                                           \
                                                                                          \
    t->size --;                                                                           \
                                                                                          \
    depth--;                                                                              \
    while(depth > 0) {                                                                    \
        avlnode_t node = path[depth];                                                     \
        avlnode_t parent = path[depth - 1];                                               \
        if(lr == 1 || (lr == 0 && _GET_AVLNODE_RIGHT(parent) == node)) {                  \
        	if(_GET_AVLNODE_BALANCE(parent) == 0) {                                   \
        		_SET_AVLNODE_BALANCE(parent, -1);                                 \
        		break;                                                            \
        	} else if(_GET_AVLNODE_BALANCE(parent) == 1) {                            \
        		_SET_AVLNODE_BALANCE(parent, 0);                                  \
        	} else {                                                                  \
        		bool should_break =                                               \
        			!avl_##name##_rotate_right(t, parent, path + depth - 1);  \
        		if(depth > 1) {                                                   \
        			if(_GET_AVLNODE_LEFT(path[depth-2]) == parent) {          \
        				_SET_AVLNODE_LEFT(path[depth-2], path[depth-1]);  \
        			} else {                                                  \
        				_SET_AVLNODE_RIGHT(path[depth-2], path[depth-1]); \
        			}                                                         \
        		}                                                                 \
        		if(should_break) {                                                \
        			break;                                                    \
        		}                                                                 \
        	}                                                                         \
        } else {                                                                          \
        	if(_GET_AVLNODE_BALANCE(parent) == 0) {                                   \
        		_SET_AVLNODE_BALANCE(parent, 1);                                  \
        		break;                                                            \
        	} else if(_GET_AVLNODE_BALANCE(parent) == -1) {                           \
        		_SET_AVLNODE_BALANCE(parent, 0);                                  \
        	} else {                                                                  \
        		bool should_break =                                               \
        			!avl_##name##_rotate_left(t, parent, path + depth - 1);   \
        		if(depth > 1) {                                                   \
        			if(_GET_AVLNODE_LEFT(path[depth-2]) == parent) {          \
        				_SET_AVLNODE_LEFT(path[depth-2], path[depth-1]);  \
        			} else {                                                  \
        				_SET_AVLNODE_RIGHT(path[depth-2], path[depth-1]); \
        			}                                                         \
        		}                                                                 \
        		if(should_break) {                                                \
        			break;                                                    \
        		}                                                                 \
        	}                                                                         \
        }                                                                                 \
        lr = 0;                                                                           \
        depth--;                                                                          \
    }                                                                                     \
    t->root = path[0];                                                                    \
    /*avl_##name##_check_subtree(t, t->root);*/                                           \
                                                                                          \
}                                                                                         \
                                                                                          \
static inline avlnode_t                                                                   \
avl_##name##_walk(avl_##name *t, void* array, avlnode_t limit, avlnode_t offset) {        \
    int         level = 0;                                                                \
    avlnode_t    count= 0,                                                                \
                node,                                                                     \
                stack[ t->max_depth + 1 ];                                                \
                                                                                          \
    if (t->root == AVLNIL) return 0;                                                      \
    stack[0] = t->root;                                                                   \
                                                                                          \
    while( (node = _GET_AVLNODE_LEFT( stack[level] )) != AVLNIL ) {                       \
        level++;                                                                          \
        stack[level] = node;                                                              \
    }                                                                                     \
                                                                                          \
    while( count < offset + limit && level >= 0 ) {                                       \
                                                                                          \
        if (count >= offset)                                                              \
             memcpy((char *) array + (count-offset) * t->elemsize,                        \
                    AVLITHELEM(t, stack[level]), t->elemsize);                            \
        count++;                                                                          \
                                                                                          \
        node = _GET_AVLNODE_RIGHT( stack[level] );                                        \
        level--;                                                                          \
        while( node != AVLNIL ) {                                                         \
            level++;                                                                      \
            stack[level] = node;                                                          \
            node = _GET_AVLNODE_LEFT( stack[level] );                                     \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    return (count > offset) ? count - offset : 0;                                         \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
avl_##name##_walk_cb(avl_##name *t, int (*cb)(void*, void*), void *cb_arg ) {             \
    int         level = 0;                                                                \
    avlnode_t    node,                                                                    \
                stack[ t->max_depth + 1 ];                                                \
                                                                                          \
    if (t->root == AVLNIL) return;                                                        \
    stack[0] = t->root;                                                                   \
                                                                                          \
    while( (node = _GET_AVLNODE_LEFT( stack[level] )) != AVLNIL ) {                       \
        level++;                                                                          \
        stack[level] = node;                                                              \
    }                                                                                     \
                                                                                          \
    while( level >= 0 ) {                                                                 \
        if ( cb(cb_arg, AVLITHELEM(t, stack[level])) == 0 )                               \
             return;                                                                      \
                                                                                          \
        node = _GET_AVLNODE_RIGHT( stack[level] );                                        \
        level--;                                                                          \
        while( node != AVLNIL ) {                                                         \
            level++;                                                                      \
            stack[level] = node;                                                          \
            node = _GET_AVLNODE_LEFT( stack[level] );                                     \
        }                                                                                 \
    }                                                                                     \
}                                                                                         \
                                                                                          \
typedef struct avl_##name##_iterator {                                                    \
    const avl_##name        *t;                                                           \
    int                  level;                                                           \
    int                  max_depth;                                                       \
    avlnode_t             stack[0];                                                       \
} avl_##name##_iterator;                                                                  \
                                                                                          \
static inline avl_##name##_iterator *                                                     \
avl_##name##_iterator_alloc(avl_##name *t) {                                              \
    avl_##name##_iterator *i = (avl_##name##_iterator *)                                  \
        realloc(NULL, sizeof(*i) + sizeof(avlnode_t) * (t->max_depth + 1));               \
    if (i) {                                                                              \
        i->t = t;                                                                         \
        i->level = 0;                                                                     \
        i->stack[0] = t->root;                                                            \
    }                                                                                     \
    return i;                                                                             \
}                                                                                         \
                                                                                          \
static inline avl_##name##_iterator *                                                     \
avl_##name##_iterator_init(avl_##name *t) {                                               \
    avl_##name##_iterator *i;                                                             \
    avlnode_t node;                                                                       \
                                                                                          \
    if (t->root == AVLNIL) return NULL;                                                   \
    i = avl_##name##_iterator_alloc(t);                                                   \
    if (!i)                                                                               \
        return i;                                                                         \
                                                                                          \
    while( (node = _GET_AVLNODE_LEFT( i->stack[i->level] )) != AVLNIL ) {                 \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
    }                                                                                     \
                                                                                          \
    return i;                                                                             \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
avl_##name##_iterator_init_set(const avl_##name *t, avl_##name##_iterator **i,            \
                                  void *k) {                                              \
    avlnode_t node;                                                                       \
    int      lastLevelEq = -1, cmp;                                                       \
                                                                                          \
    if ((*i) == NULL || t->max_depth > (*i)->max_depth) {                                 \
        avl_##name##_iterator *new_i;                                                     \
        new_i = (avl_##name##_iterator *) realloc(*i, sizeof(**i) +                       \
			               sizeof(avlnode_t) * (t->max_depth + 31));          \
        if (!new_i)                                                                       \
	    return sizeof(**i) + sizeof(avlnode_t) * (t->max_depth + 31);                 \
        *i = new_i;                                                                       \
    }                                                                                     \
                                                                                          \
    (*i)->t = t;                                                                          \
    (*i)->level = -1;                                                                     \
    if (t->root == AVLNIL) {                                                              \
            (*i)->max_depth = 0; /* valgrind points out it's used in the check above ^.*/ \
            return 0;                                                                     \
    }                                                                                     \
                                                                                          \
    (*i)->max_depth = t->max_depth;                                                       \
    (*i)->stack[0] = t->root;                                                             \
                                                                                          \
    node = t->root;                                                                       \
    while(node != AVLNIL) {                                                               \
        cmp = t->compare(k, AVLITHELEM(t, node), t->arg);                                 \
                                                                                          \
        (*i)->level++;                                                                    \
        (*i)->stack[(*i)->level] = node;                                                  \
                                                                                          \
        if (cmp > 0) {                                                                    \
            (*i)->level--; /* exclude current node from path, ie "mark as visited" */     \
            node = _GET_AVLNODE_RIGHT(node);                                              \
        } else if (cmp < 0) {                                                             \
            node = _GET_AVLNODE_LEFT(node);                                               \
        } else {                                                                          \
            lastLevelEq = (*i)->level;                                                    \
            node = _GET_AVLNODE_LEFT(node); /* one way iterator: from left to right */    \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    if (lastLevelEq >= 0)                                                                 \
        (*i)->level = lastLevelEq;                                                        \
    return 0;                                                                             \
}                                                                                         \
                                                                                          \
static inline avl_##name##_iterator *                                                     \
avl_##name##_iterator_reverse_init(avl_##name *t) {                                       \
    avl_##name##_iterator *i;                                                             \
    avlnode_t node;                                                                       \
                                                                                          \
    if (t->root == AVLNIL) return NULL;                                                   \
    i = avl_##name##_iterator_alloc(t);                                                   \
    if (!i)                                                                               \
        return i;                                                                         \
                                                                                          \
    while( (node = _GET_AVLNODE_RIGHT( i->stack[i->level] )) != AVLNIL ) {                \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
    }                                                                                     \
                                                                                          \
    return i;                                                                             \
}                                                                                         \
                                                                                          \
static inline int                                                                         \
avl_##name##_iterator_reverse_init_set(const avl_##name *t,                               \
                                          avl_##name##_iterator **i, void *k) {           \
    avlnode_t node;                                                                       \
    int      lastLevelEq = -1, cmp;                                                       \
                                                                                          \
    if ((*i) == NULL || t->max_depth > (*i)->max_depth) {                                 \
        avl_##name##_iterator *new_i;                                                     \
        new_i = (avl_##name##_iterator *) realloc(*i, sizeof(**i) +                       \
				       sizeof(avlnode_t) * (t->max_depth + 31));          \
        if (!new_i)                                                                       \
	    return sizeof(**i) + sizeof(avlnode_t) * (t->max_depth + 31);                 \
        *i = new_i;                                                                       \
    }                                                                                     \
                                                                                          \
    (*i)->t = t;                                                                          \
    (*i)->level = -1;                                                                     \
    if (t->root == AVLNIL) {                                                              \
            (*i)->max_depth = 0;                                                          \
            return 0;                                                                     \
    }                                                                                     \
                                                                                          \
    (*i)->max_depth = t->max_depth;                                                       \
    (*i)->stack[0] = t->root;                                                             \
                                                                                          \
    node = t->root;                                                                       \
    while(node != AVLNIL) {                                                               \
        cmp = t->compare(k, AVLITHELEM(t, node), t->arg);                                 \
                                                                                          \
        (*i)->level++;                                                                    \
        (*i)->stack[(*i)->level] = node;                                                  \
                                                                                          \
        if (cmp < 0) {                                                                    \
            (*i)->level--;                                                                \
            node = _GET_AVLNODE_LEFT(node);                                               \
        } else if (cmp > 0) {                                                             \
            node = _GET_AVLNODE_RIGHT(node);                                              \
        } else {                                                                          \
            lastLevelEq = (*i)->level;                                                    \
            node = _GET_AVLNODE_RIGHT(node);                                              \
        }                                                                                 \
    }                                                                                     \
                                                                                          \
    if (lastLevelEq >= 0)                                                                 \
        (*i)->level = lastLevelEq;                                                        \
    return 0;                                                                             \
}                                                                                         \
                                                                                          \
static inline void                                                                        \
avl_##name##_iterator_free(avl_##name##_iterator *i) {                                    \
    if (i == NULL)    return;                                                             \
    i = (avl_##name##_iterator *)realloc(i, 0);                                           \
}                                                                                         \
                                                                                          \
/**                                                                                       \
 * Get the last node on the iterator stack, check                                         \
 * if the node is not deleted.                                                            \
 */                                                                                       \
static inline avlnode_t                                                                   \
avl_##name##_iterator_next_node(avl_##name##_iterator *i) {                               \
                                                                                          \
    while (i->level >= 0) {                                                               \
        avlnode_t return_node = i->stack[i->level--];                                     \
        if (! avl_##name##_node_is_deleted(i->t, return_node))                            \
            return return_node;                                                           \
    }                                                                                     \
    return AVLNIL;                                                                        \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
avl_##name##_iterator_next(avl_##name##_iterator *i) {                                    \
                                                                                          \
    if (i == NULL)  return NULL;                                                          \
                                                                                          \
    const avl_##name *t = i->t;                                                           \
    avlnode_t returnNode = avl_##name##_iterator_next_node(i);                            \
                                                                                          \
    if (returnNode == AVLNIL) return NULL;                                                \
                                                                                          \
    avlnode_t node = _GET_AVLNODE_RIGHT(returnNode);                                      \
    while (node != AVLNIL) {                                                              \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
        node = _GET_AVLNODE_LEFT(i->stack[i->level]);                                     \
    }                                                                                     \
                                                                                          \
    return AVLITHELEM(t, returnNode);                                                     \
}                                                                                         \
                                                                                          \
static inline void*                                                                       \
avl_##name##_iterator_reverse_next(avl_##name##_iterator *i) {                            \
                                                                                          \
    if (i == NULL)  return NULL;                                                          \
                                                                                          \
    const avl_##name *t = i->t;                                                           \
    avlnode_t returnNode = avl_##name##_iterator_next_node(i);                            \
                                                                                          \
    if (returnNode == AVLNIL) return NULL;                                                \
                                                                                          \
    avlnode_t node = _GET_AVLNODE_LEFT(returnNode);                                       \
    while (node != AVLNIL) {                                                              \
        i->level++;                                                                       \
        i->stack[i->level] = node;                                                        \
        node = _GET_AVLNODE_RIGHT(i->stack[i->level]);                                    \
    }                                                                                     \
    return AVLITHELEM(t, returnNode);                                                     \
}
/*
 * vim: ts=4 sts=4 et
 */
#endif

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
