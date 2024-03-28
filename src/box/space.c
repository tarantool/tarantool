/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "space.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "bit/bit.h"
#include "tuple_format.h"
#include "trigger.h"
#include "user.h"
#include "session.h"
#include "txn.h"
#include "memtx_tx.h"
#include "tuple.h"
#include "xrow_update.h"
#include "request.h"
#include "xrow.h"
#include "iproto.h"
#include "iproto_constants.h"
#include "schema.h"
#include "assoc.h"
#include "box.h"
#include "space_upgrade.h"
#include "tuple_constraint.h"
#include "tuple_constraint_func.h"
#include "tuple_constraint_fkey.h"
#include "field_default_func.h"
#include "wal_ext.h"
#include "coll_id_cache.h"
#include "core/func_adapter.h"
#include "lua/utils.h"
#include "core/mp_ctx.h"
#include "port.h"

int
access_check_space(struct space *space, user_access_t access)
{
	struct credentials *cr = effective_user();
	/* Any space access also requires global USAGE privilege. */
	access |= PRIV_U;
	/*
	 * If a user has a global permission, clear the respective
	 * privilege from the list of privileges required
	 * to execute the request.
	 * No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	user_access_t space_access = access & ~cr->universal_access;
	/*
	 * Similarly to global access, subtract entity-level access
	 * (access to all spaces) if it is present.
	 */
	space_access &= ~entity_access_get(SC_SPACE)[cr->auth_token].effective;

	if (space_access &&
	    /* Check for missing USAGE access, ignore owner rights. */
	    (space_access & PRIV_U ||
	     /* Check for missing specific access, respect owner rights. */
	    (space->def->uid != cr->uid &&
	     space_access & ~space->access[cr->auth_token].effective))) {
		/*
		 * Report access violation. Throw "no such user"
		 * error if there is no user with this id.
		 * It is possible that the user was dropped
		 * from a different connection.
		 */
		struct user *user = user_find(cr->uid);
		if (user != NULL) {
			if (!(cr->universal_access & PRIV_U)) {
				diag_set(AccessDeniedError,
					 priv_name(PRIV_U),
					 schema_object_name(SC_UNIVERSE), "",
					 user->def->name);
			} else {
				diag_set(AccessDeniedError,
					 priv_name(access),
					 schema_object_name(SC_SPACE),
					 space->def->name, user->def->name);
			}
		}
		return -1;
	}
	return 0;
}

void
space_fill_index_map(struct space *space)
{
	uint32_t index_count = 0;
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		struct index *index = space->index_map[j];
		if (index) {
			assert(index_count < space->index_count);
			index->dense_id = index_count;
			space->index[index_count++] = index;
		}
	}
}

bool
space_is_system(const struct space *space)
{
	return space_id_is_system(space->def->id);
}

/**
 * Initialize constraints that are defined in @a space format.
 * Can return nonzero in case of error (diag is set).
 */
static int
space_init_constraints(struct space *space)
{
	assert(space->def != NULL);
	struct tuple_format *format = space->format;
	bool has_foreign_keys = false;
	for (size_t j = 0; j < format->constraint_count; j++) {
		struct tuple_constraint *constr = &format->constraint[j];
		has_foreign_keys = has_foreign_keys ||
				   constr->def.type == CONSTR_FKEY;
		if (constr->check != tuple_constraint_noop_check)
			continue;
		if (constr->def.type == CONSTR_FUNC) {
			if (tuple_constraint_func_init(constr, space,
						       false) != 0)
				return -1;
		} else {
			assert(constr->def.type == CONSTR_FKEY);
			if (tuple_constraint_fkey_init(constr, space, -1) != 0)
				return -1;
		}
	}
	for (uint32_t i = 0; i < tuple_format_field_count(format); i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		for (size_t j = 0; j < field->constraint_count; j++) {
			struct tuple_constraint *constr = &field->constraint[j];
			has_foreign_keys = has_foreign_keys ||
					   constr->def.type == CONSTR_FKEY;
			if (constr->check != tuple_constraint_noop_check)
				continue;
			if (constr->def.type == CONSTR_FUNC) {
				if (tuple_constraint_func_init(constr,
							       space,
							       true) != 0)
					return -1;
			} else {
				assert(constr->def.type == CONSTR_FKEY);
				if (tuple_constraint_fkey_init(constr,
							       space, i) != 0)
					return -1;
			}
		}
	}
	space->has_foreign_keys = has_foreign_keys;
	return 0;
}

/**
 * Detach constraints that are defined in @a space format.
 */
void
space_detach_constraints(struct space *space)
{
	struct tuple_format *format = space->format;
	for (size_t j = 0; j < format->constraint_count; j++) {
		struct tuple_constraint *constr = &format->constraint[j];
		constr->detach(constr);
	}
	for (size_t i = 0; i < tuple_format_field_count(format); i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		for (size_t j = 0; j < field->constraint_count; j++) {
			struct tuple_constraint *constr = &field->constraint[j];
			constr->detach(constr);
		}
	}
}

/**
 * Reattach constraints that are defined in @a space format.
 */
void
space_reattach_constraints(struct space *space)
{
	struct tuple_format *format = space->format;
	for (size_t j = 0; j < format->constraint_count; j++) {
		struct tuple_constraint *constr = &format->constraint[j];
		constr->reattach(constr);
	}
	for (size_t i = 0; i < tuple_format_field_count(format); i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		for (size_t j = 0; j < field->constraint_count; j++) {
			struct tuple_constraint *constr = &field->constraint[j];
			constr->reattach(constr);
		}
	}
}

/**
 * Destroy constraints that are defined in @a space format.
 */
