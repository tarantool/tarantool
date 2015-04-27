#ifndef TARANTOOL_BOX_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_ENGINE_H_INCLUDED
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
#include "index.h"

struct space;
struct tuple;
class Relay;

enum engine_flags {
	ENGINE_CAN_BE_TEMPORARY = 1,
	/**
	 * Identifies that engine can handle changes
	 * of primary key during update.
	 * During update operation user could change primary
	 * key of a tuple, which is prohibited, to avoid funny
	 * effects during replication. Some engines can
	 * track down this situation and abort the operation;
	 * such engines should set this flag.
	 * If the flag is not set, the server will verify
	 * that the primary key is not changed.
	 */
	ENGINE_AUTO_CHECK_UPDATE = 2,
};

extern struct rlist engines;

typedef void
(*engine_replace_f)(struct txn *txn, struct space *,
		    struct tuple *, struct tuple *, enum dup_replace_mode);

struct Handler;

/** Engine instance */
class Engine: public Object {
public:
	Engine(const char *engine_name);
	virtual ~Engine() {}
	/** Called once at startup. */
	virtual void init();
	/** Create a new engine instance for a space. */
	virtual Handler *open() = 0;
	virtual void initSystemSpace(struct space *space);
	/**
	 * Check a key definition for violation of
	 * various limits.
	 */
	virtual void keydefCheck(struct space *space, struct key_def*) = 0;
	/**
	 * Create an instance of space index. Used in alter
	 * space.
	 */
	virtual Index *createIndex(struct key_def*) = 0;
	/**
	 * Called by alter when a primary key added,
	 * after createIndex is invoked for the new
	 * key.
	 */
	virtual void addPrimaryKey(struct space *space);
	/**
	 * Delete all tuples in the index on drop.
	 */
	virtual void dropIndex(Index *) = 0;
	/**
	 * Called by alter when the primary key is dropped.
	 * Do whatever is necessary with space/handler object,
	 * e.g. disable handler replace function, if
	 * necessary.
	 */
	virtual void dropPrimaryKey(struct space *space);
	/**
	 * An optimization for MemTX engine, which
	 * builds all secondary keys after recovery from
	 * a snapshot.
	 */
	virtual bool needToBuildSecondaryKey(struct space *space);

	virtual void join(Relay *) = 0;
	/**
	 * Begin a new statement in an existing or new
	 * transaction.
	 * We use a single call to save a virtual method call
	 * since it's always clear from txn whether it's
	 * autocommit mode or not, the first statement or
	 * a subsequent statement.  Effectively it means that
	 * transaction in the engine begins with the first
	 * statement.
	 */
	virtual void beginStatement(struct txn *);
	/**
	 * Called before a WAL write is made to prepare
	 * a transaction for commit in the engine.
	 */
	virtual void prepare(struct txn *);
	/**
	 * End the transaction in the engine, the transaction
	 * has been successfully written to the WAL.
	 * This method can't throw: if any error happens here,
	 * there is no better option than panic.
	 */
	virtual void commit(struct txn *);
	/*
	 * Called to roll back effects of a statement if an
	 * error happens, e.g., in a trigger.
	 */
	virtual void rollbackStatement(struct txn_stmt *);
	/*
	 * Roll back and end the transaction in the engine.
	 */
	virtual void rollback(struct txn *);
	/**
	 * Recover the engine to a checkpoint it has.
	 * After that the engine will be given rows
	 * from the binary log to replay.
	 */
	virtual void recoverToCheckpoint(int64_t checkpoint_id) = 0;
	/**
	 * Inform the engine about the end of recovery from the
	 * binary log.
	 */
	virtual void endRecovery() = 0;
	/**
	 * Notify engine about a JOIN start (slave-side)
	 */
	virtual void beginJoin() = 0;
	/**
	 * Begin a two-phase snapshot creation in this
	 * engine (snapshot is a memtx idea of a checkpoint).
	 */
	virtual int beginCheckpoint(int64_t) = 0;
	/**
	 * Wait for a checkpoint to complete. The LSN
	 * must match one in begin_checkpoint().
	 */
	virtual int waitCheckpoint() = 0;
	/**
	 * All engines prepared their checkpoints,
	 * fix up the changes.
	 */
	virtual void commitCheckpoint() = 0;
	/**
	 * An error in one of the engines, abort checkpoint.
	 */
	virtual void abortCheckpoint() = 0;
public:
	/** Name of the engine. */
	const char *name;
	/** Engine id. */
	uint32_t id;
	/** Engine flags */
	uint32_t flags;
	/** Used for search for engine by name. */
	struct rlist link;
};

/** Engine handle - an operator of a space */

struct Handler: public Object {
public:
	Handler(Engine *f);
	virtual ~Handler() {}

