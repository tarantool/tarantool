/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "space_cache.h"

#include "assoc.h"
#include "alter.h"
#include "tuple.h"
#include "wal_ext.h"

/** ID -> space dictionary. */
static struct mh_i32ptr_t *spaces;
/** Name -> space dictionary. */
static struct mh_strnptr_t *spaces_by_name;

/**
 * Internal change counter. Grows faster, than public schema_version,
 * because we need to remember when to update pointers to already
 * non-existent space objects on space:truncate() operation.
 */
uint32_t space_cache_version;

const char *space_cache_holder_type_strs[SPACE_HOLDER_MAX] = {
	"foreign key",
};

void
space_cache_init(void)
{
	spaces = mh_i32ptr_new();
	spaces_by_name = mh_strnptr_new();
}

void
space_cache_destroy(void)
{
	if (spaces == NULL)
		return;
	while (mh_size(spaces) > 0) {
		mh_int_t i = mh_first(spaces);

		struct space *space = (struct space *)
			mh_i32ptr_node(spaces, i)->val;
		space_cache_replace(space, NULL);
		space_delete(space);
	}
	mh_i32ptr_delete(spaces);
	mh_strnptr_delete(spaces_by_name);
}

/** Return space by its number */
struct space *
space_by_id(uint32_t id)
{
	mh_int_t space = mh_i32ptr_find(spaces, id, NULL);
	if (space == mh_end(spaces))
		return NULL;
	return (struct space *)mh_i32ptr_node(spaces, space)->val;
}

/** Return space by its name */
struct space *
space_by_name(const char *name)
{
	mh_int_t space = mh_strnptr_find_str(spaces_by_name, name,
					     strlen(name));
	if (space == mh_end(spaces_by_name))
		return NULL;
	return (struct space *)mh_strnptr_node(spaces_by_name, space)->val;
}

/**
 * Visit all spaces and apply 'func'.
 */
int
space_foreach(int (*func)(struct space *sp, void *udata), void *udata)
{
	mh_int_t i;
	struct space *space;
	char key[6];
	assert(mp_sizeof_uint(BOX_SYSTEM_ID_MIN) <= sizeof(key));
	mp_encode_uint(key, BOX_SYSTEM_ID_MIN);

	/*
	 * Make sure we always visit system spaces first,
	 * in order from lowest space id to the highest..
	 * This is essential for correctly recovery from the
	 * snapshot, and harmless otherwise.
	 */
	space = space_by_id(BOX_SPACE_ID);
	struct index *pk = space ? space_index(space, 0) : NULL;
	if (pk) {
		struct iterator *it = index_create_iterator(pk, ITER_GE,
							    key, 1, NULL);
		if (it == NULL)
			return -1;
		int rc;
		struct tuple *tuple;
		while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL) {
			uint32_t id;
			if (tuple_field_u32(tuple, BOX_SPACE_FIELD_ID,
					    &id) != 0)
				continue;
			space = space_cache_find(id);
			if (space == NULL)
				continue;
			if (!space_is_system(space))
				break;
			rc = func(space, udata);
			if (rc != 0)
				break;
		}
		iterator_delete(it);
		if (rc != 0)
			return -1;
	}

	mh_foreach(spaces, i) {
		space = (struct space *)mh_i32ptr_node(spaces, i)->val;
		if (space_is_system(space))
			continue;
		if (func(space, udata) != 0)
			return -1;
	}

	return 0;
}

/**
 * If the @a old_space space is pinned, relink holders of that space to
 * the @a new_space.
 */
static void
space_cache_repin_pinned(struct space *old_space, struct space *new_space)
{
	assert(new_space != NULL);
	if (old_space == NULL)
		return;

	assert(rlist_empty(&new_space->space_cache_pin_list));
	rlist_swap(&new_space->space_cache_pin_list,
		   &old_space->space_cache_pin_list);

	struct space_cache_holder *h;
	rlist_foreach_entry(h, &new_space->space_cache_pin_list, link) {
		assert(h->space == old_space);
		h->space = new_space;
		h->on_replace(h, old_space);
	}
}

