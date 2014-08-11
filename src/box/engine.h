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
#include <exception.h>
#include "index.h"

struct space;
struct tuple;

enum engine_flags {
	ENGINE_TRANSACTIONAL = 1,
	ENGINE_NO_YIELD = 2,
};

extern uint32_t engine_flags[BOX_ENGINE_MAX];

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

/**
 * Engine specific recovery events that represents
 * global recovery stage change.
 */
enum engine_recovery_event {
	END_RECOVERY_SNAPSHOT,
	END_RECOVERY
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

struct Engine;

/** Engine instance */
class EngineFactory: public Object {
public:
	EngineFactory(const char *engine_name);
	virtual ~EngineFactory() {}
	/** Called once at startup. */
	virtual void init();
	/** Recover engine, called once at startup. */
	virtual void recover();
	/** Called at server shutdown. */
	virtual void shutdown();
	/** Create a new engine instance for a space. */
	virtual Engine *open() = 0;
	/**
	 * Create an instance of space index. Used in alter
	 * space.
	 */
	virtual Index *createIndex(struct key_def*) = 0;
	/*
	 * Delete all tuples in the index on drop.
	 */
	virtual void dropIndex(Index*) = 0;
	/**
	 * Check a key definition for violation of
	 * various limits.
	 */
	virtual void keydefCheck(struct key_def *key_def) = 0;
	/* Clean transaction engine resources.  */
	virtual void txnFinish(struct txn *txn);
	/* Inform engine about a recovery stage change. */
	virtual void recoveryEvent(enum engine_recovery_event);
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

struct Engine: public Object {
public:
	Engine(EngineFactory *f);
	virtual ~Engine() {}

	inline struct tuple*
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
		recovery = factory->recovery;
	}

	engine_recovery recovery;
	EngineFactory *factory;
};

/** Register engine factory instance. */
void engine_register(EngineFactory *engine);

/** Call a visitor function on every registered engine. */
void engine_foreach(void (*func)(EngineFactory *engine, void *udata),
                    void *udata);

/** Find engine factory by name. */
EngineFactory *engine_find(const char *name);

/** Shutdown all engine factories. */
void engine_shutdown();

static inline bool
engine_transactional(uint32_t id)
{
	assert(id);
	return engine_flags[id] & ENGINE_TRANSACTIONAL;
}

static inline bool
engine_no_yield(uint32_t id)
{
	assert(id);
	return engine_flags[id] & ENGINE_NO_YIELD;
}

static inline uint32_t
engine_id(Engine *engine)
{
	return engine->factory->id;
}

#endif /* TARANTOOL_BOX_ENGINE_H_INCLUDED */
