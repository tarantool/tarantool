/*
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
#include "alter.h"
#include "schema.h"
#include "space.h"
#include "txn.h"
#include "tuple.h"
#include "fiber.h" /* for gc_pool */
#include "scoped_guard.h"
#include <new> /* for placement new */
#include <stdio.h> /* snprintf() */
#include <ctype.h>

/** _space columns */
#define ID			0
#define ARITY			1
#define NAME			2
#define FLAGS			3
/** _index columns */
#define INDEX_ID		1
#define INDEX_TYPE		3
#define INDEX_IS_UNIQUE		4
#define INDEX_PART_COUNT	5

/* {{{ Auxiliary functions and methods. */

/**
 * Create a key_def object from a record in _index
 * system space.
 *
 * Check that:
 * - index id is within range
 * - index type is supported
 * - part count > 0
 * - there are parts for the specified part count
 * - types of parts in the parts array are known to the system
 * - fieldno of each part in the parts array is within limits
 */
struct key_def *
key_def_new_from_tuple(struct tuple *tuple)
{
	uint32_t id = tuple_field_u32(tuple, ID);
	uint32_t index_id = tuple_field_u32(tuple, INDEX_ID);
	const char *type_str = tuple_field_cstr(tuple, INDEX_TYPE);
	enum index_type type = STR2ENUM(index_type, type_str);
	uint32_t is_unique = tuple_field_u32(tuple, INDEX_IS_UNIQUE);
	uint32_t part_count = tuple_field_u32(tuple, INDEX_PART_COUNT);
	const char *name = tuple_field_cstr(tuple, NAME);

	struct key_def *key_def = key_def_new(id, index_id, name, type,
					      is_unique > 0, part_count);
	auto scoped_guard =
		make_scoped_guard([=] { key_def_delete(key_def); });

	struct tuple_iterator it;
	tuple_rewind(&it, tuple);
	uint32_t len; /* unused */
	/* Parts follow part count. */
	(void) tuple_seek(&it, INDEX_PART_COUNT, &len);

	for (uint32_t i = 0; i < part_count; i++) {
		uint32_t fieldno = tuple_next_u32(&it);
		const char *field_type_str = tuple_next_cstr(&it);
		enum field_type field_type =
			STR2ENUM(field_type, field_type_str);
		key_def_set_part(key_def, i, fieldno, field_type);
	}
	key_def_check(key_def);
	scoped_guard.is_active = false;
	return key_def;
}

static void
space_def_init_flags(struct space_def *def, struct tuple *tuple)
{
	/* default values of flags */
	def->temporary = false;

	/* there is no property in the space */
	if (tuple->field_count <= FLAGS)
		return;

	const char *flags = tuple_field_cstr(tuple, FLAGS);
	while (flags && *flags) {
		while (isspace(*flags)) /* skip space */
			flags++;
		if (strncmp(flags, "temporary", strlen("temporary")) == 0)
			def->temporary = true;
		flags = strchr(flags, ',');
		if (flags)
			flags++;
	}
}

/**
 * Fill space_def structure from struct tuple.
 */
void
space_def_create_from_tuple(struct space_def *def, struct tuple *tuple,
			    uint32_t errcode)
{
	def->id = tuple_field_u32(tuple, ID);
	def->arity = tuple_field_u32(tuple, ARITY);
	int n = snprintf(def->name, sizeof(def->name),
			 "%s", tuple_field_cstr(tuple, NAME));

	space_def_init_flags(def, tuple);
	space_def_check(def, n, errcode);
	if (errcode != ER_ALTER_SPACE &&
	    def->id >= SC_SYSTEM_ID_MIN && def->id < SC_SYSTEM_ID_MAX) {
		say_warn("\n"
"*******************************************************\n"
"* Creating a space with a reserved id %3u.            *\n"
"* Ids in range %3u-%3u may be used for a system space *\n"
"* the future. Assuming you know what you're doing.    *\n"
"*******************************************************",
			 (unsigned) def->id,
			 (unsigned) SC_SYSTEM_ID_MIN,
			 (unsigned) SC_SYSTEM_ID_MAX);
	}
}

/* }}} */

/* {{{ struct alter_space - the body of a full blown alter */
struct alter_space;