void
space_cache_replace(struct space *old_space, struct space *new_space)
{
	assert(new_space != NULL || old_space != NULL);
	if (new_space != NULL) {
		/*
		 * If the replaced space has a different name, we
		 * must explicitly delete it from @spaces_by_name
		 * cache. Note, since a space id never changes, we
		 * don't need to do so for @spaces cache.
		 */
		struct space *old_space_by_name = NULL;
		if (old_space != NULL && strcmp(space_name(old_space),
						space_name(new_space)) != 0) {
			const char *name = space_name(old_space);
			mh_int_t k = mh_strnptr_find_str(spaces_by_name, name,
							 strlen(name));
			assert(k != mh_end(spaces_by_name));
			old_space_by_name = (struct space *)
				mh_strnptr_node(spaces_by_name, k)->val;
			mh_strnptr_del(spaces_by_name, k, NULL);
		}
		/*
		 * Insert @new_space into @spaces cache, replacing
		 * @old_space if it's not NULL.
		 */
		const struct mh_i32ptr_node_t node_p = { space_id(new_space),
							 new_space };
		struct mh_i32ptr_node_t old, *p_old = &old;
		mh_i32ptr_put(spaces, &node_p, &p_old, NULL);
		struct space *old_space_by_id = p_old != NULL ?
						(struct space *)p_old->val :
						NULL;
		assert(old_space_by_id == old_space);
		(void)old_space_by_id;
		/*
		 * Insert @new_space into @spaces_by_name cache.
		 */
		const char *name = space_name(new_space);
		uint32_t name_len = strlen(name);
		uint32_t name_hash = mh_strn_hash(name, name_len);
		const struct mh_strnptr_node_t node_s = {name, name_len,
							 name_hash, new_space};
		struct mh_strnptr_node_t old_s, *p_old_s = &old_s;
		mh_strnptr_put(spaces_by_name, &node_s, &p_old_s, NULL);
		if (old_space_by_name == NULL && p_old_s != NULL)
			old_space_by_name = (struct space *)p_old_s->val;
		assert(old_space_by_name == old_space);
		(void)old_space_by_name;

		/* If old space is pinned, we have to pin the new space. */
		space_cache_repin_pinned(old_space, new_space);
		/*
		 * We should update reference to WAL extensions; otherwise
		 * since alter operation may yield and then rollback
		 * (e.g. due to disk issues) - in this gap WAL extensions can
		 * be reconfigured; as a result space->wal_ext will point to
		 * dangling (already freed) memory.
		 */
		new_space->wal_ext = space_wal_ext_by_name(name);
	} else {
		/*
		 * Delete @old_space from @spaces cache.
		 */
		mh_int_t k = mh_i32ptr_find(spaces, space_id(old_space), NULL);
		assert(k != mh_end(spaces));
		struct space *old_space_by_id =
			(struct space *)mh_i32ptr_node(spaces, k)->val;
		assert(old_space_by_id == old_space);
		(void)old_space_by_id;
		mh_i32ptr_del(spaces, k, NULL);
		/*
		 * Delete @old_space from @spaces_by_name cache.
		 */
		const char *name = space_name(old_space);
		k = mh_strnptr_find_str(spaces_by_name, name, strlen(name));
		assert(k != mh_end(spaces_by_name));
		struct space *old_space_by_name =
			(struct space *)mh_strnptr_node(spaces_by_name, k)->val;
		assert(old_space_by_name == old_space);
		(void)old_space_by_name;
		mh_strnptr_del(spaces_by_name, k, NULL);
	}
	space_cache_version++;

	if (trigger_run(&on_alter_space, new_space != NULL ?
					 new_space : old_space) != 0) {
		diag_log();
		panic("Can't update space cache");
	}

	if (old_space != NULL)
		space_invalidate(old_space);
}

void
space_cache_on_replace_noop(struct space_cache_holder *holder,
			    struct space *old_space)
{
	(void)holder;
	(void)old_space;
}

void
space_cache_pin(struct space *space, struct space_cache_holder *holder,
		space_cache_on_replace on_replace,
		enum space_cache_holder_type type, bool selfpin)
{
	if (!selfpin)
		assert(mh_i32ptr_find(spaces, space->def->id, NULL)
		       != mh_end(spaces));
	holder->on_replace = on_replace;
	holder->type = type;
	rlist_add_tail(&space->space_cache_pin_list, &holder->link);
	holder->space = space;
	holder->selfpin = selfpin;
}

void
space_cache_unpin(struct space_cache_holder *holder)
{
	struct space *space = holder->space; (void)space;
	if (!holder->selfpin)
		assert(mh_i32ptr_find(spaces, space->def->id, NULL)
		       != mh_end(spaces));
#ifndef NDEBUG
	/* Paranoid check that the holder in space's pin list. */
	bool is_in_list = false;
	struct rlist *tmp;
	rlist_foreach(tmp, &space->space_cache_pin_list)
		is_in_list = is_in_list || tmp == &holder->link;
	assert(is_in_list);
#endif
	rlist_del(&holder->link);
	holder->space = NULL;
}

bool
space_cache_is_pinned(struct space *space, enum space_cache_holder_type *type)
{
	assert(mh_i32ptr_find(spaces, space->def->id, NULL) != mh_end(spaces));
	struct space_cache_holder *h;
	rlist_foreach_entry(h, &space->space_cache_pin_list, link) {
		/* Self-pinned spaces are treated as not pinned. */
		if (!h->selfpin) {
			*type = h->type;
			return true;
		}
	}
	return false;
}
