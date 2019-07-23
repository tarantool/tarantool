#ifndef TARANTOOL_LIB_JSON_JSON_H_INCLUDED
#define TARANTOOL_LIB_JSON_JSON_H_INCLUDED
/*
 * Copyright 2010-2018 Tarantool AUTHORS: please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "trivia/util.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef typeof
#define typeof __typeof__
#endif

/**
 * Lexer for JSON paths:
 * <field>, <.field>, <[123]>, <['field']> and their combinations.
 */
struct json_lexer {
	/** Source string. */
	const char *src;
	/** Length of string. */
	int src_len;
	/** Current lexer's offset in bytes. */
	int offset;
	/** Current lexer's offset in symbols. */
	int symbol_count;
	/**
	 * Base field offset for emitted JSON_TOKEN_NUM tokens,
	 * e.g. 0 for C and 1 for Lua.
	 */
	int index_base;
};

enum json_token_type {
	JSON_TOKEN_NUM,
	JSON_TOKEN_STR,
	JSON_TOKEN_ANY,
	/** Lexer reached end of path. */
	JSON_TOKEN_END,
};

/**
 * Element of a JSON path. It can be either string or number.
 * String idenfiers are in ["..."] and between dots. Numbers are
 * indexes in [...].
 *
 * May be organized in a tree-like structure reflecting a JSON
 * document structure, for more details see the comment to struct
 * json_tree.
 */
struct json_token {
	enum json_token_type type;
	union {
		struct {
			/** String identifier. */
			const char *str;
			/** Length of @a str. */
			int len;
		};
		/** Index value. */
		int num;
	};
	/** Pointer to the parent token in a JSON tree. */
	struct json_token *parent;
	/**
	 * Array of child tokens in a JSON tree. Indexes in this
	 * array match [token.num] index for JSON_TOKEN_NUM type
	 * and are allocated sequentially for JSON_TOKEN_STR child
	 * tokens.
	 *
	 * JSON_TOKEN_ANY is exclusive. If it's present, it must
	 * be the only one and have index 0 in the children array.
	 * It will be returned by lookup by any key.
	 */
	struct json_token **children;
	/** Allocation size of children array. */
	int children_capacity;
	/**
	 * Max occupied index in the children array or -1 if
	 * the children array is empty.
	 */
	int max_child_idx;
	/**
	 * Index of this token in parent's children array or -1
	 * if the token was removed from a JSON tree or represents
	 * a JSON tree root.
	 */
	int sibling_idx;
	/**
	 * Hash value of the token. Used for lookups in a JSON tree.
	 * For more details, see the comment to json_tree::hash.
	 */
	uint32_t hash;
};

struct mh_json_t;

/**
 * This structure is used for organizing JSON tokens produced
 * by a lexer in a tree-like structure reflecting a JSON document
 * structure.
 *
 * Each intermediate node of the tree corresponds to either
 * a JSON map or an array, depending on the key type used by
 * its children (JSON_TOKEN_STR or JSON_TOKEN_NUM, respectively).
 * Leaf nodes may represent both complex JSON structures and
 * final values - it is not mandated by the JSON tree design.
 * The root of the tree doesn't have a key and is preallocated
 * when the tree is created.
 *
 * The json_token structure is intrusive by design, i.e. to store
 * arbitrary information in a JSON tree, one has to incorporate it
 * into a user defined structure.
 *
 * Example:
 *
 *   struct data {
 *           ...
 *           struct json_token token;
 *   };
 *
 *   struct json_tree tree;
 *   json_tree_create(&tree);
 *   struct json_token *parent = &tree->root;
 *
 *   // Add a path to the tree.
 *   struct data *data = data_new();
 *   struct json_lexer lexer;
 *   json_lexer_create(&lexer, path, path_len);
 *   json_lexer_next_token(&lexer, &data->token);
 *   while (data->token.type != JSON_TOKEN_END) {
 *           json_tree_add(&tree, parent, &data->token);
 *           parent = &data->token;
 *           data = data_new();
 *           json_lexer_next_token(&lexer, &data->token);
 *   }
 *   data_delete(data);
 *
 *   // Look up a path in the tree.
 *   data = json_tree_lookup_path(&tree, &tree.root,
 *                                path, path_len);
 */