class AlterSpaceOp {
public:
	struct rlist link;
	virtual void prepare(struct alter_space * /* alter */) {}
	virtual void alter_def(struct alter_space * /* alter */) {}
	virtual void alter(struct alter_space * /* alter */) {}
	virtual void commit(struct alter_space * /* alter */) {}
	virtual void rollback(struct alter_space * /* alter */) {}
	virtual ~AlterSpaceOp() {}
	template <typename T> static T *create();
	static void destroy(AlterSpaceOp *op);
};

template <typename T> T *
AlterSpaceOp::create()
{
	return new (p0alloc(fiber->gc_pool, sizeof(T))) T;
}

void
AlterSpaceOp::destroy(AlterSpaceOp *op)
{
	op->~AlterSpaceOp();
}

/**
 * A trigger installed on transaction commit/rollback events of
 * the transaction which initiated the alter.
 */
struct txn_alter_trigger {
	struct trigger trigger;
	struct alter_space *alter;
};

struct txn_alter_trigger *
txn_alter_trigger(struct trigger *trigger)
{
	return (struct txn_alter_trigger *) trigger;
}

struct txn_alter_trigger *
txn_alter_trigger_new(trigger_f run, struct alter_space *alter)
{
	struct txn_alter_trigger *trigger = (struct txn_alter_trigger *)
		p0alloc(fiber->gc_pool, sizeof(*trigger));
	trigger->trigger.run = run;
	trigger->alter = alter;
	return trigger;
}

struct alter_space {
	/** List of alter operations */
	struct rlist ops;
	/** Definition of the new space - space_def. */
	struct space_def space_def;
	/** Definition of the new space - keys. */
	struct rlist key_list;
	/** Old space. */
	struct space *old_space;
	/** New space. */
	struct space *new_space;
};

struct alter_space *
alter_space_new()
{
	struct alter_space *alter = (struct alter_space *)
		p0alloc(fiber->gc_pool, sizeof(*alter));
	rlist_create(&alter->ops);
	return alter;
}

/** Destroy alter. */
static void
alter_space_delete(struct alter_space *alter)
{
	/* Destroy the ops. */
	while (! rlist_empty(&alter->ops)) {
		AlterSpaceOp *op = rlist_shift_entry(&alter->ops,
						     AlterSpaceOp, link);
		AlterSpaceOp::destroy(op);
	}
	/* Delete the new space, if any. */
	if (alter->new_space)
		space_delete(alter->new_space);
}

/** Add a single operation to the list of alter operations. */
static void
alter_space_add_op(struct alter_space *alter, AlterSpaceOp *op)
{
	/* Add to the tail: operations must be processed in order. */
	rlist_add_tail_entry(&alter->ops, op, link);
}

/**
 * Commit the alter.
 *
 * Move all unchanged indexes from the old space to the new space.
 * Set the newly built indexes in the new space, or free memory
 * of the dropped indexes.
 * Replace the old space with a new one in the space cache.
 */
static void
alter_space_commit(struct trigger *trigger, void * /* event */)
{
	struct alter_space *alter = txn_alter_trigger(trigger)->alter;
#if 0
	/*
	 * Clear the lock first - should there be an
	 * exception/bug, the lock must not be left around.
	 */
	space_unset_on_replace(space, alter);
#endif
	/*
	 * If an index is unchanged, all its properties, including
	 * ID are intact. Move this index here. If an index is
	 * changed, even if this is a minor change, there is a
	 * ModifyIndex instance which will move the index from an
	 * old position to the new one.
	 */
	for (uint32_t i = 0; i < alter->new_space->index_count; i++) {
		Index *new_index = alter->new_space->index[i];
		Index *old_index = space_index(alter->old_space,
					       index_id(new_index));
		/*
		 * Move unchanged index from the old space to the
		 * new one.
		 */
		if (old_index != NULL &&
		    key_def_cmp(new_index->key_def,
				old_index->key_def) == 0) {
			space_swap_index(alter->old_space,
					 alter->new_space,
					 index_id(old_index),
					 index_id(new_index),
					 false);
		}
	}
	/*
	 * Commit alter ops, this will move the changed
	 * indexes into their new places.
	 */
	struct AlterSpaceOp *op;
	rlist_foreach_entry(op, &alter->ops, link) {
		op->commit(alter);
	}
	/* Rebuild index maps once for all indexes. */
	space_fill_index_map(alter->old_space);
	space_fill_index_map(alter->new_space);
	/*
	 * Don't forget about space triggers.
	 */
	rlist_swap(&alter->new_space->on_replace,
		   &alter->old_space->on_replace);
	/*
	 * The new space is ready. Time to update the space
	 * cache with it.
	 */
	struct space *old_space = space_cache_replace(alter->new_space);
	assert(old_space == alter->old_space);
	space_delete(old_space);
	alter->new_space = NULL; /* for alter_space_delete(). */
	alter_space_delete(alter);
}

