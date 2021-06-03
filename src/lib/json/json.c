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

#include "json.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unicode/uchar.h>
#include <unicode/utf8.h>

#include "trivia/util.h"
#include <PMurHash.h>

/**
 * Read a single symbol from a string starting from an offset.
 * @param lexer JSON path lexer.
 * @param[out] UChar32 Read symbol.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_read_symbol(struct json_lexer *lexer, UChar32 *out)
{
	if (json_lexer_is_eof(lexer)) {
		*out = U_SENTINEL;
		return lexer->symbol_count + 1;
	}
	U8_NEXT(lexer->src, lexer->offset, lexer->src_len, *out);
	if (*out == U_SENTINEL)
		return lexer->symbol_count + 1;
	++lexer->symbol_count;
	return 0;
}

/**
 * Rollback one symbol offset.
 * @param lexer JSON path lexer.
 * @param offset Offset to the previous symbol.
 */
static inline void
json_revert_symbol(struct json_lexer *lexer, int offset)
{
	lexer->offset = offset;
	--lexer->symbol_count;
}

/** Fast forward when it is known that a symbol is 1-byte char. */
static inline void
json_skip_char(struct json_lexer *lexer)
{
	++lexer->offset;
	++lexer->symbol_count;
}

/** Get a current symbol as a 1-byte char. */
static inline char
json_current_char(const struct json_lexer *lexer)
{
	return *(lexer->src + lexer->offset);
}

/**
 * Parse string identifier in quotes. Lexer either stops right
 * after the closing quote, or returns an error position.
 * @param lexer JSON path lexer.
 * @param[out] token JSON token to store result.
 * @param quote_type Quote by that a string must be terminated.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_string(struct json_lexer *lexer, struct json_token *token,
		  UChar32 quote_type)
{
	assert(lexer->offset < lexer->src_len);
	assert(quote_type == json_current_char(lexer));
	/* The first symbol is always char  - ' or ". */
	json_skip_char(lexer);
	int str_offset = lexer->offset;
	UChar32 c;
	int rc;
	while ((rc = json_read_symbol(lexer, &c)) == 0) {
		if (c == quote_type) {
			int len = lexer->offset - str_offset - 1;
			if (len == 0)
				return lexer->symbol_count;
			token->type = JSON_TOKEN_STR;
			token->str = lexer->src + str_offset;
			token->len = len;
			return 0;
		}
	}
	return rc;
}

/**
 * Parse digit sequence into integer until non-digit is met.
 * Lexer stops right after the last digit.
 * @param lexer JSON lexer.
 * @param[out] token JSON token to store result.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_integer(struct json_lexer *lexer, struct json_token *token)
{
	const char *end = lexer->src + lexer->src_len;
	const char *pos = lexer->src + lexer->offset;
	assert(pos < end);
	int len = 0;
	int value = 0;
	char c = *pos;
	if (! isdigit(c))
		return lexer->symbol_count + 1;
	do {
		value = value * 10 + c - (int)'0';
		++len;
	} while (++pos < end && isdigit((c = *pos)));
	if (value < lexer->index_base)
		return lexer->symbol_count + 1;
	lexer->offset += len;
	lexer->symbol_count += len;
	token->type = JSON_TOKEN_NUM;
	token->num = value - lexer->index_base;
	return 0;
}

/**
 * Check that a symbol can be part of a JSON path not inside
 * ["..."].
 */
static inline bool
json_is_valid_identifier_symbol(UChar32 c)
{
	return u_isUAlphabetic(c) || c == (UChar32)'_' || u_isdigit(c);
}

/**
 * Parse identifier out of quotes. It can contain only alphas,
 * digits and underscores. And can not contain digit at the first
 * position. Lexer is stoped right after the last non-digit,
 * non-alpha and non-underscore symbol.
 * @param lexer JSON lexer.
 * @param[out] token JSON token to store result.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_identifier(struct json_lexer *lexer, struct json_token *token)
{
	assert(lexer->offset < lexer->src_len);
	int str_offset = lexer->offset;
	UChar32 c;
	int rc = json_read_symbol(lexer, &c);
	if (rc != 0)
		return rc;
	/* First symbol can not be digit. */
	if (!u_isalpha(c) && c != (UChar32)'_')
		return lexer->symbol_count;
	int last_offset = lexer->offset;
	while ((rc = json_read_symbol(lexer, &c)) == 0) {
		if (! json_is_valid_identifier_symbol(c)) {
			json_revert_symbol(lexer, last_offset);
			break;
		}
		last_offset = lexer->offset;
	}
	token->type = JSON_TOKEN_STR;
	token->str = lexer->src + str_offset;
	token->len = lexer->offset - str_offset;
	return 0;
}

