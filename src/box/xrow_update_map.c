/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "xrow_update_field.h"
#include "fiber.h"
#include "small/region.h"
#include "tuple_format.h"

/**
 * Descriptor of one updated key-value pair. Besides updated data
 * it contains a tail with unchanged pairs, just so as not to
 * create a separate object for them, and to be similar with array
 * update items.
 */
struct xrow_update_map_item {
	/**
	 * Updated key. Can be NULL. In such a case this item
	 * contains only an unchanged tail. A key can be nullified
	 * if it was removed from the map, or when a map is just
	 * created and has no any update yet.
	 */
	const char *key;
	/** Length of @a key. */
	uint32_t key_len;
	/** Updated value. */
	struct xrow_update_field field;
	/** Link in the list of updated map keys. */
	struct stailq_entry in_items;
	/**
	 * Size in bytes of unchanged tail data. It goes right
	 * after @a field.data.
	 */
	uint32_t tail_size;
};

static inline struct xrow_update_map_item *
xrow_update_map_item_alloc(void)
{
	size_t size;
	struct xrow_update_map_item *item =
		region_alloc_object(&fiber()->gc, typeof(*item), &size);
	if (item == NULL)
		diag_set(OutOfMemory, size, "region_alloc_object", "item");
	return item;
}

static void
xrow_update_map_create_item(struct xrow_update_field *field,
			    struct xrow_update_map_item *item,
			    enum xrow_update_type type, const char *key,
			    uint32_t key_len, const char *data,
			    uint32_t data_size, uint32_t tail_size)
{
	assert(field->type == XUPDATE_MAP);
	item->key = key;
	item->key_len = key_len;
	item->field.type = type;
	item->field.data = data;
	item->field.size = data_size,
	item->tail_size = tail_size;
	/*
	 * Each time a new item it created it is stored in the
	 * head of update map item list. It helps in case the
	 * tuple is regularly updated, because on all next updates
	 * this key will be found from the very beginning of the
	 * map.
	 */
	stailq_add_entry(&field->map.items, item, in_items);
}

static inline struct xrow_update_map_item *
xrow_update_map_new_item(struct xrow_update_field *field,
			 enum xrow_update_type type, const char *key,
			 uint32_t key_len, const char *data, uint32_t data_size,
			 uint32_t tail_size)
{
	struct xrow_update_map_item *item = xrow_update_map_item_alloc();
	if (item != NULL) {
		xrow_update_map_create_item(field, item, type, key, key_len,
					    data, data_size, tail_size);
	}
	return item;
}

/**
 * Find an update item to which @a op should be applied. The
 * target field may do not exist, but at least its parent should.
 */
static int
xrow_update_map_extract_opt_item(struct xrow_update_field *field,
				 struct xrow_update_op *op,
				 struct xrow_update_map_item **res)
{
	assert(field->type == XUPDATE_MAP);
	if (op->is_token_consumed) {
		if (xrow_update_op_next_token(op) != 0)
			return -1;
		if (op->token_type != JSON_TOKEN_STR) {
			return xrow_update_err(op, "can't update a map by not "\
					       "a string key");
		}
	}
	struct stailq *items = &field->map.items;
	struct xrow_update_map_item *i, *new_item;
	/*
	 * Firstly, try to find the key among already updated
	 * ones. Perhaps, the needed node is just an intermediate
	 * key of a bigger JSON path, and there are many updates
	 * passing this key, so it should be here for all except
	 * first updates.
	 */
	stailq_foreach_entry(i, items, in_items) {
		if (i->key != NULL && i->key_len == op->key_len &&
		    memcmp(i->key, op->key, i->key_len) == 0) {
			*res = i;
			return 0;
		}
	}
	/*
	 * Slow path - the key is updated first time, need to
	 * decode tails.
	 */
	uint32_t key_len, i_tail_size;
	const char *pos, *key, *end, *tmp, *begin;
	stailq_foreach_entry(i, items, in_items) {
		begin = i->field.data + i->field.size;
		pos = begin;
		end = begin + i->tail_size;
		i_tail_size = 0;
		while(pos < end) {
			if (mp_typeof(*pos) != MP_STR) {
				mp_next(&pos);
				mp_next(&pos);
				continue;
			}
			i_tail_size = pos - begin;
			key = mp_decode_str(&pos, &key_len);
			if (key_len == op->key_len &&
			    memcmp(key, op->key, key_len) == 0)
				goto key_is_found;
			mp_next(&pos);
		}
	}
	*res = NULL;
	return 0;

key_is_found:
	tmp = pos;
	mp_next(&tmp);
	if (i_tail_size == 0 && i->key == NULL) {
		/*
		 * Looks like the needed key was found from the
		 * beginning of tail of an item without a key.
		 * This is actually good, because this item can
		 * be transformed instead of a new item creation.
		 */
		i->key = op->key;
		i->key_len = op->key_len;
		i->field.data = pos;
		i->field.size = tmp - pos;
		i->tail_size = end - tmp;
		new_item = i;
	} else {
		new_item = xrow_update_map_new_item(field, XUPDATE_NOP, op->key,
						    op->key_len, pos, tmp - pos,
						    end - tmp);
		if (new_item == NULL)
			return -1;
		i->tail_size = i_tail_size;
	}
	*res = new_item;
	return 0;
}