/**
 * Rollback all effects of space alter. This is
 * a transaction trigger, and it fires most likely
 * upon a failed write to the WAL.
 *
 * Keep in mind that we may end up here in case of
 * alter_space_commit() failure (unlikely)
 */
static void
alter_space_rollback(struct trigger *trigger, void * /* event */)
{
	struct alter_space *alter = txn_alter_trigger(trigger)->alter;
#if 0
	/* Clear the lock, first thing. */
		op->rollback(alter);
	space_remove_trigger(alter);
#endif
	struct AlterSpaceOp *op;
	rlist_foreach_entry(op, &alter->ops, link)
		op->rollback(alter);
	alter_space_delete(alter);
}

/**
 * alter_space_do() - do all the work necessary to
 * create a new space.
 *
 * If something may fail during alter, it must be done here,
 * before a record is written to the Write Ahead Log.  Only
 * trivial and infallible actions are left to the commit phase
 * of the alter.
 *
 * The implementation of this function follows "Template Method"
 * pattern, providing a skeleton of the alter, while all the
 * details are encapsulated in AlterSpaceOp methods.
 *
 * These are the major steps of alter defining the structure of
 * the algorithm and performed regardless of what is altered:
 *
 * - the input is checked for validity; each check is
 *   encapsulated in AlterSpaceOp::prepare() method.
 * - a copy of the definition of the old space is created
 * - the definition of the old space is altered, to get
 *   definition of a new space
 * - an instance of the new space is created, according to the new
 *   definition; the space is so far empty
 * - data structures of the new space are built; sometimes, it
 *   doesn't need to happen, e.g. when alter only changes the name
 *   of a space or an index, or other accidental property.
 *   If any data structure needs to be built, e.g. a new index,
 *   only this index is built, not the entire space with all its
 *   indexes.
 * - at commit, the new space is coalesced with the old one.
 *   On rollback, the new space is deleted.
 */
static void
alter_space_do(struct txn *txn, struct alter_space *alter,
	       struct space *old_space)
{
#if 0
	/*
	 * Mark the space as being altered, to abort
	 * concurrent alter operations from while this
	 * alter is being written to the write ahead log.
	 * Must be the last, to make sure we reach commit and
	 * remove it. It's removed only in comit/rollback.
	 *
	 * @todo This is, essentially, an implicit pessimistic
	 * metadata lock on the space (ugly!), and it should be
	 * replaced with an explicit lock, since there is nothing
	 * worse than having to retry your alter -- usually alter
	 * is done in a script without error-checking.
	 * Plus, implicit locks are evil.
	 */
	if (space->on_replace == space_alter_on_replace)
		tnt_raise(ER_ALTER, space_id(space));
#endif
	alter->old_space = old_space;
	alter->space_def = old_space->def;
	/* Create a definition of the new space. */
	space_dump_def(old_space, &alter->key_list);
	/*
	 * Allow for a separate prepare step so that some ops
	 * can be optimized.
	 */
	struct AlterSpaceOp *op, *tmp;
	rlist_foreach_entry_safe(op, &alter->ops, link, tmp)
		op->prepare(alter);
	/*
	 * Alter the definition of the old space, so that
	 * a new space can be created with a new definition.
	 */
	rlist_foreach_entry(op, &alter->ops, link)
		op->alter_def(alter);
	/*
	 * Create a new (empty) space for the new definition.
	 * Sic: the space engine is not the same yet, the
	 * triggers are not set.
         */
	alter->new_space = space_new(&alter->space_def, &alter->key_list);
	/*
	 * Copy the engine, the new space is at the same recovery
	 * phase as the old one. Do it before performing the alter,
	 * since engine.recover does different things depending on
	 * the recovery phase.
	 */
	alter->new_space->engine = alter->old_space->engine;
	/*
	 * Change the new space: build the new index, rename,
	 * change arity.
	 */
	rlist_foreach_entry(op, &alter->ops, link)
		op->alter(alter);
	/*
	 * Install transaction commit/rollback triggers to either
	 * finish or rollback the DDL depending on the results of
	 * writing to WAL.
	 */
	struct txn_alter_trigger *on_commit =
		txn_alter_trigger_new(alter_space_commit, alter);
	trigger_set(&txn->on_commit, &on_commit->trigger);
	struct txn_alter_trigger *on_rollback =
		txn_alter_trigger_new(alter_space_rollback, alter);
	trigger_set(&txn->on_rollback, &on_rollback->trigger);
}