int
json_lexer_next_token(struct json_lexer *lexer, struct json_token *token)
{
	if (json_lexer_is_eof(lexer)) {
		token->type = JSON_TOKEN_END;
		return 0;
	}
	UChar32 c;
	int last_offset = lexer->offset;
	int rc = json_read_symbol(lexer, &c);
	if (rc != 0)
		return rc;
	switch(c) {
	case (UChar32)'[':
		/* Error for '[\0'. */
		if (json_lexer_is_eof(lexer))
			return lexer->symbol_count;
		c = json_current_char(lexer);
		if (c == '"' || c == '\'') {
			rc = json_parse_string(lexer, token, c);
		} else if (c == '*') {
			json_skip_char(lexer);
			token->type = JSON_TOKEN_ANY;
		} else {
			rc = json_parse_integer(lexer, token);
		}
		if (rc != 0)
			return rc;
		/*
		 * Expression, started from [ must be finished
		 * with ] regardless of its type.
		 */
		if (json_lexer_is_eof(lexer) ||
		    json_current_char(lexer) != ']')
			return lexer->symbol_count + 1;
		/* Skip ] - one byte char. */
		json_skip_char(lexer);
		return 0;
	case (UChar32)'.':
		if (json_lexer_is_eof(lexer))
			return lexer->symbol_count + 1;
		return json_parse_identifier(lexer, token);
	default:
		if (last_offset != 0)
			return lexer->symbol_count;
		json_revert_symbol(lexer, last_offset);
		return json_parse_identifier(lexer, token);
	}
}

/**
 * Compare JSON tokens as nodes of a JSON tree. That is, including
 * parent references.
 */
static int
json_token_cmp_in_tree(const struct json_token *a, const struct json_token *b)
{
	if (a->parent != b->parent)
		return a->parent - b->parent;
	return json_token_cmp(a, b);
}

int
json_path_cmp(const char *a, int a_len, const char *b, int b_len,
	      int index_base)
{
	struct json_lexer lexer_a, lexer_b;
	json_lexer_create(&lexer_a, a, a_len, index_base);
	json_lexer_create(&lexer_b, b, b_len, index_base);
	struct json_token token_a, token_b;
	/* For the sake of json_token_cmp_in_tree(). */
	token_a.parent = NULL;
	token_b.parent = NULL;
	int rc_a, rc_b;
	while ((rc_a = json_lexer_next_token(&lexer_a, &token_a)) == 0 &&
	       (rc_b = json_lexer_next_token(&lexer_b, &token_b)) == 0 &&
		token_a.type != JSON_TOKEN_END &&
		token_b.type != JSON_TOKEN_END) {
		int rc = json_token_cmp_in_tree(&token_a, &token_b);
		if (rc != 0)
			return rc;
	}
	/* Paths a and b must be valid. */
	assert(rc_a == 0 && rc_b == 0);
	/*
	 * The parser stopped because the end of one of the paths
	 * was reached. As JSON_TOKEN_END > JSON_TOKEN_{NUM, STR},
	 * the path having more tokens has lower key.type value.
	 */
	return token_b.type - token_a.type;
}

int
json_path_validate(const char *path, int path_len, int index_base)
{
	struct json_lexer lexer;
	json_lexer_create(&lexer, path, path_len, index_base);
	struct json_token token;
	int rc;
	while ((rc = json_lexer_next_token(&lexer, &token)) == 0 &&
	       token.type != JSON_TOKEN_END) {}
	return rc;
}

int
json_path_multikey_offset(const char *path, int path_len, int index_base)
{
	struct json_lexer lexer;
	json_lexer_create(&lexer, path, path_len, index_base);
	struct json_token token;
	int rc, last_lexer_offset = 0;
	while ((rc = json_lexer_next_token(&lexer, &token)) == 0) {
		if (token.type == JSON_TOKEN_ANY)
			return last_lexer_offset;
		else if (token.type == JSON_TOKEN_END)
			break;
		last_lexer_offset = lexer.offset;
	}
	assert(rc == 0);
	return path_len;
}

/**
 * An snprint-style helper to print an individual token key.
 */
static int
json_token_snprint(char *buf, int size, const struct json_token *token,
		   int index_base)
{
	int len = 0;
	switch (token->type) {
	case JSON_TOKEN_NUM:
		len = snprintf(buf, size, "[%d]", token->num + index_base);
		break;
	case JSON_TOKEN_STR:
		len = snprintf(buf, size, "[\"%.*s\"]", token->len, token->str);
		break;
	case JSON_TOKEN_ANY:
		len = snprintf(buf, size, "[*]");
		break;
	default:
		unreachable();
	}
	return len;
}