static int
space_cleanup_constraints(struct space *space)
{
	struct tuple_format *format = space->format;
	for (size_t j = 0; j < format->constraint_count; j++) {
		struct tuple_constraint *constr = &format->constraint[j];
		constr->destroy(constr);
	}
	for (size_t i = 0; i < tuple_format_field_count(format); i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		for (size_t j = 0; j < field->constraint_count; j++) {
			struct tuple_constraint *constr = &field->constraint[j];
			constr->destroy(constr);
		}
	}
	return 0;
}

/**
 * Pin collation identifier with `id` in the cache, so that it can't be deleted.
 */
static void
space_pin_collations_helper(struct space *space, uint32_t id,
			    enum coll_id_holder_type holder_type)
{
	if (id == COLL_NONE)
		return;
	struct coll_id *coll_id = coll_by_id(id);
	assert(coll_id != NULL);
	struct coll_id_cache_holder *h = xmalloc(sizeof(*h));
	rlist_add_tail_entry(&space->coll_id_holders, h, in_space);
	coll_id_pin(coll_id, h, holder_type);
}

void
space_pin_collations(struct space *space)
{
	struct tuple_format *format = space->format;
	for (uint32_t i = 0; i < tuple_format_field_count(format); i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		space_pin_collations_helper(space, field->coll_id,
					    COLL_ID_HOLDER_SPACE_FORMAT);
	}

	for (uint32_t i = 0; i < space->index_count; i++) {
		struct key_def *key_def = space->index[i]->def->key_def;
		for (uint32_t i = 0; i < key_def->part_count; i++) {
			struct key_part *part = &key_def->parts[i];
			space_pin_collations_helper(space, part->coll_id,
						    COLL_ID_HOLDER_INDEX);
		}
	}
}

void
space_unpin_collations(struct space *space)
{
	struct coll_id_cache_holder *h, *tmp;
	rlist_foreach_entry_safe(h, &space->coll_id_holders, in_space, tmp) {
		coll_id_unpin(h);
		free(h);
	}
	rlist_create(&space->coll_id_holders);
}

/**
 * Initialize functional default field values that are defined in space format.
 * Can return nonzero in case of error (diag is set).
 */
static int
space_init_defaults(struct space *space)
{
	struct tuple_format *format = space->format;
	for (uint32_t i = 0; i < format->default_field_count; i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		if (tuple_field_has_default_func(field) &&
		    field_default_func_init(&field->default_value.func) != 0)
			return -1;
	}
	return 0;
}

void
space_pin_defaults(struct space *space)
{
	struct tuple_format *format = space->format;
	for (uint32_t i = 0; i < format->default_field_count; i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		if (tuple_field_has_default_func(field))
			field_default_func_pin(&field->default_value.func);
	}
}

void
space_unpin_defaults(struct space *space)
{
	struct tuple_format *format = space->format;
	for (uint32_t i = 0; i < format->default_field_count; i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		if (tuple_field_has_default_func(field))
			field_default_func_unpin(&field->default_value.func);
	}
}

/**
 * Setup each event in `events', associated with the `space' by id and by name.
 */
static void
space_set_events_impl(struct space *space, struct space_event *events[],
		      size_t event_count, const char *const by_id_fmt[],
		      const char *const by_name_fmt[])
{
	size_t space_id_len = snprintf(NULL, 0, "%u", space_id(space));
	size_t space_name_len = strlen(space_name(space));

	for (size_t i = 0; i < event_count; ++i) {
		size_t region_svp = region_used(&fiber()->gc);

		/* Set event by id. */
		size_t buf_size = strlen(by_id_fmt[i]) + space_id_len
				  - strlen("%u") + 1;
		char *event_name = xregion_alloc(&fiber()->gc, buf_size);
		snprintf(event_name, buf_size, by_id_fmt[i], space_id(space));
		struct event **event = &events[i]->by_id;
		assert(*event == NULL);
		*event = event_get(event_name, true);
		event_ref(*event);

		/* Set event by name. */
		buf_size = strlen(by_name_fmt[i]) + space_name_len
			   - strlen("%s") + 1;
		event_name = xregion_alloc(&fiber()->gc, buf_size);
		snprintf(event_name, buf_size, by_name_fmt[i],
			 space_name(space));
		event = &events[i]->by_name;
		assert(*event == NULL);
		*event = event_get(event_name, true);
		event_ref(*event);

		region_truncate(&fiber()->gc, region_svp);
	}
}

/**
 * Set DML events associated with the space: on_replace and before_replace,
 * bound by name and by id. If argument is_on_recovery is true, recovery events
 * are set instead of regular ones.
 */