/* }}}  */

/* {{{ AlterSpaceOp descendants - alter operations, such as Add/Drop index */

/** Change non-essential properties of a space. */
class ModifySpace: public AlterSpaceOp
{
public:
	/* New space definition. */
	struct space_def def;
	virtual void prepare(struct alter_space *alter);
	virtual void alter_def(struct alter_space *alter);
};

/** Check that space properties are OK to change. */
void
ModifySpace::prepare(struct alter_space *alter)
{
	if (def.id != space_id(alter->old_space))
		tnt_raise(ClientError, ER_ALTER_SPACE,
			  (unsigned) space_id(alter->old_space),
			  "space id is immutable");

	if (def.arity != 0 &&
	    def.arity != alter->old_space->def.arity &&
	    alter->old_space->engine.state != READY_NO_KEYS &&
	    space_size(alter->old_space) > 0) {

		tnt_raise(ClientError, ER_ALTER_SPACE,
			  (unsigned) def.id,
			  "can not change arity on a non-empty space");
	}
	if (def.temporary != alter->old_space->def.temporary &&
	    alter->old_space->engine.state != READY_NO_KEYS &&
	    space_size(alter->old_space) > 0) {
		tnt_raise(ClientError, ER_ALTER_SPACE,
			  (unsigned) space_id(alter->old_space),
			  "can not switch temporary flag on a non-empty space");
	}
}

/** Amend the definition of the new space. */
void
ModifySpace::alter_def(struct alter_space *alter)
{
	alter->space_def = def;
}

/** DropIndex - remove an index from space. */

class AddIndex;

class DropIndex: public AlterSpaceOp {
public:
	/** A reference to Index key def of the dropped index. */
	struct key_def *old_key_def;
	virtual void alter_def(struct alter_space *alter);
	virtual void alter(struct alter_space *alter);
	virtual void commit(struct alter_space *alter);
};

/*
 * Alter the definition of the new space and remove
 * the new index from it.
 */
void
DropIndex::alter_def(struct alter_space * /* alter */)
{
	rlist_del_entry(old_key_def, link);
}

/* Do the drop. */
void
DropIndex::alter(struct alter_space *alter)
{
	/*
	 * If it's not the primary key, nothing to do --
	 * the dropped index didn't exist in the new space
	 * definition, so does not exist in the created space.
	 */
	if (space_index(alter->new_space, 0) != NULL)
		return;
	/*
	 * Deal with various cases of dropping of the primary key.
	 */
	/*
	 * Dropping the primary key in a system space: off limits.
	 */
	if (space_is_system(alter->new_space))
		tnt_raise(ClientError, ER_LAST_DROP,
			  space_id(alter->new_space));
	/*
	 * Can't drop primary key before secondary keys.
	 */
	if (alter->new_space->index_count) {
		tnt_raise(ClientError, ER_DROP_PRIMARY_KEY,
			  (unsigned) alter->new_space->def.id);
	}
	/*
	 * OK to drop the primary key. Put the space back to
	 * 'READY_NO_KEYS' state, so that:
	 * - DML returns proper errors rather than crashes the
	 *   server (thanks to engine_no_keys.replace),
	 * - When a new primary key is finally added, the space
	 *   can be put back online properly with
	 *   engine_no_keys.recover.
	 */
	alter->new_space->engine = engine_no_keys;
}

void
DropIndex::commit(struct alter_space *alter)
{
	/*
	 * Delete all tuples in the old space if dropping the
	 * primary key.
	 */
	if (space_index(alter->new_space, 0) != NULL)
		return;
	Index *pk = index_find(alter->old_space, 0);
	if (pk == NULL)
		return;
	struct iterator *it = pk->position();
	pk->initIterator(it, ITER_ALL, NULL, 0);
	struct tuple *tuple;
	while ((tuple = it->next(it)))
		tuple_ref(tuple, -1);
}