int
json_tree_snprint_path(char *buf, int size, const struct json_token *token,
		       int index_base)
{
	/* The token must be linked in a tree. */
	assert(token->parent != NULL);

	/*
	 * Calculate the length of the final path string by passing
	 * 0-length buffer to json_token_snprint() routine.
	 */
	int len = 0;
	for (const struct json_token *iter = token;
	     iter->type != JSON_TOKEN_END; iter = iter->parent)
		len += json_token_snprint(NULL, 0, iter, index_base);

	/*
	 * Nothing else to do if the caller is only interested in
	 * the string length.
	 */
	if (size == 0)
		return len;

	/*
	 * Write the path to the supplied buffer.
	 */
	int pos = len;
	char last = '\0';
	for (const struct json_token *iter = token;
	     iter->type != JSON_TOKEN_END; iter = iter->parent) {
		pos -= json_token_snprint(NULL, 0, iter, index_base);
		assert(pos >= 0);
		if (pos >= size) {
			/* The token doesn't fit in the buffer. */
			continue;
		}
		int rc = json_token_snprint(buf + pos, size - pos,
					    iter, index_base);
		/*
		 * Restore the character overwritten with
		 * a terminating nul by json_token_snprint().
		 */
		if (last != '\0') {
			assert(pos + rc < size);
			buf[pos + rc] = last;
		}
		last = buf[pos];
	}
	return len;
}

#define MH_SOURCE 1
#define mh_name _json
#define mh_key_t struct json_token *
#define mh_node_t struct json_token *
#define mh_arg_t void *
#define mh_hash(a, arg) ((*(a))->hash)
#define mh_hash_key(a, arg) ((a)->hash)
#define mh_cmp(a, b, arg) (json_token_cmp_in_tree(*(a), *(b)))
#define mh_cmp_key(a, b, arg) (json_token_cmp_in_tree((a), *(b)))
#include "salad/mhash.h"

static const uint32_t hash_seed = 13U;

/**
 * Compute the hash value of a JSON token.
 *
 * See the comment to json_tree::hash for implementation details.
 */
static uint32_t
json_token_hash(struct json_token *token)
{
	uint32_t h = token->parent->hash;
	uint32_t carry = 0;
	const void *data;
	int data_size;
	if (token->type == JSON_TOKEN_STR) {
		data = token->str;
		data_size = token->len;
	} else if (token->type == JSON_TOKEN_NUM) {
		data = &token->num;
		data_size = sizeof(token->num);
	} else if (token->type == JSON_TOKEN_ANY) {
		data = "*";
		data_size = 1;
	} else {
		unreachable();
	}
	PMurHash32_Process(&h, &carry, data, data_size);
	return PMurHash32_Result(h, carry, data_size);
}

int
json_tree_create(struct json_tree *tree)
{
	memset(tree, 0, sizeof(struct json_tree));
	tree->root.hash = hash_seed;
	tree->root.type = JSON_TOKEN_END;
	tree->root.max_child_idx = -1;
	tree->root.sibling_idx = -1;
	tree->hash = mh_json_new();
	return tree->hash == NULL ? -1 : 0;
}

static void
json_token_destroy(struct json_token *token)
{
 	assert(token->max_child_idx == -1);
	assert(token->sibling_idx == -1);
	free(token->children);
	token->children = NULL;
}

void
json_tree_destroy(struct json_tree *tree)
{
	json_token_destroy(&tree->root);
	mh_json_delete(tree->hash);
}

struct json_token *
json_tree_lookup_slowpath(struct json_tree *tree, struct json_token *parent,
			  const struct json_token *token)
{
	assert(token->type == JSON_TOKEN_STR);
	struct json_token key;
	key.type = token->type;
	key.str = token->str;
	key.len = token->len;
	key.parent = parent;
	key.hash = json_token_hash(&key);
	mh_int_t id = mh_json_find(tree->hash, &key, NULL);
	if (id == mh_end(tree->hash))
		return NULL;
	struct json_token **entry = mh_json_node(tree->hash, id);
	assert(entry != NULL && *entry != NULL);
	return *entry;
}

