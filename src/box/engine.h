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

/** Reflects what space_replace() is supposed to do. */
enum engine_recovery_state {
	/**
	 * The space is created, but has no data
	 * and no primary key, or, if there is a primary
	 * key, it's not ready for use (being built with
	 * buildNext()).
	 * Replace is always an error, since there are no
	 * indexes to add data to.
	 */
	READY_NO_KEYS,
	/**
	 * The space has a functional primary key.
	 * Replace adds the tuple to this key.
	 */
	READY_PRIMARY_KEY,
	/**
	 * The space is fully functional, all keys
	 * are fully built, replace adds its tuple
	 * to all keys.
	 */
	READY_ALL_KEYS
};

typedef void (*engine_recover_f)(struct space*);

typedef struct tuple *
(*engine_replace_f)(struct space *, struct tuple *, struct tuple *,
                    enum dup_replace_mode);

struct engine_recovery {
	enum engine_recovery_state state;
	/* Recover is called after each recover step to enable
	 * keys. When recovery is complete, it enables all keys
	 * at once and resets itself to a no-op.
	 */
	engine_recover_f recover;
	engine_replace_f replace;
};

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
	 * Delete all tuples in the index on drop.
	 */
	virtual void dropIndex(Index*) = 0;

	virtual void join(Relay*) = 0;
	/**
	 * Engine specific transaction life-cycle routines.
	 */
	virtual void begin(struct txn*, struct space*);
	virtual void commit(struct txn*);
	virtual void rollback(struct txn*);
	/** Recovery */
	virtual void begin_recover_snapshot(int64_t snapshot_lsn) = 0;
	/* Inform engine about a recovery stage change. */
	virtual void end_recover_snapshot() = 0;
	/**
	 * Inform the engine about the global recovery
	 * state change (end of recovery from the binary log).
	 */
	virtual void end_recovery() = 0;
	/**
	 * Notify engine about a JOIN start (slave-side)
	 */
	virtual void begin_join() = 0;
	/**
	 * Begin a two-phase snapshot creation in this
	 * engine (snapshot is a memtx idea of a checkpoint).
	 */
	virtual int begin_checkpoint(int64_t) = 0;
	/**
	 * Wait for a checkpoint to complete. The LSN
	 * must match one in begin_checkpoint().
	 */
	virtual int wait_checkpoint() = 0;
	/**
	 * All engines prepared their checkpoints,
	 * fix up the changes.
	 */
	virtual void commit_checkpoint() = 0;
	/**
	 * An error in one of the engines, abort checkpoint.
	 */
	virtual void abort_checkpoint() = 0;
public:
	/** Name of the engine. */
	const char *name;
	/** Engine id. */
	uint32_t id;
	/** Engine flags */
	uint32_t flags;
	struct engine_recovery recovery;
	/** Used for search for engine by name. */
	struct rlist link;
};

/** Engine handle - an operator of a space */

struct Handler: public Object {
public:
	Handler(Engine *f);
	virtual ~Handler() {}

	inline struct tuple *
	replace(struct space *space,
	        struct tuple *old_tuple,
	        struct tuple *new_tuple, enum dup_replace_mode mode)
	{
		return recovery.replace(space, old_tuple, new_tuple, mode);
	}

	inline void recover(struct space *space) {
		recovery.recover(space);
	}

	inline void initRecovery() {
		recovery = engine->recovery;
	}

	engine_recovery recovery;
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
engine_begin_recover_snapshot(int64_t snapshot_lsn);

/**
 * Called at the start of JOIN routine.
 */
void
engine_begin_join();

/**
 * Called at the end of recovery from snapshot.
 * Build primary keys in all spaces.
 * */
void
engine_end_recover_snapshot();

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
engine_join(Relay*);

#endif /* TARANTOOL_BOX_ENGINE_H_INCLUDED */