/** Change non-essential (no data change) properties of an index. */
class ModifyIndex: public AlterSpaceOp
{
public:
	struct key_def *new_key_def;
	struct key_def *old_key_def;
	virtual void alter_def(struct alter_space *alter);
	virtual void commit(struct alter_space *alter);
	virtual ~ModifyIndex();
};

/** Update the definition of the new space */
void
ModifyIndex::alter_def(struct alter_space *alter)
{
	rlist_del_entry(old_key_def, link);
	rlist_add_entry(&alter->key_list, new_key_def, link);
}

/** Move the index from the old space to the new one. */
void
ModifyIndex::commit(struct alter_space *alter)
{
	/* Move the old index to the new place but preserve */
	space_swap_index(alter->old_space, alter->new_space,
			 old_key_def->iid, new_key_def->iid, true);
}

ModifyIndex::~ModifyIndex()
{
	/* new_key_def is NULL if exception is raised before it's set. */
	if (new_key_def)
		key_def_delete(new_key_def);
}

/**
 * Add to index trigger -- invoked on any change in the old space,
 * while the AddIndex tuple is being written to the WAL. The job
 * of this trigger is to keep the added index up to date with the
 * state of the primary key in the old space.
 *
 * Initially it's installed as old_space->on_replace trigger, and
 * for each successfully replaced tuple in the new index,
 * a trigger is added to txn->on_rollback list to remove the tuple
 * from the new index if the transaction rolls back.
 *
 * The trigger is removed when alter operation commits/rolls back.
 */
struct add2index_trigger {
	struct trigger trigger;
	Index *new_index;
};

struct add2index_trigger *
add2index_trigger(struct trigger *trigger)
{
	return (struct add2index_trigger *) trigger;
}

struct add2index_trigger *
add2index_trigger_new(trigger_f run, Index *new_index)
{
	struct add2index_trigger *trigger = (struct add2index_trigger *)
		p0alloc(fiber->gc_pool, sizeof(*trigger));
	trigger->trigger.run = run;
	trigger->new_index = new_index;
	return trigger;
}

/** AddIndex - add a new index to the space. */
class AddIndex: public AlterSpaceOp {
public:
	/** New index key_def. */
	struct key_def *new_key_def;
	struct add2index_trigger *on_replace;
	virtual void prepare(struct alter_space *alter);
	virtual void alter_def(struct alter_space *alter);
	virtual void alter(struct alter_space *alter);
	virtual ~AddIndex();
};

/**
 * Optimize addition of a new index: try to either completely
 * remove it or at least avoid building from scratch.
 */
void
AddIndex::prepare(struct alter_space *alter)
{
	AlterSpaceOp *prev_op = rlist_prev_entry_safe(this, &alter->ops,
						      link);
	DropIndex *drop = dynamic_cast<DropIndex *>(prev_op);

	if (drop == NULL ||
	    drop->old_key_def->type != new_key_def->type ||
	    drop->old_key_def->is_unique != new_key_def->is_unique ||
	    key_part_cmp(drop->old_key_def->parts,
			 drop->old_key_def->part_count,
			 new_key_def->parts,
			 new_key_def->part_count) != 0) {
		/*
		 * The new index is too distinct from the old one,
		 * have to rebuild.
		 */
		return;
	}
	/* Only index meta has changed, no data change. */
	rlist_del_entry(drop, link);
	rlist_del_entry(this, link);
	/* Add ModifyIndex only if the there is a change. */
	if (key_def_cmp(drop->old_key_def, new_key_def) != 0) {
		ModifyIndex *modify = AlterSpaceOp::create<ModifyIndex>();
		alter_space_add_op(alter, modify);
		modify->new_key_def = new_key_def;
		new_key_def = NULL;
		modify->old_key_def = drop->old_key_def;
	}
	AlterSpaceOp::destroy(drop);
	AlterSpaceOp::destroy(this);
}

/** Add definition of the new key to the new space def. */
void
AddIndex::alter_def(struct alter_space *alter)
{
	rlist_add_tail_entry(&alter->key_list, new_key_def, link);
}