	/**
	 * @brief A single method to handle REPLACE, DELETE and UPDATE.
	 *
	 * @param sp space
	 * @param old_tuple the tuple that should be removed (can be NULL)
	 * @param new_tuple the tuple that should be inserted (can be NULL)
	 * @param mode      dup_replace_mode, used only if new_tuple is not
	 *                  NULL and old_tuple is NULL, and only for the
	 *                  primary key.
	 *
	 * For DELETE, new_tuple must be NULL. old_tuple must be
	 * previously found in the primary key.
	 *
	 * For REPLACE, old_tuple must be NULL. The additional
	 * argument dup_replace_mode further defines how REPLACE
	 * should proceed.
	 *
	 * For UPDATE, both old_tuple and new_tuple must be given,
	 * where old_tuple must be previously found in the primary key.
	 *
	 * Let's consider these three cases in detail:
	 *
	 * 1. DELETE, old_tuple is not NULL, new_tuple is NULL
	 *    The effect is that old_tuple is removed from all
	 *    indexes. dup_replace_mode is ignored.
	 *
	 * 2. REPLACE, old_tuple is NULL, new_tuple is not NULL,
	 *    has one simple sub-case and two with further
	 *    ramifications:
	 *
	 *	A. dup_replace_mode is DUP_INSERT. Attempts to insert the
	 *	new tuple into all indexes. If *any* of the unique indexes
	 *	has a duplicate key, deletion is aborted, all of its
	 *	effects are removed, and an error is thrown.
	 *
	 *	B. dup_replace_mode is DUP_REPLACE. It means an existing
	 *	tuple has to be replaced with the new one. To do it, tries
	 *	to find a tuple with a duplicate key in the primary index.
	 *	If the tuple is not found, throws an error. Otherwise,
	 *	replaces the old tuple with a new one in the primary key.
	 *	Continues on to secondary keys, but if there is any
	 *	secondary key, which has a duplicate tuple, but one which
	 *	is different from the duplicate found in the primary key,
	 *	aborts, puts everything back, throws an exception.
	 *
	 *	For example, if there is a space with 3 unique keys and
	 *	two tuples { 1, 2, 3 } and { 3, 1, 2 }:
	 *
	 *	This REPLACE/DUP_REPLACE is OK: { 1, 5, 5 }
	 *	This REPLACE/DUP_REPLACE is not OK: { 2, 2, 2 } (there
	 *	is no tuple with key '2' in the primary key)
	 *	This REPLACE/DUP_REPLACE is not OK: { 1, 1, 1 } (there
	 *	is a conflicting tuple in the secondary unique key).
	 *
	 *	C. dup_replace_mode is DUP_REPLACE_OR_INSERT. If
	 *	there is a duplicate tuple in the primary key, behaves the
	 *	same way as DUP_REPLACE, otherwise behaves the same way as
	 *	DUP_INSERT.
	 *
	 * 3. UPDATE has to delete the old tuple and insert a new one.
	 *    dup_replace_mode is ignored.
	 *    Note that old_tuple primary key doesn't have to match
	 *    new_tuple primary key, thus a duplicate can be found.
	 *    For this reason, and since there can be duplicates in
	 *    other indexes, UPDATE is the same as DELETE +
	 *    REPLACE/DUP_INSERT.
	 *
	 * @return old_tuple. DELETE, UPDATE and REPLACE/DUP_REPLACE
	 * always produce an old tuple. REPLACE/DUP_INSERT always returns
	 * NULL. REPLACE/DUP_REPLACE_OR_INSERT may or may not find
	 * a duplicate.
	 *
	 * The method is all-or-nothing in all cases. Changes are either
	 * applied to all indexes, or nothing applied at all.
	 *
	 * Note, that even in case of REPLACE, dup_replace_mode only
	 * affects the primary key, for secondary keys it's always
	 * DUP_INSERT.
	 *
	 * The call never removes more than one tuple: if
	 * old_tuple is given, dup_replace_mode is ignored.
	 * Otherwise, it's taken into account only for the
	 * primary key.
	 */
	engine_replace_f replace;

	Engine *engine;
};

/** Register engine engine instance. */
void engine_register(Engine *engine);

/** Call a visitor function on every registered engine. */
#define engine_foreach(engine) rlist_foreach_entry(engine, &engines, link)

/** Find engine engine by name. */
Engine *engine_find(const char *name);

/** Shutdown all engine factories. */
void engine_shutdown();

static inline bool
engine_can_be_temporary(uint32_t flags)
{
	return flags & ENGINE_CAN_BE_TEMPORARY;
}

static inline bool
engine_auto_check_update(uint32_t flags)
{
	return flags & ENGINE_AUTO_CHECK_UPDATE;
}

static inline uint32_t
engine_id(Handler *space)
{
	return space->engine->id;
}

/**
 * Tell the engine what the last LSN to recover from is
 * (during server start.
 */
void
engine_recover_to_checkpoint(int64_t checkpoint_id);

/**
 * Called at the start of JOIN routine.
 */
void
engine_begin_join();

/**
 * Called at the end of recovery.
 * Build secondary keys in all spaces.
 */
void
engine_end_recovery();

/**
 * Save a snapshot.
 */
int
engine_checkpoint(int64_t checkpoint_id);

/**
 * Send a snapshot.
 */
void
engine_join(Relay *);

#endif /* TARANTOOL_BOX_ENGINE_H_INCLUDED */