struct json_tree {
	/**
	 * Preallocated token corresponding to the JSON tree root.
	 * It doesn't have a key (set to JSON_TOKEN_END).
	 */
	struct json_token root;
	/**
	 * Hash table that is used to quickly look up a token
	 * corresponding to a JSON map item given a key and
	 * a parent token. We store all tokens that have type
	 * JSON_TOKEN_STR in this hash table. Apparently, we
	 * don't need to store JSON_TOKEN_NUM tokens as we can
	 * quickly look them up in the children array anyway.
	 *
	 * The hash table uses pair <parent, key> as key, so
	 * even tokens that happen to have the same key will
	 * have different keys in the hash. To look up a tree
	 * node corresponding to a particular path, we split
	 * the path into tokens and look up the first token
	 * in the root node and each following token in the
	 * node returned at the previous step.
	 *
	 * We compute a hash value for a token by hashing its
	 * key using the hash value of its parent as seed. This
	 * is equivalent to computing hash for the path leading
	 * to the token. However, we don't need to recompute
	 * hash starting from the root at each step as we
	 * descend the tree looking for a specific path, and we
	 * can start descent from any node, not only from the root.
	 *
	 * As a consequence of this hashing technique, even
	 * though we don't need to store JSON_TOKEN_NUM tokens
	 * in the hash table, we still have to compute hash
	 * values for them.
	 */
	struct mh_json_t *hash;
};

/**
 * Create @a lexer.
 * @param[out] lexer Lexer to create.
 * @param src Source string.
 * @param src_len Length of @a src.
 * @param index_base Base field offset for emitted JSON_TOKEN_NUM
 *                   tokens e.g. 0 for C and 1 for Lua.
 */
static inline void
json_lexer_create(struct json_lexer *lexer, const char *src, int src_len,
		  int index_base)
{
	lexer->src = src;
	lexer->src_len = src_len;
	lexer->offset = 0;
	lexer->symbol_count = 0;
	lexer->index_base = index_base;
}

/**
 * Get a next path token.
 * @param lexer Lexer.
 * @param[out] token Token to store parsed result.
 * @retval   0 Success. For result see @a token.str, token.len,
 *             token.num.
 * @retval > 0 Position of a syntax error. A position is 1-based
 *             and starts from a beginning of a source string.
 */
int
json_lexer_next_token(struct json_lexer *lexer, struct json_token *token);

/** Check if @a lexer has finished parsing. */
static inline bool
json_lexer_is_eof(const struct json_lexer *lexer)
{
	return lexer->offset == lexer->src_len;
}

/**
 * Compare two JSON paths using Lexer class.
 * - in case of paths that have same token-sequence prefix,
 *   the path having more tokens is assumed to be greater
 * - both paths must be valid
 *   (may be tested with json_path_validate).
 */
int
json_path_cmp(const char *a, int a_len, const char *b, int b_len,
	      int index_base);

/**
 * Check if the passed JSON path is valid.
 * Return 0 for valid path and error position for invalid.
 */
int
json_path_validate(const char *path, int path_len, int index_base);

/**
 * Scan the JSON path string and return the offset of the first
 * character [*] (the array index placeholder).
 * - if [*] is not found, path_len is returned.
 * - specified JSON path must be valid
 *   (may be tested with json_path_validate).
 */
int
json_path_multikey_offset(const char *path, int path_len, int index_base);

/**
 * Test if a given JSON token is a JSON tree leaf, i.e.
 * has no child nodes.
 */
static inline bool
json_token_is_leaf(struct json_token *token)
{
	return token->max_child_idx < 0;
}

/**
 * Compare two JSON tokens, not taking into account their tree
 * attributes. Only the token values are compared. That might be
 * used to compare two JSON paths. String comparison of the paths
 * may not work because the same token can be present in different
 * forms: ['a'] == .a, for example.
 */