static void
space_set_dml_events(struct space *space, bool is_on_recovery)
{
	assert(space->def != NULL);
	/*
	 * Space events - on_replace and before_replace events, associated with
	 * space by id and by name. All format strings for event names must be
	 * declared in the same order.
	 */
	struct space_event *space_events[] = {
		&space->on_replace_event,
		&space->before_replace_event,
	};
	/*
	 * Format strings for names of events associated with the space by id
	 * and by name. The space has several versions of each replace trigger
	 * (for example, on_recovery_replace which is fired only on recovery and
	 * on_replace which is fired after recovery).
	 */
	static const char *const event_by_id_recovery_fmt[] = {
		"box.space[%u].on_recovery_replace",
		"box.space[%u].before_recovery_replace",
	};
	static const char *const event_by_id_fmt[] = {
		"box.space[%u].on_replace",
		"box.space[%u].before_replace",
	};
	static const char *const event_by_name_recovery_fmt[] = {
		"box.space.%s.on_recovery_replace",
		"box.space.%s.before_recovery_replace",
	};
	static const char *const event_by_name_fmt[] = {
		"box.space.%s.on_replace",
		"box.space.%s.before_replace",
	};
	static_assert(lengthof(space_events) ==
		      lengthof(event_by_id_recovery_fmt),
		      "Every space event must be covered with name");
	static_assert(lengthof(space_events) == lengthof(event_by_id_fmt),
		      "Every space event must be covered with name");
	static_assert(lengthof(space_events) ==
		      lengthof(event_by_name_recovery_fmt),
		      "Every space event must be covered with name");
	static_assert(lengthof(space_events) == lengthof(event_by_name_fmt),
		      "Every space event must be covered with name");

	const char *const *by_id_fmt =
		is_on_recovery ? event_by_id_recovery_fmt : event_by_id_fmt;
	const char *const *by_name_fmt =
		is_on_recovery ? event_by_name_recovery_fmt : event_by_name_fmt;

	space->run_recovery_triggers = is_on_recovery;

	space_set_events_impl(space, space_events, lengthof(space_events),
			      by_id_fmt, by_name_fmt);
}

/**
 * Set transactional events associated with the space, bound by space name and
 * by space id.
 */
static void
space_set_txn_events(struct space *space)
{
	assert(space->def != NULL);
	/*
	 * Transactional events associated with the space by id and by name. All
	 * format strings for event names must be declared in the same order.
	 */
	struct space_event *txn_events[] = {
		&space->txn_events[TXN_EVENT_BEFORE_COMMIT],
		&space->txn_events[TXN_EVENT_ON_COMMIT],
		&space->txn_events[TXN_EVENT_ON_ROLLBACK],
	};
	static_assert(lengthof(space->txn_events) == lengthof(txn_events),
		      "Every txn event must be present in txn_events");
	/*
	 * Format strings for event names.
	 */
	static const char *const event_by_id_fmt[] = {
		"box.before_commit.space[%u]",
		"box.on_commit.space[%u]",
		"box.on_rollback.space[%u]",
	};
	static const char *const event_by_name_fmt[] = {
		"box.before_commit.space.%s",
		"box.on_commit.space.%s",
		"box.on_rollback.space.%s",
	};
	static_assert(lengthof(event_by_id_fmt) == lengthof(txn_events),
		      "Every txn event must be covered with name");
	static_assert(lengthof(event_by_name_fmt) == lengthof(txn_events),
		      "Every txn event must be covered with name");

	space_set_events_impl(space, txn_events, lengthof(txn_events),
			      event_by_id_fmt, event_by_name_fmt);
}

/**
 * Set all events (DML and transactional) associated with the space, bound by
 * name and by id. If argument is_on_recovery is true, recovery events are set
 * instead of regular ones (applicable only to DML events).
 */
static void
space_set_events(struct space *space, bool is_on_recovery)
{
	space_set_dml_events(space, is_on_recovery);
	space_set_txn_events(space);
}

/**
 * Reset space events.
 */
static void
space_reset_events(struct space *space)
{
	struct space_event *space_dml_events[] = {
		&space->on_replace_event,
		&space->before_replace_event,
	};
	for (size_t i = 0; i < lengthof(space_dml_events); ++i) {
		event_unref(space_dml_events[i]->by_id);
		space_dml_events[i]->by_id = NULL;
		event_unref(space_dml_events[i]->by_name);
		space_dml_events[i]->by_name = NULL;
	}
	for (size_t i = 0; i < lengthof(space->txn_events); ++i) {
		event_unref(space->txn_events[i].by_id);
		space->txn_events[i].by_id = NULL;
		event_unref(space->txn_events[i].by_name);
		space->txn_events[i].by_name = NULL;
	}
	space->run_recovery_triggers = false;
}

void
space_remove_temporary_triggers(struct space *space)
{
	struct space_event *space_events[] = {
		&space->on_replace_event,
		&space->before_replace_event,
	};
	for (size_t i = 0; i < lengthof(space_events); ++i) {
		event_remove_temporary_triggers(space_events[i]->by_id);
		event_remove_temporary_triggers(space_events[i]->by_name);
	}
}

int
space_create(struct space *space, struct engine *engine,
	     const struct space_vtab *vtab, struct space_def *def,
	     struct rlist *key_list, struct tuple_format *format)
{
	if (!rlist_empty(key_list)) {
		/* Primary key must go first. */
		struct index_def *pk = rlist_first_entry(key_list,
					struct index_def, link);
		assert(pk->iid == 0);
		(void)pk;
	}

	uint32_t index_id_max = 0;
	uint32_t index_count = 0;
	struct index_def *index_def;
	rlist_foreach_entry(index_def, key_list, link) {
		index_count++;
		index_id_max = MAX(index_id_max, index_def->iid);
	}

	memset(space, 0, sizeof(*space));
	space->vtab = vtab;
	space->engine = engine;
	space->index_count = index_count;
	space->index_id_max = index_id_max;
	rlist_create(&space->before_replace);
	rlist_create(&space->on_replace);
	rlist_create(&space->coll_id_holders);
	space->run_triggers = true;

	space->format = format;
	if (format != NULL)
		tuple_format_ref(format);

	space->def = space_def_dup(def);
	bool is_on_recovery = recovery_state < FINISHED_RECOVERY;
	space_set_events(space, is_on_recovery);

