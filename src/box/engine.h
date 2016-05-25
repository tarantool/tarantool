#ifndef TARANTOOL_BOX_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_ENGINE_H_INCLUDED
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

	virtual void join(struct xstream *);
	/**
	 * Begin a new single or multi-statement transaction.
	 * Called on first statement in a transaction, not when
	 * a user said begin(). Effectively it means that
	 * transaction in the engine begins with the first
	 * statement.
	 */
	virtual void begin(struct txn *);
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
	virtual void commit(struct txn *, int64_t signature);
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
	 * Bootstrap an empty data directory
	 */
	virtual void bootstrap() {};
	/**
	 * Begin initial recovery from snapshot or dirty disk data.
	 */
	virtual void beginInitialRecovery() {};
	/**
	 * Notify engine about a start of recovering from WALs
	 * that could be local WALs during local recovery
	 * of WAL catch up durin join on slave side
	 */
	virtual void beginFinalRecovery() {};
	/**
	 * Inform the engine about the end of recovery from the
	 * binary log.
	 */
	virtual void endRecovery() {};

	/**
	 * Begin a two-phase snapshot creation in this
	 * engine (snapshot is a memtx idea of a checkpoint).
	 */
	virtual int beginCheckpoint();
	/**
	 * Wait for a checkpoint to complete.
	 */
	virtual int waitCheckpoint(struct vclock *vclock);
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

	virtual void
	applySnapshotRow(struct space *space, struct request *);
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
 * Initialize an empty data directory
 */
void
engine_bootstrap();

/**
 * Called at the start of recovery.
 */
void
engine_begin_initial_recovery();

/**
 * Called in the middle of JOIN stage,
 * when xlog catch-up process is started
 */
void
engine_begin_final_recovery();

/**
 * Called at the end of recovery.
 * Build secondary keys in all spaces.
 */
void
engine_end_recovery();

int
engine_begin_checkpoint();

/**
 * Save a snapshot.
 */
int
engine_commit_checkpoint(struct vclock *vclock);

void
engine_abort_checkpoint();

/**
 * Feed snapshot data as join events to the replicas.
 * (called on the master).
 */
void
engine_join(struct xstream *stream);

#endif /* TARANTOOL_BOX_ENGINE_H_INCLUDED */