static inline int
json_token_cmp(const struct json_token *l, const struct json_token *r)
{
	if (l->type != r->type)
		return l->type - r->type;
	switch(l->type) {
	case JSON_TOKEN_NUM:
		return l->num - r->num;
	case JSON_TOKEN_STR:
		if (l->len != r->len)
			return l->len - r->len;
		return memcmp(l->str, r->str, l->len);
	default:
		return 0;
	}
}

/**
 * Test if a given JSON token is multikey.
 */
static inline bool
json_token_is_multikey(struct json_token *token)
{
	return token->max_child_idx == 0 &&
	       token->children[0]->type == JSON_TOKEN_ANY;
}

/**
 * An snprint-style function to print the path to a token in
 * a JSON tree.
 */
int
json_tree_snprint_path(char *buf, int size, const struct json_token *token,
		       int index_base);

/**
 * Initialize a new empty JSON tree.
 *
 * Returns 0 on success, -1 on memory allocation error.
 */
int
json_tree_create(struct json_tree *tree);

/**
 * Destroy a JSON tree.
 *
 * Note, this routine expects the tree to be empty - the caller
 * is supposed to use json_tree_foreach_safe() and json_tree_del()
 * to dismantle the tree before calling this function.
 */
void
json_tree_destroy(struct json_tree *tree);

/**
 * Internal function, use json_tree_lookup() instead.
 */
struct json_token *
json_tree_lookup_slowpath(struct json_tree *tree, struct json_token *parent,
			  const struct json_token *token);

/**
 * Look up a token in a JSON tree given a parent token and a key.
 *
 * Returns NULL if not found.
 */
static inline struct json_token *
json_tree_lookup(struct json_tree *tree, struct json_token *parent,
		 const struct json_token *token)
{
	struct json_token *ret = NULL;
	if (unlikely(json_token_is_multikey(parent))) {
		assert(parent->max_child_idx == 0);
		return parent->children[0];
	}
	switch (token->type) {
	case JSON_TOKEN_NUM:
		if (likely(token->num <= parent->max_child_idx))
			ret = parent->children[token->num];
		break;
	case JSON_TOKEN_ANY:
		if (likely(parent->max_child_idx >= 0))
			ret = parent->children[parent->max_child_idx];
		break;
	case JSON_TOKEN_STR:
		ret = json_tree_lookup_slowpath(tree, parent, token);
		break;
	default:
		unreachable();
	}
	return ret;
}

/**
 * Insert a token into a JSON tree at a given position.
 *
 * The token key (json_token::type and num/str,len) must be set,
 * e.g. by json_lexer_next_token(). The caller must ensure that
 * no token with the same key is linked to the same parent, e.g.
 * with json_tree_lookup().
 *
 * Returns 0 on success, -1 on memory allocation error.
 */
int
json_tree_add(struct json_tree *tree, struct json_token *parent,
	      struct json_token *token);

/**
 * Delete a token from a JSON tree.
 *
 * The token must be linked to the tree (see json_tree_add())
 * and must not have any children.
 */
void
json_tree_del(struct json_tree *tree, struct json_token *token);

/**
 * Look up a token in a JSON tree by path.
 *
 * The path is relative to the given root token. In order to
 * look up a token by absolute path, pass json_tree::root.
 * The index_base is passed to json_lexer used for tokenizing
 * the path, see json_lexer_create() for more details.
 *
 * Returns NULL if no token is found or the path is invalid.
 */
struct json_token *
json_tree_lookup_path(struct json_tree *tree, struct json_token *root,
		      const char *path, int path_len, int index_base);

/**
 * Perform pre-order traversal in a JSON subtree rooted
 * at a given node.
 *
 * To start a new traversal, pass NULL for @pos.
 * Returns @root at the first iteration.
 * Returns NULL when traversal is over.
 */
struct json_token *
json_tree_preorder_next(struct json_token *root, struct json_token *pos);

/**
 * Perform post-order traversal in a JSON subtree rooted
 * at a given node.
 *
 * To start a new traversal, pass NULL for @pos.
 * Returns @root at the last iteration.
 * Returns NULL when traversal is over.
 */
struct json_token *
json_tree_postorder_next(struct json_token *root, struct json_token *pos);

/**
 * Perform pre-order JSON tree traversal.
 * Note, this function does not visit the root node.
 * See also json_tree_preorder_next().
 */