/**
 * A trigger invoked on rollback in old space while the record
 * about alter is being written to the WAL.
 */
static void
on_rollback_in_old_space(struct trigger *trigger, void *event)
{
	struct txn *txn = (struct txn *) event;
	Index *new_index = add2index_trigger(trigger)->new_index;
	/* Remove the failed tuple from the new index. */
	new_index->replace(txn->new_tuple, txn->old_tuple, DUP_INSERT);
}

/**
 * A trigger invoked on replace in old space while
 * the record about alter is being written to the WAL.
 */
static void
on_replace_in_old_space(struct trigger *trigger, void *event)
{
	struct txn *txn = (struct txn *) event;
	Index *new_index = add2index_trigger(trigger)->new_index;
	/*
	 * First set rollback trigger, then do replace, since
	 * creating the trigger may fail.
	 */
	struct add2index_trigger *on_rollback =
		add2index_trigger_new(on_rollback_in_old_space, new_index);
	trigger_set(&txn->on_rollback, &on_rollback->trigger);
	/* Put the tuple into thew new index. */
	(void) new_index->replace(txn->old_tuple, txn->new_tuple,
				  DUP_INSERT);
}

/**
 * Optionally build the new index.
 *
 * During recovery the space is often not fully constructed yet
 * anyway, so there is no need to fully populate index with data,
 * it is done at the end of recovery.
 *
 * Note, that system  spaces are exception to this, since
 * they are fully enabled at all times.
 */
void
AddIndex::alter(struct alter_space *alter)
{
	/*
	 * READY_NO_KEYS is when a space has no functional keys.
	 * Possible both during and after recovery.
	 */
	if (alter->new_space->engine.state == READY_NO_KEYS) {
		if (new_key_def->iid == 0) {
			/*
			 * Adding a primary key: bring the space
			 * up to speed with the current recovery
			 * state. During snapshot recovery it
			 * means preparing the primary key for
			 * build (beginBuild()). During xlog
			 * recovery, it means building the primary
			 * key. After recovery, it means building
			 * all keys.
			 */
			alter->new_space->engine.recover(alter->new_space);
		} else {
			/*
			 * Adding a secondary key: nothing to do.
			 * Before the end of recovery, nothing to do
			 * because secondary keys are built in bulk later.
			 * During normal operation, nothing to do
			 * because without a primary key there is
			 * no data in the space, and secondary
			 * keys are built once the primary is
			 * added.
			 * TODO Consider prohibiting this branch
			 * altogether.
			 */
		}
		return;
	}
	Index *pk = index_find(alter->old_space, 0);
	Index *new_index = index_find(alter->new_space, new_key_def->iid);
	/* READY_PRIMARY_KEY is a state that only occurs during WAL recovery. */
	if (alter->new_space->engine.state == READY_PRIMARY_KEY) {
		if (new_key_def->iid == 0) {
			/*
			 * Bulk rebuild of the new primary key
			 * from old primary key - it is safe to do
			 * in bulk and without tuple-by-tuple
			 * verification, since all tuples have
			 * been verified when inserted, before
			 * shutdown.
			 */
			index_build(new_index, pk);
		} else {
			/*
			 * No need to build a secondary key during
			 * WAL recovery.
			 */
		}
		return;
	}
	/* Now deal with any kind of add index during normal operation. */
	struct iterator *it = pk->position();
	pk->initIterator(it, ITER_ALL, NULL, 0);
	/*
	 * The index has to be built tuple by tuple, since
	 * there is no guarantee that all tuples satisfy
	 * new index' constraints. If any tuple can not be
	 * added to the index (insufficient number of fields,
	 * etc., the build is aborted.
	 */
	new_index->beginBuild();
	new_index->endBuild();
	/* Build the new index. */
	struct tuple *tuple;
	struct tuple_format *format = alter->new_space->format;
	char *field_map = ((char *) palloc(fiber->gc_pool,
					   format->field_map_size) +
			   format->field_map_size);
	while ((tuple = it->next(it))) {
		/*
		 * Check that the tuple is OK according to the
		 * new format.
		 */
		tuple_init_field_map(format, tuple, (uint32_t *) field_map);
		/*
		 * @todo: better message if there is a duplicate.
		 */
		struct tuple *old_tuple =
			new_index->replace(NULL, tuple, DUP_INSERT);
		assert(old_tuple == NULL); /* Guaranteed by DUP_INSERT. */
		(void) old_tuple;
	}
	on_replace = add2index_trigger_new(on_replace_in_old_space,
					   new_index);
	trigger_set(&alter->old_space->on_replace, &on_replace->trigger);
}

