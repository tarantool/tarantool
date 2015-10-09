#ifndef TARANTOOL_BOX_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_ENGINE_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "index.h"

struct request;
struct space;
struct tuple;
struct relay;

enum engine_flags {
	ENGINE_CAN_BE_TEMPORARY = 1,
	ENGINE_AUTO_CHECK_UPDATE = 2,
};

extern struct rlist engines;

typedef void
(*engine_replace_f)(struct txn *txn, struct space *,
		    struct tuple *, struct tuple *, enum dup_replace_mode);

class Handler;

/** Engine instance */
class Engine {
public:
	Engine(const char *engine_name);

	Engine(const Engine &) = delete;
	Engine& operator=(const Engine&) = delete;
	virtual ~Engine() {}
	/** Called once at startup. */
	virtual void init();
	/** Create a new engine instance for a space. */
	virtual Handler *open() = 0;
	/**
	 * Create an instance of space index. Used in alter
	 * space.
	 */
	virtual Index *createIndex(struct key_def*) = 0;
	virtual void initSystemSpace(struct space *space);
	/**
	 * Check a key definition for violation of
	 * various limits.
	 */
	virtual void keydefCheck(struct space *space, struct key_def*);
	/**
	 * Called by alter when a primary key added,
	 * after createIndex is invoked for the new
	 * key.
	 */
	virtual void addPrimaryKey(struct space *space);
	/**
	 * Delete all tuples in the index on drop.
	 */
	virtual void dropIndex(Index *);
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

	virtual void join(struct relay *);
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
	virtual void recoverToCheckpoint(int64_t checkpoint_id);
	/**
	 * Inform the engine about the end of recovery from the
	 * binary log.
	 */
	virtual void endRecovery();
	/**
	 * Notify engine about a JOIN start (slave-side)
	 */
	virtual void beginJoin();
	/**
	 * Begin a two-phase snapshot creation in this
	 * engine (snapshot is a memtx idea of a checkpoint).
	 */
	virtual int beginCheckpoint(int64_t);
	/**
	 * Wait for a checkpoint to complete. The LSN
	 * must match one in begin_checkpoint().
	 */
	virtual int waitCheckpoint();
	/**
	 * All engines prepared their checkpoints,
	 * fix up the changes.
	 */
	virtual void commitCheckpoint();
	/**
	 * An error in one of the engines, abort checkpoint.
	 */
	virtual void abortCheckpoint();

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

class Handler {
public:
	Handler(Engine *f);
	virtual ~Handler() {}
	Handler(const Handler &) = delete;
	Handler& operator=(const Handler&) = delete;

	virtual struct tuple *
	executeReplace(struct txn *, struct space *,
		       struct request *);
	virtual struct tuple *
	executeDelete(struct txn *, struct space *,
		      struct request *);
	virtual struct tuple *
	executeUpdate(struct txn *, struct space *,
		      struct request *);
	virtual void
	executeUpsert(struct txn *, struct space *,
		      struct request *);

	virtual void
	executeSelect(struct txn *, struct space *,
		      uint32_t index_id, uint32_t iterator,
		      uint32_t offset, uint32_t limit,
		      const char *key, const char *key_end,
		      struct port *);

	virtual void onAlter(Handler *old);
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
engine_join(struct relay *);

#endif /* TARANTOOL_BOX_ENGINE_H_INCLUDED */