	/* Create indexes and fill the index map. */
	space->index_map = (struct index **)
		calloc(index_count + index_id_max + 1, sizeof(struct index *));
	if (space->index_map == NULL) {
		diag_set(OutOfMemory, (index_count + index_id_max + 1) *
			 sizeof(struct index *), "malloc", "index_map");
		goto fail;
	}
	space->index = space->index_map + index_id_max + 1;
	size_t size = BITMAP_SIZE(index_id_max + 1);
	space->check_unique_constraint_map = calloc(size, 1);
	if (space->check_unique_constraint_map == NULL) {
		diag_set(OutOfMemory, size, "malloc",
			 "check_unique_constraint_map");
		goto fail;
	}
	rlist_foreach_entry(index_def, key_list, link) {
		struct index *index = space_create_index(space, index_def);
		if (index == NULL)
			goto fail_free_indexes;
		space->index_map[index_def->iid] = index;
		if (index_def->opts.is_unique)
			bit_set(space->check_unique_constraint_map,
				index_def->iid);
	}
	space_fill_index_map(space);

	rlist_create(&space->space_cache_pin_list);
	if (space_init_constraints(space) != 0)
		goto fail_free_indexes;
	space_pin_collations(space);
	if (space_init_defaults(space) != 0)
		goto fail_free_indexes;

	/*
	 * Check if there are unique indexes that are contained
	 * by other unique indexes. For them, we can skip check
	 * for duplicates on INSERT. Prefer indexes with higher
	 * ids for uniqueness check optimization as they are
	 * likelier to have a "colder" cache.
	 */
	for (int i = space->index_count - 1; i >= 0; i--) {
		struct index *index = space->index[i];
		if (!bit_test(space->check_unique_constraint_map,
			      index->def->iid))
			continue;
		for (int j = 0; j < (int)space->index_count; j++) {
			struct index *other = space->index[j];
			if (i != j && bit_test(space->check_unique_constraint_map,
					       other->def->iid) &&
			    key_def_contains(index->def->key_def,
					     other->def->key_def)) {
				bit_clear(space->check_unique_constraint_map,
					  index->def->iid);
				break;
			}
		}
	}
	rlist_create(&space->memtx_stories);
	rlist_create(&space->alter_stmts);
	space->lua_ref = LUA_NOREF;
	return 0;

fail_free_indexes:
	for (uint32_t i = 0; i <= index_id_max; i++) {
		struct index *index = space->index_map[i];
		if (index != NULL)
			index_unref(index);
	}
fail:
	free(space->index_map);
	free(space->check_unique_constraint_map);
	if (space->def != NULL)
		space_def_delete(space->def);
	if (space->format != NULL) {
		space_cleanup_constraints(space);
		space_unpin_defaults(space);
		tuple_format_unref(space->format);
	}
	space_unpin_collations(space);
	return -1;
}

int
space_on_initial_recovery_complete(struct space *space, void *nothing)
{
	(void)nothing;
	if (space_init_constraints(space) != 0)
		return -1;
	if (space_init_defaults(space) != 0)
		return -1;
	return 0;
}

int
space_on_final_recovery_complete(struct space *space, void *nothing)
{
	(void)nothing;
	space_reset_events(space);
	space_set_events(space, false);
	space_upgrade_run(space);
	return 0;
}

int
space_on_bootstrap_complete(struct space *space, void *nothing)
{
	(void)nothing;
	space_reset_events(space);
	space_set_events(space, false);
	return 0;
}

struct space *
space_new(struct space_def *def, struct rlist *key_list)
{
	struct engine *engine = engine_find(def->engine_name);
	if (engine == NULL)
		return NULL;
	return engine_create_space(engine, def, key_list);
}

struct space *
space_new_ephemeral(struct space_def *def, struct rlist *key_list)
{
	assert(space_opts_is_data_temporary(&def->opts));
	assert(def->opts.is_ephemeral);
	struct space *space = space_new(def, key_list);
	if (space == NULL)
		return NULL;
	space->vtab->init_ephemeral_space(space);
	return space;
}

void
space_delete(struct space *space)
{
	assert(rlist_empty(&space->alter_stmts));
	memtx_tx_on_space_delete(space);
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		struct index *index = space->index_map[j];
		if (index != NULL)
			index_unref(index);
	}
	free(space->index_map);
	free(space->check_unique_constraint_map);
	if (space->format != NULL) {
		space_cleanup_constraints(space);
		space_unpin_defaults(space);
		tuple_format_unref(space->format);
	}
	space_unpin_collations(space);
	trigger_destroy(&space->before_replace);
	trigger_destroy(&space->on_replace);
	space_reset_events(space);
	if (space->upgrade != NULL)
		space_upgrade_delete(space->upgrade);
	space_def_delete(space->def);
	/*
	 * SQL triggers should be deleted with on_replace_dd_triggers on
	 * deletion from corresponding system space.
	 */
	assert(space->sql_triggers == NULL);
	assert(rlist_empty(&space->space_cache_pin_list));
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, space->lua_ref);
	space->vtab->destroy(space);
}

/**
 * Call a visitor function for spaces with id in the range [id_min..id_max].
 */
static int
space_foreach_helper(uint32_t id_min, uint32_t id_max, struct index *pk,
		     int (*func)(struct space *sp, void *udata), void *udata)
{
	char key[6];
	assert(mp_sizeof_uint(id_min) <= sizeof(key));
	mp_encode_uint(key, id_min);
	struct iterator *it = index_create_iterator(pk, ITER_GE, key, 1);
	if (it == NULL)
		return -1;
	int rc;
	struct tuple *tuple;
	while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL) {
		uint32_t id;
		if (tuple_field_u32(tuple, BOX_SPACE_FIELD_ID, &id) != 0)
			continue;
		if (id > id_max)
			break;
		struct space *space = space_cache_find(id);
		if (space == NULL)
			break;
		rc = func(space, udata);
		if (rc != 0)
			break;
	}
	iterator_delete(it);
	if (rc != 0)
		return -1;
	return 0;
}