#define json_tree_foreach_preorder(pos, root)				     \
	for ((pos) = json_tree_preorder_next((root), (root));		     \
	     (pos) != NULL;						     \
	     (pos) = json_tree_preorder_next((root), (pos)))

/**
 * Perform post-order JSON tree traversal.
 * Note, this function does not visit the root node.
 * See also json_tree_postorder_next().
 */
#define json_tree_foreach_postorder(pos, root)				     \
	for ((pos) = json_tree_postorder_next((root), NULL);		     \
	     (pos) != (root);						     \
	     (pos) = json_tree_postorder_next((root), (pos)))

/**
 * Perform post-order JSON tree traversal safe against node removal.
 * Note, this function does not visit the root node.
 * See also json_tree_postorder_next().
 */
#define json_tree_foreach_safe(pos, root, tmp)				     \
	for ((pos) = json_tree_postorder_next((root), NULL);		     \
	     (pos) != (root) &&						     \
	     ((tmp) = json_tree_postorder_next((root), (pos))) != NULL;	     \
	     (pos) = (tmp))

/**
 * Return a container of a json_tree_token.
 */
#define json_tree_entry(token, type, member)				     \
	container_of((token), type, member)

/**
 * Return a container of a json_tree_token or NULL if @token is NULL.
 */
#define json_tree_entry_safe(token, type, member)			     \
	((token) != NULL ? json_tree_entry((token), type, member) : NULL)    \

/**
 * Container-aware wrapper around json_tree_lookup().
 */
#define json_tree_lookup_entry(tree, parent, token, type, member) ({	     \
	struct json_token *ret = json_tree_lookup((tree), (parent), (token));\
	json_tree_entry_safe(ret, type, member);			     \
})

/**
 * Container-aware wrapper around json_tree_lookup_path().
 */
#define json_tree_lookup_path_entry(tree, root, path, path_len, index_base,  \
				    type, member) ({			     \
	struct json_token *ret = json_tree_lookup_path((tree), (root),	     \
					(path), (path_len), (index_base));   \
	json_tree_entry_safe(ret, type, member);			     \
})

/**
 * Container-aware wrapper around json_tree_preorder_next().
 */
#define json_tree_preorder_next_entry(root, pos, type, member) ({	     \
	struct json_token *next = json_tree_preorder_next((root), (pos));    \
	json_tree_entry_safe(next, type, member);			     \
})

/**
 * Container-aware wrapper around json_tree_postorder_next().
 */
#define json_tree_postorder_next_entry(root, pos, type, member) ({	     \
	struct json_token *next = json_tree_postorder_next((root), (pos));   \
	json_tree_entry_safe(next, type, member);			     \
})

/**
 * Container-aware version of json_tree_foreach_preorder().
 */
#define json_tree_foreach_entry_preorder(pos, root, type, member)	     \
	for ((pos) = json_tree_preorder_next_entry((root), (root),	     \
						   type, member);	     \
	     (pos) != NULL;						     \
	     (pos) = json_tree_preorder_next_entry((root), &(pos)->member,   \
						   type, member))

/**
 * Container-aware version of json_tree_foreach_postorder().
 */
#define json_tree_foreach_entry_postorder(pos, root, type, member)	     \
	for ((pos) = json_tree_postorder_next_entry((root), NULL,	     \
						    type, member);	     \
	     &(pos)->member != (root);					     \
	     (pos) = json_tree_postorder_next_entry((root), &(pos)->member,  \
						     type, member))

/**
 * Container-aware version of json_tree_foreach_safe().
 */
#define json_tree_foreach_entry_safe(pos, root, type, member, tmp)	     \
	for ((pos) = json_tree_postorder_next_entry((root), NULL,	     \
						    type, member);	     \
	     &(pos)->member != (root) &&				     \
	     ((tmp) = json_tree_postorder_next_entry((root), &(pos)->member, \
						     type, member)) != NULL; \
	     (pos) = (tmp))

#ifdef __cplusplus
}
#endif

#endif /* TARANTOOL_JSON_JSON_H_INCLUDED */