AddIndex::~AddIndex()
{
	/*
	 * The trigger by now may reside in the new space (on
	 * commit) or in the old space (rollback). Remove it
	 * from the list, wherever it is.
	 */
	if (on_replace)
		trigger_clear(&on_replace->trigger);
	if (new_key_def)
		key_def_delete(new_key_def);
}

/* }}} */

/**
 * A trigger invoked on commit/rollback of DROP/ADD space.
 * The trigger removed the space from the space cache.
 */
static void
on_drop_space(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	uint32_t id = tuple_field_u32(txn->old_tuple ?
				      txn->old_tuple : txn->new_tuple, ID);
	struct space *space = space_cache_delete(id);
	space_delete(space);
}

static struct trigger drop_space_trigger =  { rlist_nil, on_drop_space };

/**
 * A trigger which is invoked on replace in a data dictionary
 * space _space.
 *
 * Generally, whenever a data dictionary change occurs
 * 2 things should be done:
 *
 * - space cache should be updated, and changes in the space
 *   cache should be reflected in Lua bindings
 *   (this is done in space_cache_replace() and
 *   space_cache_delete())
 *
 * - the space which is changed should be rebuilt according
 *   to the nature of the modification, i.e. indexes added/dropped,
 *   tuple format changed, etc.
 *
 * When dealing with an update of _space space, we have 3 major
 * cases:
 *
 * 1) insert a new tuple: creates a new space
 *    The trigger prepares a space structure to insert
 *    into the  space cache and registers an on commit
 *    hook to perform the registration. Should the statement
 *    itself fail, transaction is rolled back, the transaction
 *    rollback hook must be there to delete the created space
 *    object, avoiding a memory leak. The hooks are written
 *    in a way that excludes the possibility of a failure.
 *
 * 2) delete a tuple: drops an existing space.
 *
 *    A space can be dropped only if it has no indexes.
 *    The only reason for this restriction is that there
 *    must be no tuples in _index without a corresponding tuple
 *    in _space. It's not possible to delete such tuples
 *    automatically (this would require multi-statement
 *    transactions), so instead the trigger verifies that the
 *    records have been deleted by the user.
 *
 *    Then the trigger registers transaction commit hook to
 *    perform the deletion from the space cache.  No rollback hook
 *    is required: if the transaction is rolled back, nothing is
 *    done.
 *
 * 3) modify an existing tuple: some space
 *    properties are immutable, but it's OK to change
 *    space name or arity. This is done in WAL-error-
 *    safe mode.
 *
 * A note about memcached_space: Tarantool 1.4 had a check
 * which prevented re-definition of memcached_space. With
 * dynamic space configuration such a check would be particularly
 * clumsy, so it is simply not done.
 */