int
space_foreach(int (*func)(struct space *sp, void *udata), void *udata)
{
	struct space *space = space_by_id(BOX_SPACE_ID);
	assert(space != NULL);
	struct index *pk = space_index(space, 0);
	assert(pk != NULL);
	/*
	 * Iterate over system spaces.
	 */
	int rc = space_foreach_helper(BOX_SYSTEM_ID_MIN, BOX_SYSTEM_ID_MAX,
				      pk, func, udata);
	if (rc != 0)
		return -1;
	/*
	 * Iterate over non-system spaces.
	 */
	rc = space_foreach_helper(0, BOX_SYSTEM_ID_MIN - 1, pk, func, udata);
	if (rc != 0)
		return -1;
	rc = space_foreach_helper(BOX_SYSTEM_ID_MAX + 1, BOX_SPACE_MAX,
				  pk, func, udata);
	if (rc != 0)
		return -1;
	return 0;
}

void
space_dump_def(const struct space *space, struct rlist *key_list)
{
	rlist_create(key_list);

	/** Ensure the primary key is added first. */
	for (unsigned j = 0; j < space->index_count; j++)
		rlist_add_tail_entry(key_list, space->index[j]->def, link);
}

struct key_def *
space_index_key_def(struct space *space, uint32_t id)
{
	if (id <= space->index_id_max && space->index_map[id])
		return space->index_map[id]->def->key_def;
	return NULL;
}

void
generic_space_swap_index(struct space *old_space, struct space *new_space,
			 uint32_t old_index_id, uint32_t new_index_id)
{
	SWAP(old_space->index_map[old_index_id],
	     new_space->index_map[new_index_id]);
}

void
space_run_triggers(struct space *space, bool yesno)
{
	space->run_triggers = yesno;
}

size_t
space_bsize(struct space *space)
{
	return space->vtab->bsize(space);
}

struct index_def *
space_index_def(struct space *space, int n)
{
	return space->index[n]->def;
}

const char *
index_name_by_id(struct space *space, uint32_t id)
{
	struct index *index = space_index(space, id);
	if (index != NULL)
		return index->def->name;
	return NULL;
}

/**
 * Pushes arguments for replace triggers (on_replace, before_replace)
 * to port_c. Transaction mustn't be aborted.
 */
static void
space_push_replace_trigger_arguments(struct port *args, struct txn *txn)
{
	assert(txn_check_can_continue(txn) == 0);
	struct txn_stmt *stmt = txn_current_stmt(txn);
	assert(stmt != NULL);

	if (stmt->old_tuple != NULL)
		port_c_add_tuple(args, stmt->old_tuple);
	else
		port_c_add_null(args);
	if (stmt->new_tuple != NULL)
		port_c_add_tuple(args, stmt->new_tuple);
	else
		port_c_add_null(args);
	/* TODO: maybe the space object has to be here */
	port_c_add_str0(args, space_name(stmt->space));
	/* Operation type: INSERT/UPDATE/UPSERT/REPLACE/DELETE */
	port_c_add_str0(args, iproto_type_name(stmt->type));
	/* Pass xrow header and body to recovery triggers. */
	if (stmt->space->run_recovery_triggers) {
		struct xrow_header *row = stmt->row;
		assert(row != NULL && row->header != NULL);
		port_c_add_mp_object(args, row->header, row->header_end,
				     &iproto_mp_ctx);
		assert(row->bodycnt == 1);
		const char *body = row->body[0].iov_base;
		const char *body_end = body + row->body[0].iov_len;
		port_c_add_mp_object(args, body, body_end,
				     &iproto_mp_ctx);
	}
}

/**
 * Run replace triggers from passed event. If argument update_tuple is true,
 * new tuple in current statement is updated after each trigger:
 * 1. the tuple is set to returned tuple,
 * 2. the tuple is set to NULL if a trigger returned NULL,
 * 3. the tuple is not updated if a trigger returned nothing,
 * 4. an error is thrown otherwise.
 * Updated tuple is validated against the space format.
 */
