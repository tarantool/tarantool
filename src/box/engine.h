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
	ENGINE_NO_YIELD = 1,
	ENGINE_CAN_BE_TEMPORARY = 2,
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
	ENGINE_AUTO_CHECK_UPDATE = 4,
};

extern uint32_t engine_flags[BOX_ENGINE_MAX];
extern struct rlist engines;

typedef struct tuple *
(*engine_replace_f)(struct space *, struct tuple *, struct tuple *,
                    enum dup_replace_mode);

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
	 * Engine specific transaction life-cycle routines.
	 */
	virtual void begin(struct txn*, struct space*);
	virtual void commit(struct txn*);
	virtual void rollback(struct txn*);
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
engine_no_yield(uint32_t flags)
{
	return flags & ENGINE_NO_YIELD;
}

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