static void
on_replace_dd_space(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct tuple *old_tuple = txn->old_tuple;
	struct tuple *new_tuple = txn->new_tuple;
	/*
	 * Things to keep in mind:
	 * - old_tuple is set only in case of UPDATE.  For INSERT
	 *   or REPLACE it is NULL.
	 * - the trigger may be called inside recovery from a snapshot,
	 *   when index look up is not possible
	 * - _space, _index and other metaspaces initially don't
	 *   have a tuple which represents it, this tuple is only
	 *   created during recovery from
	 *   a snapshot.
	 *
	 * Let's establish whether an old space exists. Use
	 * old_tuple ID field, if old_tuple is set, since UPDATE
	 * may have changed space id.
	 */
	uint32_t old_id = tuple_field_u32(old_tuple ?
					  old_tuple : new_tuple, ID);
	struct space *old_space = space_by_id(old_id);
	if (new_tuple != NULL && old_space == NULL) { /* INSERT */
		struct space_def def;
		space_def_create_from_tuple(&def, new_tuple, ER_CREATE_SPACE);
		struct space *space = space_new(&def, &rlist_nil);
		(void) space_cache_replace(space);
		/*
		 * So may happen that until the DDL change record
		 * is written to the WAL, the space is used for
		 * insert/update/delete. All these updates are
		 * rolled back by the pipelined rollback mechanism,
		 * so it's safe to simply drop the space on
		 * rollback.
		 */
		trigger_set(&txn->on_rollback, &drop_space_trigger);
	} else if (new_tuple == NULL) { /* DELETE */
		/* Verify that the space is empty (has no indexes) */
		if (old_space->index_count) {
			tnt_raise(ClientError, ER_DROP_SPACE,
				  (unsigned) space_id(old_space),
				  "the space has indexes");
		}
		/* @todo lock space metadata until commit. */
		/*
		 * dd_space_delete() can't fail, any such
		 * failure would have to abort the server.
		 */
		trigger_set(&txn->on_commit, &drop_space_trigger);
	} else { /* UPDATE, REPLACE */
		assert(old_space != NULL && new_tuple != NULL);
		/*
		 * Allow change of space properties, but do it
		 * in WAL-error-safe mode.
		 */
		struct alter_space *alter = alter_space_new();
		auto scoped_guard =
		        make_scoped_guard([=] {alter_space_delete(alter);});
		ModifySpace *modify =
			AlterSpaceOp::create<ModifySpace>();
		alter_space_add_op(alter, modify);
		space_def_create_from_tuple(&modify->def, new_tuple,
					    ER_ALTER_SPACE);
		alter_space_do(txn, alter, old_space);
		scoped_guard.is_active = false;
	}
}

/**
 * Just like with _space, 3 major cases:
 *
 * - insert a tuple = addition of a new index. The
 *   space should exist.
 *
 * - delete a tuple - drop index.
 *
 * - update a tuple - change of index type or key parts.
 *   Change of index type is the same as deletion of the old
 *   index and addition of the new one.
 *
 *   A new index needs to be built before we attempt to commit
 *   a record to the write ahead log, since:
 *
 *   1) if it fails, it's not good to end up with a corrupt index
 *   which is already committed to WAL
 *
 *   2) Tarantool indexes also work as constraints (min number of
 *   fields in the space, field uniqueness), and it's not good to
 *   commit to WAL a constraint which is not enforced in the
 *   current data set.
 *
 *   When adding a new index, ideally we'd also need to rebuild
 *   all tuple formats in all tuples, since the old format may not
 *   be ideal for the new index. We, however, do not do that,
 *   since that would entail rebuilding all indexes at once.
 *   Instead, the default tuple format of the space is changed,
 *   and as tuples get updated/replaced, all tuples acquire a new
 *   format.
 *
 *   The same is the case with dropping an index: nothing is
 *   rebuilt right away, but gradually the extra space reserved
 *   for offsets is relinquished to the slab allocator as tuples
 *   are modified.
 */
static void
on_replace_dd_index(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct tuple *old_tuple = txn->old_tuple;
	struct tuple *new_tuple = txn->new_tuple;
	uint32_t id = tuple_field_u32(old_tuple ? old_tuple : new_tuple, ID);
	uint32_t iid = tuple_field_u32(old_tuple ? old_tuple : new_tuple,
				       INDEX_ID);
	struct space *old_space = space_find(id);
	Index *old_index = space_index(old_space, iid);
	struct alter_space *alter = alter_space_new();
	auto scoped_guard =
		make_scoped_guard([=] { alter_space_delete(alter); });
	/*
	 * The order of checks is important, DropIndex most be added
	 * first, so that AddIndex::prepare() can change
	 * Drop + Add to a Modify.
	 */
	if (old_index != NULL) {
		DropIndex *drop_index = AlterSpaceOp::create<DropIndex>();
		alter_space_add_op(alter, drop_index);
		drop_index->old_key_def = old_index->key_def;
	}
	if (new_tuple != NULL) {
		AddIndex *add_index = AlterSpaceOp::create<AddIndex>();
		alter_space_add_op(alter, add_index);
		add_index->new_key_def = key_def_new_from_tuple(new_tuple);
	}
	alter_space_do(txn, alter, old_space);
	scoped_guard.is_active = false;
}

struct trigger alter_space_on_replace_space = {
	rlist_nil, on_replace_dd_space
};

struct trigger alter_space_on_replace_index = {
	rlist_nil, on_replace_dd_index
};

/* vim: set foldmethod=marker */