int
json_tree_add(struct json_tree *tree, struct json_token *parent,
	      struct json_token *token)
{
	assert(json_tree_lookup(tree, parent, token) == NULL);
	token->parent = parent;
	token->children = NULL;
	token->children_capacity = 0;
	token->max_child_idx = -1;
	token->hash = json_token_hash(token);
	int insert_idx = token->type == JSON_TOKEN_NUM ?
			 (int)token->num : parent->max_child_idx + 1;
	/*
	 * Dynamically grow the children array if necessary.
	 */
	if (insert_idx >= parent->children_capacity) {
		int new_size = parent->children_capacity == 0 ?
			       8 : 2 * parent->children_capacity;
		while (insert_idx >= new_size)
			new_size *= 2;
		struct json_token **children = realloc(parent->children,
						new_size * sizeof(void *));
		if (children == NULL)
			return -1; /* out of memory */
		memset(children + parent->children_capacity, 0,
		       (new_size - parent->children_capacity) * sizeof(void *));
		parent->children = children;
		parent->children_capacity = new_size;
	}
	/*
	 * Insert the token into the hash (only for tokens representing
	 * JSON map entries, see the comment to json_tree::hash).
	 */
	if (token->type == JSON_TOKEN_STR) {
		mh_int_t id = mh_json_put(tree->hash,
			(const struct json_token **)&token, NULL, NULL);
		if (id == mh_end(tree->hash))
			return -1; /* out of memory */
	}
	/*
	 * Success, now we can insert the new token into its parent's
	 * children array.
	 */
	assert(parent->children[insert_idx] == NULL);
	parent->children[insert_idx] = token;
	parent->max_child_idx = MAX(parent->max_child_idx, insert_idx);
	token->sibling_idx = insert_idx;
	assert(json_tree_lookup(tree, parent, token) == token);
	return 0;
}

void
json_tree_del(struct json_tree *tree, struct json_token *token)
{
	struct json_token *parent = token->parent;
	assert(token->sibling_idx >= 0);
	assert(parent->children[token->sibling_idx] == token);
	assert(json_tree_lookup(tree, parent, token) == token);
	/*
	 * Clear the entry corresponding to this token in parent's
	 * children array and update max_child_idx if necessary.
	 */
	parent->children[token->sibling_idx] = NULL;
	token->sibling_idx = -1;
	while (parent->max_child_idx >= 0 &&
	       parent->children[parent->max_child_idx] == NULL)
		parent->max_child_idx--;
	/*
	 * Remove the token from the hash (only for tokens representing
	 * JSON map entries, see the comment to json_tree::hash).
	 */
	if (token->type == JSON_TOKEN_STR) {
		mh_int_t id = mh_json_find(tree->hash, token, NULL);
		assert(id != mh_end(tree->hash));
		mh_json_del(tree->hash, id, NULL);
	}
	json_token_destroy(token);
	assert(json_tree_lookup(tree, parent, token) == NULL);
}

struct json_token *
json_tree_lookup_path(struct json_tree *tree, struct json_token *root,
		      const char *path, int path_len, int index_base)
{
	int rc;
	struct json_lexer lexer;
	struct json_token token;
	struct json_token *ret = root;
	json_lexer_create(&lexer, path, path_len, index_base);
	while ((rc = json_lexer_next_token(&lexer, &token)) == 0 &&
	       token.type != JSON_TOKEN_END && ret != NULL) {
		/*
		 * We could skip intermediate lookups by computing
		 * a rolling hash of all tokens produced by the
		 * lexer. But then we would still have to traverse
		 * the path back to ensure it is equal to the
		 * found to the found one. For that we would have
		 * to keep the stack of lexer tokens somewhere.
		 * Given the complications of the alternative,
		 * intermediate lookups don't seem to be so big of
		 * a problem.
		 */
		ret = json_tree_lookup(tree, ret, &token);
	}
	if (rc != 0 || token.type != JSON_TOKEN_END)
		return NULL;
	return ret;
}

/**
 * Return the child of @parent following @pos or NULL if @pos
 * points to the last child in the children array. If @pos is
 * NULL, this function returns the first child.
 */
static struct json_token *
json_tree_child_next(struct json_token *parent, struct json_token *pos)
{
	assert(pos == NULL || pos->parent == parent);
	struct json_token **arr = parent->children;
	if (arr == NULL)
		return NULL;
	int idx = pos != NULL ? pos->sibling_idx + 1 : 0;
	while (idx <= parent->max_child_idx && arr[idx] == NULL)
		idx++;
	return idx <= parent->max_child_idx ? arr[idx] : NULL;
}

/**
 * Return the leftmost descendant of the tree rooted at @pos
 * or NULL if the tree is empty.
 */
static struct json_token *
json_tree_leftmost(struct json_token *pos)
{
	struct json_token *last;
	do {
		last = pos;
		pos = json_tree_child_next(pos, NULL);
	} while (pos != NULL);
	return last;
}

struct json_token *
json_tree_preorder_next(struct json_token *root, struct json_token *pos)
{
	struct json_token *next = json_tree_child_next(pos, NULL);
	if (next != NULL)
		return next;
	while (pos != root) {
		next = json_tree_child_next(pos->parent, pos);
		if (next != NULL)
			return next;
		pos = pos->parent;
	}
	return NULL;
}

struct json_token *
json_tree_postorder_next(struct json_token *root, struct json_token *pos)
{
	struct json_token *next;
	if (pos == NULL)
		return json_tree_leftmost(root);
	if (pos == root)
		return NULL;
	next = json_tree_child_next(pos->parent, pos);
	if (next != NULL)
		return json_tree_leftmost(next);
	return pos->parent;
}