static int
space_run_replace_triggers(struct event *event, struct txn *txn,
			   bool update_tuple)
{
	/** Return early if event has no triggers or if txn can't continue. */
	if (!event_has_triggers(event))
		return 0;
	int rc = txn_check_can_continue(txn);
	if (rc != 0)
		return -1;

	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	struct func_adapter *trigger = NULL;
	const char *name = NULL;
	struct port args, ret;
	/* Don't pass port for returned values if we don't update tuple. */
	struct port *ret_ptr = update_tuple ? &ret : NULL;
	port_c_create(&args);
	space_push_replace_trigger_arguments(&args, txn);
	while (rc == 0 && event_trigger_iterator_next(&it, &trigger, &name)) {
		bool has_ret = false;
		uint32_t region_svp = region_used(&fiber()->gc);
		/*
		 * The transaction could be aborted while the previous trigger
		 * was running (e.g. if the trigger callback yielded).
		 */
		rc = txn_check_can_continue(txn);
		if (rc != 0)
			goto out;
		rc = func_adapter_call(trigger, &args, ret_ptr);
		/*
		 * We have returned values only if we update tuple and
		 * the trigger hasn't failed.
		 */
		has_ret = update_tuple && rc == 0;
		if (rc != 0 || !update_tuple)
			goto out;

		/*
		 * The transaction could be aborted while the trigger was
		 * running (e.g. if the trigger callback yielded).
		 */
		rc = txn_check_can_continue(txn);
		if (rc != 0)
			goto out;
		struct txn_stmt *stmt = txn_current_stmt(txn);
		assert(stmt != NULL);

		/*
		 * Pop the returned value.
		 * See the function description for details.
		 */
		struct tuple *result = NULL;
		const struct port_c_entry *retvals = port_get_c_entries(&ret);
		if (retvals == NULL) {
			rc = 0;
			goto out;
		} else if (retvals->type == PORT_C_ENTRY_TUPLE) {
			result = retvals->tuple;
		} else if (retvals->type != PORT_C_ENTRY_NULL) {
			diag_set(ClientError, ER_BEFORE_REPLACE_RET);
			rc = -1;
			goto out;
		}

		/*
		 * Tuple returned by a before_replace trigger callback must
		 * match the space format.
		 *
		 * Since upgrade from pre-1.7.5 versions passes tuple with not
		 * suitable format to before_recovery_replace triggers,
		 * we need to disable format validation on recovery triggers.
		 */
		if (!stmt->space->run_recovery_triggers &&
		    result != NULL &&
		    tuple_validate(stmt->space->format, result) != 0) {
			rc = -1;
			goto out;
		}

		/* Update the new tuple. */
		if (result != NULL)
			tuple_ref(result);
		if (stmt->new_tuple != NULL)
			tuple_unref(stmt->new_tuple);
		stmt->new_tuple = result;
		/* Can't update values in port - destroy it and create again. */
		port_destroy(&args);
		port_c_create(&args);
		space_push_replace_trigger_arguments(&args, txn);
out:
		if (has_ret)
			port_destroy(&ret);
		region_truncate(&fiber()->gc, region_svp);
	}
	event_trigger_iterator_destroy(&it);
	port_destroy(&args);
	return rc;
}

int
space_on_replace(struct space *space, struct txn *txn)
{
	/*
	 * Since the triggers can yield, a space can be dropped
	 * while executing one of the trigger lists and all the events will be
	 * unreferenced - reference them to prevent use-after-free.
	 */
	struct event *events[] = {
		space->on_replace_event.by_id,
		space->on_replace_event.by_name,
	};
	for (size_t i = 0; i < lengthof(events); ++i)
		event_ref(events[i]);
	int rc = trigger_run(&space->on_replace, txn);
	for (size_t i = 0; i < lengthof(events) && rc == 0; ++i)
		rc = space_run_replace_triggers(events[i], txn, false);
	for (size_t i = 0; i < lengthof(events); ++i)
		event_unref(events[i]);
	return rc;
}

/**
 * Apply default values from the space format to the null (or absent) fields of
 * request->tuple.
 */
static int
space_apply_defaults(struct space *space, struct txn *txn,
		     struct request *request)
{
	assert(request->type == IPROTO_INSERT ||
	       request->type == IPROTO_REPLACE ||
	       request->type == IPROTO_UPSERT);

	const char *new_data = request->tuple;
	const char *new_data_end = request->tuple_end;
	size_t region_svp = region_used(&fiber()->gc);

	if (tuple_format_apply_defaults(space->format, &new_data,
					&new_data_end) != 0)
		return -1;

	bool is_tuple_changed = new_data != request->tuple;
	if (!is_tuple_changed)
		return 0;
	/*
	 * Field defaults changed the resulting tuple.
	 * Fix the request to conform.
	 */
	struct region *txn_region = tx_region_acquire(txn);
	int rc = request_create_from_tuple(request, space, NULL, 0, new_data,
					   new_data_end - new_data, txn_region,
					   true);
	tx_region_release(txn, TX_ALLOC_SYSTEM);
	region_truncate(&fiber()->gc, region_svp);
	return rc;
}

/**
 * Run BEFORE triggers and foreign key constraint checks registered for a space.
 * If a trigger changes the current statement, this function updates the
 * request accordingly.
 */
static int
space_before_replace(struct space *space, struct txn *txn,
		     struct request *request)
{
	enum iproto_type type = request->type;
	struct index *pk = space_index(space, 0);

	const char *key = NULL;
	uint32_t part_count = 0;
	struct index *index = NULL;
	struct tuple *old_tuple = NULL;
	size_t region_svp = region_used(&fiber()->gc);

	/*
	 * Lookup the old tuple.
	 */
	switch (type) {
	case IPROTO_UPDATE:
	case IPROTO_DELETE:
		index = index_find_unique(space, request->index_id);
		if (index == NULL)
			return -1;
		key = request->key;
		part_count = mp_decode_array(&key);
		break;
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
	case IPROTO_UPSERT:
		/*
		 * Since upgrade from pre-1.7.5 versions passes tuple with
		 * not suitable format to before_replace triggers during recovery,
		 * we need to disable format validation until box is configured.
		 */
		if (box_is_configured() && tuple_validate_raw(space->format,
							      request->tuple) != 0)
			return -1;
		if (pk == NULL)
			goto after_old_tuple_lookup;
		index = pk;
		key = tuple_extract_key_raw(request->tuple, request->tuple_end,
					    index->def->key_def, MULTIKEY_NONE,
					    NULL);
		if (key == NULL)
			return -1;
		part_count = mp_decode_array(&key);
		break;
	default:
		/* Unknown request type, nothing to do. */
		return 0;
	}

	if (exact_key_validate(index->def->key_def, key, part_count) != 0 ||
	    index_get(index, key, part_count, &old_tuple) != 0) {
		region_truncate(&fiber()->gc, region_svp);
		return -1;
	}
	region_truncate(&fiber()->gc, region_svp);

after_old_tuple_lookup:;