/**
 * The same as optional extractor, but the field to update should
 * exist. It is the case of all scalar operations (except '=' - it
 * can work as insert).
 */
static inline struct xrow_update_map_item *
xrow_update_map_extract_item(struct xrow_update_field *field,
			     struct xrow_update_op *op)
{
	assert(field->type == XUPDATE_MAP);
	struct xrow_update_map_item *res;
	if (xrow_update_map_extract_opt_item(field, op, &res) != 0)
		return NULL;
	if (res == NULL)
		xrow_update_err_no_such_field(op);
	return res;
}

int
xrow_update_op_do_map_insert(struct xrow_update_op *op,
			     struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_MAP);
	struct xrow_update_map_item *item;
	if (xrow_update_map_extract_opt_item(field, op, &item) != 0)
		return -1;
	if (!xrow_update_op_is_term(op)) {
		if (item == NULL)
			return xrow_update_err_no_such_field(op);
		op->is_token_consumed = true;
		return xrow_update_op_do_field_insert(op, &item->field);
	}
	if (item != NULL)
		return xrow_update_err_duplicate(op);
	++field->map.size;
	item = xrow_update_map_new_item(field, XUPDATE_NOP, op->key,
					op->key_len, op->arg.set.value,
					op->arg.set.length, 0);
	return item != NULL ? 0 : -1;
}

int
xrow_update_op_do_map_set(struct xrow_update_op *op,
			  struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_MAP);
	struct xrow_update_map_item *item;
	if (xrow_update_map_extract_opt_item(field, op, &item) != 0)
		return -1;
	if (!xrow_update_op_is_term(op)) {
		if (item == NULL)
			return xrow_update_err_no_such_field(op);
		op->is_token_consumed = true;
		return xrow_update_op_do_field_set(op, &item->field);
	}
	if (item != NULL) {
		op->new_field_len = op->arg.set.length;
		/* Ignore the previous op, if any. */
		item->field.type = XUPDATE_SCALAR;
		item->field.scalar.op = op;
		return 0;
	}
	++field->map.size;
	item = xrow_update_map_new_item(field, XUPDATE_NOP, op->key,
					op->key_len, op->arg.set.value,
					op->arg.set.length, 0);
	return item != NULL ? 0 : -1;
}

int
xrow_update_op_do_map_delete(struct xrow_update_op *op,
			     struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_MAP);
	struct xrow_update_map_item *item;
	if (xrow_update_map_extract_opt_item(field, op, &item) != 0)
		return -1;
	if (!xrow_update_op_is_term(op)) {
		if (item == NULL)
			return xrow_update_err_no_such_field(op);
		op->is_token_consumed = true;
		return xrow_update_op_do_field_delete(op, &item->field);
	}
	if (op->arg.del.count != 1)
		return xrow_update_err_delete1(op);
	if (item == NULL)
		return 0;
	/*
	 * Note, even if tail size becomes 0, this item is not
	 * deleted. This is because items are linked via stailq,
	 * elements of which can't be deleted as simple as that.
	 * But it is not a big deal, because '#' is a really rare
	 * operation.
	 * Why just a next key from the tail can't be decoded?
	 * Why key should be NULL here? This is because the JSON
	 * updates allow to update a map containing non-string
	 * keys. If the next key is not a string, it can't be
	 * saved as key of the item. Therefore, it is better not
	 * to touch unchanged tails unless a new update operation
	 * needs it.
	 */
	item->key = NULL;
	item->key_len = 0;
	item->field.data += item->field.size;
	item->field.size = 0;
	item->field.type = XUPDATE_NOP;
	--field->map.size;
	return 0;
}

#define DO_SCALAR_OP_GENERIC(op_type)						\
int										\
xrow_update_op_do_map_##op_type(struct xrow_update_op *op,			\
				struct xrow_update_field *field)		\
{										\
	assert(field->type == XUPDATE_MAP);					\
	struct xrow_update_map_item *item =					\
		xrow_update_map_extract_item(field, op);			\
	if (item == NULL)							\
		return -1;							\
	if (!xrow_update_op_is_term(op)) {					\
		op->is_token_consumed = true;					\
		return xrow_update_op_do_field_##op_type(op, &item->field);	\
	}									\
	if (item->field.type != XUPDATE_NOP)					\
		return xrow_update_err_double(op);				\
	if (xrow_update_op_do_##op_type(op, item->field.data) != 0)		\
		return -1;							\
	item->field.type = XUPDATE_SCALAR;					\
	item->field.scalar.op = op;						\
	return 0;								\
}

DO_SCALAR_OP_GENERIC(arith)

DO_SCALAR_OP_GENERIC(bit)

DO_SCALAR_OP_GENERIC(splice)

int
xrow_update_map_create(struct xrow_update_field *field, const char *header,
		       const char *data, const char *data_end, int field_count)
{
	field->type = XUPDATE_MAP;
	field->data = header;
	field->size = data_end - header;
	field->map.size = field_count;
	stailq_create(&field->map.items);
	if (field_count == 0)
		return 0;
	struct xrow_update_map_item *first =
		xrow_update_map_new_item(field, XUPDATE_NOP, NULL, 0, data, 0,
					 data_end - data);
	return first != NULL ? 0 : -1;
}

int
xrow_update_map_create_with_child(struct xrow_update_field *field,
				  const char *header,
				  const struct xrow_update_field *child,
				  const char *key, uint32_t key_len)
{
	field->type = XUPDATE_MAP;
	field->data = header;
	stailq_create(&field->map.items);

	const char *pos = header;
	uint32_t i, ikey_len, field_count = mp_decode_map(&pos);
	const char *begin = pos;
	struct xrow_update_map_item *item = NULL;
	for (i = 0; i < field_count; ++i) {
		if (mp_typeof(*pos) != MP_STR) {
			mp_next(&pos);
			mp_next(&pos);
			continue;
		}
		const char *before_key = pos;
		const char *ikey = mp_decode_str(&pos, &ikey_len);
		if (ikey_len == key_len && memcmp(ikey, key, key_len) == 0) {
			item = xrow_update_map_new_item(field, XUPDATE_NOP,
							NULL, 0, begin, 0,
							before_key - begin);
			if (item == NULL)
				return -1;
			++i;
			break;
		}
		mp_next(&pos);
	}
	/*
	 * When a map is initialized with an existing child, it
	 * means, that it was already found earlier. I.e. it is
	 * impossible to miss it.
	 */
	assert(item != NULL);
	const char *data = pos;
	mp_next(&pos);
	uint32_t data_size = pos - data;
	for (; i < field_count; ++i) {
		mp_next(&pos);
		mp_next(&pos);
	}
	item = xrow_update_map_item_alloc();
	if (item == NULL)
		return -1;
	item->field = *child;
	xrow_update_map_create_item(field, item, child->type, key, key_len,
				    data, data_size, pos - data - data_size);
	field->map.size = field_count;
	field->size = pos - header;
	return 0;
}

uint32_t
xrow_update_map_sizeof(struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_MAP);
	uint32_t res = mp_sizeof_map(field->map.size);
	struct xrow_update_map_item *i;
	stailq_foreach_entry(i, &field->map.items, in_items) {
		res += i->tail_size;
		if (i->key != NULL) {
			res += mp_sizeof_str(i->key_len) +
			       xrow_update_field_sizeof(&i->field);
		}
	}
	return res;
}

uint32_t
xrow_update_map_store(struct xrow_update_field *field,
		      struct json_tree *format_tree,
		      struct json_token *this_node, char *out, char *out_end)
{
	assert(field->type == XUPDATE_MAP);
	char *out_begin = out;
	out = mp_encode_map(out, field->map.size);
	struct xrow_update_map_item *i;
	/*
	 * This is the trick about saving updated keys before
	 * others. The first cycle doesn't save unchanged tails.
	 */
	if (this_node == NULL) {
		stailq_foreach_entry(i, &field->map.items, in_items) {
			if (i->key != NULL) {
				out = mp_encode_str(out, i->key, i->key_len);
				out += xrow_update_field_store(&i->field, NULL,
							       NULL, out,
							       out_end);
			}
		}
	} else {
		struct json_token token;
		token.type = JSON_TOKEN_STR;
		struct json_token *next_node;
		stailq_foreach_entry(i, &field->map.items, in_items) {
			if (i->key != NULL) {
				token.str = i->key;
				token.len = i->key_len;
				next_node = json_tree_lookup(format_tree,
							     this_node,
							     &token);
				out = mp_encode_str(out, i->key, i->key_len);
				out += xrow_update_field_store(&i->field,
							       format_tree,
							       next_node, out,
							       out_end);
			}
		}
	}
	stailq_foreach_entry(i, &field->map.items, in_items) {
		memcpy(out, i->field.data + i->field.size, i->tail_size);
		out += i->tail_size;
	}
	assert(out <= out_end);
	return out - out_begin;
}