	/*
	 * Create the new tuple.
	 */
	uint32_t new_size, old_size = 0;
	const char *new_data, *new_data_end;
	const char *old_data = NULL, *old_data_end;

	switch (request->type) {
	case IPROTO_INSERT:
		new_data = request->tuple;
		new_data_end = request->tuple_end;
		break;
	case IPROTO_REPLACE:
		if (old_tuple != NULL)
			old_data = tuple_data_range(old_tuple, &old_size);
		new_data = request->tuple;
		new_data_end = request->tuple_end;
		break;
	case IPROTO_UPDATE:
		if (old_tuple == NULL) {
			/* Nothing to update. */
			return 0;
		}
		old_data = tuple_data_range(old_tuple, &old_size);
		old_data_end = old_data + old_size;
		new_data = xrow_update_execute(request->tuple,
					       request->tuple_end, old_data,
					       old_data_end,
					       space->format, &new_size,
					       request->index_base, NULL);
		if (new_data == NULL)
			return -1;
		new_data_end = new_data + new_size;
		break;
	case IPROTO_DELETE:
		if (old_tuple == NULL) {
			/* Nothing to delete. */
			return 0;
		}
		old_data = tuple_data_range(old_tuple, &old_size);
		new_data = new_data_end = NULL;
		break;
	case IPROTO_UPSERT:
		if (old_tuple == NULL) {
			/*
			 * Turn UPSERT into INSERT, but still check
			 * provided operations.
			 */
			new_data = request->tuple;
			new_data_end = request->tuple_end;
			if (xrow_update_check_ops(request->ops,
						  request->ops_end,
						  space->format,
						  request->index_base) != 0)
				return -1;
			break;
		}
		old_data = tuple_data_range(old_tuple, &old_size);
		old_data_end = old_data + old_size;
		new_data = xrow_upsert_execute(request->ops, request->ops_end,
					       old_data, old_data_end,
					       space->format, &new_size,
					       request->index_base, false,
					       NULL);
		new_data_end = new_data + new_size;
		break;
	default:
		unreachable();
	}

	struct tuple *new_tuple = NULL;
	if (new_data != NULL) {
		new_tuple = tuple_new(tuple_format_runtime,
				      new_data, new_data_end);
		region_truncate(&fiber()->gc, region_svp);
		if (new_tuple == NULL)
			return -1;
		tuple_ref(new_tuple);
	}

	assert(old_tuple != NULL || new_tuple != NULL);

	if (old_tuple != NULL) {
		/*
		 * Before deleting we must check that there are no tuples
		 * in this space or in other spaces that refer to this tuple
		 * via foreign key constraint.
		 */
		struct space_cache_holder *h;
		rlist_foreach_entry(h, &space->space_cache_pin_list, link) {
			if (h->type != SPACE_HOLDER_FOREIGN_KEY)
				continue;
			struct tuple_constraint *constr =
				container_of(h, struct tuple_constraint,
					     space_cache_holder);
			assert(constr->def.type == CONSTR_FKEY);
			if (tuple_constraint_fkey_check_delete(constr,
							       old_tuple,
							       new_tuple) != 0)
				return -1;
		}
	}

	/*
	 * Execute all registered BEFORE triggers.
	 *
	 * We pass the log row, old and new tuples to the triggers
	 * in txn_current_stmt(), which should be empty, because
	 * the engine method (execute_replace or similar) has not
	 * been called yet. Triggers may update new_tuple in place
	 * so the next trigger sees the result of the previous one.
	 * After we are done, we clear row, old_tuple and new_tuple
	 * in txn_current_stmt() to be set by the engine.
	 */
	struct txn_stmt *stmt = txn_current_stmt(txn);
	assert(stmt->old_tuple == NULL && stmt->new_tuple == NULL);
	assert(stmt->row == NULL);
	stmt->old_tuple = old_tuple;
	stmt->new_tuple = new_tuple;
	stmt->row = request->header;

	struct event *events[] = {
		space->before_replace_event.by_id,
		space->before_replace_event.by_name,
	};
	/*
	 * Since the triggers can yield, a space can be dropped
	 * while executing one of the trigger lists and all the events will be
	 * unreferenced - reference them to prevent use-after-free.
	 */
	for (size_t i = 0; i < lengthof(events); ++i)
		event_ref(events[i]);
	int rc = trigger_run(&space->before_replace, txn);
	for (size_t i = 0; i < lengthof(events) && rc == 0; ++i)
		rc = space_run_replace_triggers(events[i], txn, true);
	for (size_t i = 0; i < lengthof(events); ++i)
		event_unref(events[i]);

	/*
	 * BEFORE triggers cannot change the old tuple,
	 * but they may replace the new tuple.
	 */
	bool request_changed = (stmt->new_tuple != new_tuple);
	new_tuple = stmt->new_tuple;
	assert(stmt->old_tuple == old_tuple);
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
	stmt->row = NULL;

	if (rc != 0)
		goto out;

	/*
	 * We don't allow to change the value of the primary key
	 * in the same statement.
	 */
	if (pk != NULL && request_changed &&
	    old_tuple != NULL && new_tuple != NULL &&
	    tuple_compare(old_tuple, HINT_NONE, new_tuple, HINT_NONE,
			  pk->def->key_def) != 0) {
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 space->def->name);
		rc = -1;
		goto out;
	}

	/*
	 * BEFORE triggers changed the resulting tuple.
	 * Fix the request to conform.
	 */
	if (request_changed) {
		new_data = new_tuple == NULL ? NULL :
			   tuple_data_range(new_tuple, &new_size);
		struct region *txn_region = tx_region_acquire(txn);
		rc = request_create_from_tuple(request, space,
					       old_data, old_size,
					       new_data, new_size,
					       txn_region, false);
		tx_region_release(txn, TX_ALLOC_SYSTEM);
	}
out:
	if (new_tuple != NULL)
		tuple_unref(new_tuple);
	return rc;
}

int
space_execute_dml(struct space *space, struct txn *txn,
		  struct request *request, struct tuple **result)
{
	if (unlikely(space->sequence != NULL) &&
	    (request->type == IPROTO_INSERT ||
	     request->type == IPROTO_REPLACE)) {
		/*
		 * The space has a sequence associated with it.
		 * If the tuple has 'nil' for the primary key,
		 * we should replace it with the next sequence
		 * value.
		 */
		struct region *txn_region = tx_region_acquire(txn);
		int rc = request_handle_sequence(request, space, txn_region);
		tx_region_release(txn, TX_ALLOC_SYSTEM);
		if (rc != 0)
			return -1;
	}

	/* Any operation except insert can remove old tuple. */
	bool need_foreign_key_check = false;
	if (request->type != IPROTO_INSERT) {
		struct space_cache_holder *h;
		rlist_foreach_entry(h, &space->space_cache_pin_list, link) {
			if (h->type == SPACE_HOLDER_FOREIGN_KEY) {
				need_foreign_key_check = true;
				break;
			}
		}
	}

	bool need_defaults_apply = tuple_format_has_defaults(space->format) &&
				   recovery_state == FINISHED_RECOVERY &&
				   request->type != IPROTO_UPDATE &&
				   request->type != IPROTO_DELETE;
	if (unlikely(need_defaults_apply)) {
		if (space_apply_defaults(space, txn, request) != 0)
			return -1;
	}

	if (unlikely((space_has_before_replace_triggers(space) &&
		      space->run_triggers) || need_foreign_key_check)) {
		/*
		 * Call BEFORE triggers if any before dispatching
		 * the request. Note, it may change the request
		 * type and arguments.
		 */
		if (space_before_replace(space, txn, request) != 0)
			return -1;
	}

	switch (request->type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		if (space->vtab->execute_replace(space, txn,
						 request, result) != 0)
			return -1;
		break;
	case IPROTO_UPDATE:
		if (space->vtab->execute_update(space, txn,
						request, result) != 0)
			return -1;
		if (*result != NULL && request->index_id != 0) {
			/*
			 * XXX: this is going to break with sync replication
			 * for cases when tuple is NULL, since the leader
			 * will be unable to certify such updates correctly.
			 */
			struct region *txn_region = tx_region_acquire(txn);
			request_rebind_to_primary_key(request, space, *result,
						      txn_region);
			tx_region_release(txn, TX_ALLOC_SYSTEM);
		}
		break;
	case IPROTO_DELETE:
		if (space->vtab->execute_delete(space, txn,
						request, result) != 0)
			return -1;
		if (*result != NULL && request->index_id != 0) {
			struct region *txn_region = tx_region_acquire(txn);
			request_rebind_to_primary_key(request, space, *result,
						      txn_region);
			tx_region_release(txn, TX_ALLOC_SYSTEM);
		}
		break;
	case IPROTO_UPSERT:
		*result = NULL;
		if (space->vtab->execute_upsert(space, txn, request) != 0)
			return -1;
		break;
	default:
		*result = NULL;
	}
	return 0;
}

/* {{{ Virtual method stubs */

size_t
generic_space_bsize(struct space *space)
{
	(void)space;
	return 0;
}

int
generic_space_ephemeral_replace(struct space *space, const char *tuple,
				const char *tuple_end)
{
	(void)space;
	(void)tuple;
	(void)tuple_end;
	unreachable();
	return -1;
}

int
generic_space_ephemeral_delete(struct space *space, const char *key)
{
	(void)space;
	(void)key;
	unreachable();
	return -1;
}

int
generic_space_ephemeral_rowid_next(struct space *space, uint64_t *rowid)
{
	(void)space;
	(void)rowid;
	unreachable();
	return 0;
}

void
generic_init_system_space(struct space *space)
{
	(void)space;
}

void
generic_init_ephemeral_space(struct space *space)
{
	(void)space;
}

int
generic_space_check_index_def(struct space *space, struct index_def *index_def)
{
	(void)space;
	(void)index_def;
	return 0;
}

int
generic_space_add_primary_key(struct space *space)
{
	(void)space;
	return 0;
}

void
generic_space_drop_primary_key(struct space *space)
{
	(void)space;
}

int
generic_space_check_format(struct space *space, struct tuple_format *format)
{
	(void)space;
	(void)format;
	return 0;
}

int
generic_space_build_index(struct space *src_space, struct index *new_index,
			  struct tuple_format *new_format,
			  bool check_unique_constraint)
{
	(void)src_space;
	(void)new_index;
	(void)new_format;
	(void)check_unique_constraint;
	return 0;
}

int
generic_space_prepare_alter(struct space *old_space, struct space *new_space)
{
	(void)old_space;
	(void)new_space;
	return 0;
}

void
generic_space_finish_alter(struct space *old_space, struct space *new_space)
{
	(void)old_space;
	(void)new_space;
}

int
generic_space_prepare_upgrade(struct space *old_space, struct space *new_space)
{
	(void)old_space;
	if (new_space->def->opts.upgrade_def != NULL) {
		diag_set(ClientError, ER_UNSUPPORTED, new_space->engine->name,
			 "space upgrade");
		return -1;
	}
	return 0;
}

void
generic_space_invalidate(struct space *space)
{
	(void)space;
}

/* }}} */
