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
#include <stdint.h>
#include <small/rlist.h>

#include "diag.h"
#include "error.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/**
	 * For simplicity, assume that the total engine count can't exceed
	 * the value of this constant.
	 */
	MAX_ENGINE_COUNT = 10,
	/**
	 * Max number of engines involved in a multi-statement transaction.
	 * This value must be greater than any `engine::id' of an engine
	 * without `ENGINE_BYPASS_TX' flag.
	 */
	MAX_TX_ENGINE_COUNT = 3,
};

struct engine;
struct engine_read_view;
struct txn;
struct txn_stmt;
struct read_view_opts;
struct space;
struct space_def;
struct vclock;
struct xstream;
struct engine_join_ctx;

extern struct rlist engines;

/**
 * Recovery state of entire tarantool. Apart from memtx recovery state,
 * which is internal recovery optimization status, this enum describers
 * real sequence of recovery actions.
 */
enum recovery_state {
	/** Recovery have not been started yet. */
	RECOVERY_NOT_STARTED,
	/** Recovery from snapshot file. */
	INITIAL_RECOVERY,
	/** Recovery from WAL file(s). */
	FINAL_RECOVERY,
	/** Recovery from WAL file(s). */
	FINISHED_RECOVERY,
};

/**
 * The one and only recovery status of entire tarantool.
 * See enum recovery_state description.
 */
extern enum recovery_state recovery_state;

/**
 * Aggregated memory statistics. Used by box.info.memory().
 */
struct engine_memory_stat {
	/** Size of memory used for storing user data. */
	size_t data;
	/** Size of memory used for indexing user data. */
	size_t index;
	/** Size of memory used for caching user data. */
	size_t cache;
	/** Size of memory used by active transactions. */
	size_t tx;
};

typedef int
engine_backup_cb(const char *path, void *arg);

struct engine_vtab {
	/** Destroy an engine instance. */
	void (*free)(struct engine *);
	/**
	 * Shutdown an engine instance. Shutdown stops all internal
	 * fibers/threads. It may yield.
	 */
	void (*shutdown)(struct engine *);
	/** Allocate a new space instance. */
	struct space *(*create_space)(struct engine *engine,
			struct space_def *def, struct rlist *key_list);
	/**
	 * Create a read view of the data stored in the engine.
	 *
	 * This function is supposed to do the engine-wide work necessary for
	 * creation of a read view, e.g. disable garbage collection. An index
	 * read view is created by index_vtab::create_read_view. The caller
	 * must not yield between calling this function and creation of the
	 * corresponding index read views.
	 *
	 * May be called only if the engine has the ENGINE_SUPPORTS_READ_VIEW
	 * flag set.
	 */
	struct engine_read_view *(*create_read_view)(
		struct engine *engine, const struct read_view_opts *opts);
	/**
	 * Freeze a read view to feed to a new replica.
	 * Setup and return a context that will be used
	 * on further steps.
	 */
	int (*prepare_join)(struct engine *engine, struct engine_join_ctx *ctx);
	/**
	 * Feed the read view frozen on the previous step to
	 * the given stream.
	 */
	int (*join)(struct engine *engine, struct engine_join_ctx *ctx,
		    struct xstream *stream);
	/**
	 * Release the read view and free the context prepared
	 * on the first step.
	 */
	void (*complete_join)(struct engine *engine,
			      struct engine_join_ctx *ctx);
	/**
	 * Begin a new single or multi-statement transaction.
	 * Called on first statement in a transaction, not when
	 * a user said begin(). Effectively it means that
	 * transaction in the engine begins with the first
	 * statement.
	 */
	int (*begin)(struct engine *, struct txn *);
	/**
	 * Begine one statement in existing transaction.
	 */
	int (*begin_statement)(struct engine *, struct txn *);
	/**
	 * Called before a WAL write is made to prepare
	 * a transaction for commit in the engine.
	 */
	int (*prepare)(struct engine *, struct txn *);
	/**
	 * End the transaction in the engine, the transaction
	 * has been successfully written to the WAL.
	 * This method can't throw: if any error happens here,
	 * there is no better option than panic.
	 */
	void (*commit)(struct engine *, struct txn *);
	/*
	 * Called to roll back effects of a statement if an
	 * error happens, e.g., in a trigger.
	 */
	void (*rollback_statement)(struct engine *, struct txn *,
				   struct txn_stmt *);
	/*
	 * Roll back and end the transaction in the engine.
	 */
	void (*rollback)(struct engine *, struct txn *);
	/**
	 * Notify the engine that the instance is about to switch
	 * to read-only mode. The engine is supposed to abort all
	 * active rw transactions when this method is called.
	 */
	void (*switch_to_ro)(struct engine *);
	/**
	 * Bootstrap an empty data directory
	 */
	int (*bootstrap)(struct engine *);
	/**
	 * Begin initial recovery from checkpoint or dirty disk data.
	 * On local recovery @recovery_vclock points to the vclock
	 * used for assigning LSNs to statements replayed from WAL.
	 * On remote recovery, it is set to NULL.
	 */
	int (*begin_initial_recovery)(struct engine *engine,
			const struct vclock *recovery_vclock);
	/**
	 * Notify engine about a start of recovering from WALs
	 * that could be local WALs during local recovery
	 * of WAL catch up durin join on slave side
	 */
	int (*begin_final_recovery)(struct engine *);
	/**
	 * Notify the engine that the instance is about to enter
	 * the hot standby mode to complete recovery from WALs.
	 */
	int (*begin_hot_standby)(struct engine *);
	/**
	 * Inform the engine about the end of recovery from the
	 * binary log.
	 */
	int (*end_recovery)(struct engine *);
	/**
	 * Begin a two-phase checkpoint creation in this
	 * engine (snapshot is a memtx idea of a checkpoint).
	 * Must not yield.
	 */
	int (*begin_checkpoint)(struct engine *, bool is_scheduled);
	/**
	 * Wait for a checkpoint to complete.
	 */
	int (*wait_checkpoint)(struct engine *, const struct vclock *);
	/**
	 * All engines prepared their checkpoints,
	 * fix up the changes.
	 */
	void (*commit_checkpoint)(struct engine *, const struct vclock *);
	/**
	 * An error in one of the engines, abort checkpoint.
	 */
	void (*abort_checkpoint)(struct engine *);
	/**
	 * Remove files that are not needed to recover
	 * from checkpoint @vclock or newer.
	 *
	 * If this function returns a non-zero value, garbage
	 * collection is aborted, i.e. this method isn't called
	 * for other engines and xlog files aren't deleted.
	 *
	 * Used to abort garbage collection in case memtx engine
	 * fails to delete a snapshot file, because we recover
	 * checkpoint list by scanning the snapshot directory.
	 */
	void (*collect_garbage)(struct engine *engine,
				const struct vclock *vclock);
	/**
	 * Backup callback. It is supposed to call @cb for each file
	 * that needs to be backed up in order to restore from the
	 * checkpoint @vclock.
	 */
	int (*backup)(struct engine *engine, const struct vclock *vclock,
		      engine_backup_cb cb, void *cb_arg);
	/**
	 * Accumulate engine memory statistics.
	 */
	void (*memory_stat)(struct engine *, struct engine_memory_stat *);
	/**
	 * Reset all incremental statistic counters.
	 */
	void (*reset_stat)(struct engine *);
	/**
	 * Check definition of a new space for engine-specific
	 * limitations. E.g. not all engines support data-temporary
	 * spaces.
	 */
	int (*check_space_def)(struct space_def *);
};

enum {
	/**
	 * If set, the engine will not participate in transaction
	 * control. In particular, this means that any operations
	 * done on this engine's spaces can mix in other engine's
	 * transactions w/o throwing ER_CROSS_ENGINE_TRANSACTION.
	 */
	ENGINE_BYPASS_TX = 1 << 0,
	/**
	 * Set if the engine supports creation of a read view.
	 */
	ENGINE_SUPPORTS_READ_VIEW = 1 << 1,
	/**
	 * Set if checkpointing is implemented by the memtx engine.
	 * Engine setting this flag must support read views.
	 */
	ENGINE_CHECKPOINT_BY_MEMTX = 1 << 2,
	/**
	 * Set if replica join is implemented by the memtx engine.
	 * Engine setting this flag must support read views.
	 */
	ENGINE_JOIN_BY_MEMTX = 1 << 3,
	/**
	 * Set if the engine supports cross-engine transactions.
	 */
	ENGINE_SUPPORTS_CROSS_ENGINE_TX = 1 << 4,
};

struct engine {
	/** Virtual function table. */
	const struct engine_vtab *vtab;
	/** Engine name. */
	const char *name;
	/** Engine id. */
	uint32_t id;
	/** Engine flags. */
	uint32_t flags;
	/** Used for search for engine by name. */
	struct rlist link;
};

/** Engine read view virtual function table. */
struct engine_read_view_vtab {
	/** Free an engine read view instance. */
	void
	(*free)(struct engine_read_view *rv);
};

/**
 * Engine read view.
 *
 * Must not be freed until all corresponding index read views are closed.
 */
struct engine_read_view {
	/** Virtual function table. */
	const struct engine_read_view_vtab *vtab;
	/** Link in read_view::engines. */
	struct rlist link;
};

/**
 * Cursor used during checkpoint initial join. Shared between engines.
 */
struct checkpoint_cursor {
	/** Signature of the checkpoint to take data from. */
	struct vclock *vclock;
	/** Checkpoint lsn to start from. */
	int64_t start_lsn;
	/** Counter, shared between engines */
	int64_t lsn_counter;
};

struct engine_join_ctx {
	/** Vclock to respond with. */
	struct vclock *vclock;
	/** Whether sending JOIN_META stage is required. */
	bool send_meta;
	/** Checkpoint join cursor. */
	struct checkpoint_cursor *cursor;
	/** Array of engine join contexts, one per each engine. */
	void **data;
};

/** Register engine instance. */
void engine_register(struct engine *engine);

/** Call a visitor function on every registered engine. */
#define engine_foreach(engine) rlist_foreach_entry(engine, &engines, link)

/** Find engine engine by name. */
struct engine *
engine_by_name(const char *name);

/** Find engine by name and raise error if not found. */
static inline struct engine *
engine_find(const char *name)
{
	struct engine *engine = engine_by_name(name);
	if (engine == NULL)
		diag_set(ClientError, ER_NO_SUCH_ENGINE, name);
	return engine;
}

static inline struct space *
engine_create_space(struct engine *engine, struct space_def *def,
		    struct rlist *key_list)
{
	return engine->vtab->create_space(engine, def, key_list);
}

static inline struct engine_read_view *
engine_create_read_view(struct engine *engine,
			const struct read_view_opts *opts)
{
	return engine->vtab->create_read_view(engine, opts);
}

static inline int
engine_begin(struct engine *engine, struct txn *txn)
{
	return engine->vtab->begin(engine, txn);
}

static inline int
engine_begin_statement(struct engine *engine, struct txn *txn)
{
	return engine->vtab->begin_statement(engine, txn);
}

static inline int
engine_prepare(struct engine *engine, struct txn *txn)
{
	return engine->vtab->prepare(engine, txn);
}

static inline void
engine_commit(struct engine *engine, struct txn *txn)
{
	engine->vtab->commit(engine, txn);
}

static inline void
engine_rollback_statement(struct engine *engine, struct txn *txn,
			  struct txn_stmt *stmt)
{
	engine->vtab->rollback_statement(engine, txn, stmt);
}

static inline void
engine_rollback(struct engine *engine, struct txn *txn)
{
	engine->vtab->rollback(engine, txn);
}

static inline int
engine_check_space_def(struct engine *engine, struct space_def *def)
{
	return engine->vtab->check_space_def(def);
}

static inline void
engine_read_view_delete(struct engine_read_view *rv)
{
	rv->vtab->free(rv);
}

/**
 * Shutdown all engines. Shutdown stops all internal fibers/threads.
 * It may yield.
 */
void
engine_shutdown(void);

/**
 * Free all engines.
 */
void
engine_free(void);

/**
 * Called before switching the instance to read-only mode.
 */
void
engine_switch_to_ro(void);

/**
 * Initialize an empty data directory
 */
int
engine_bootstrap(void);

/**
 * Called at the start of recovery.
 */
int
engine_begin_initial_recovery(const struct vclock *recovery_vclock);

/**
 * Called in the middle of JOIN stage,
 * when xlog catch-up process is started
 */
int
engine_begin_final_recovery(void);

/**
 * Called before entering the hot standby mode.
 */
int
engine_begin_hot_standby(void);

/**
 * Called at the end of recovery.
 */
int
engine_end_recovery(void);

int
engine_prepare_join(struct engine_join_ctx *ctx);

int
engine_join(struct engine_join_ctx *ctx, struct xstream *stream);

void
engine_complete_join(struct engine_join_ctx *ctx);

int
engine_begin_checkpoint(bool is_scheduled);

/**
 * Create a checkpoint.
 */
int
engine_commit_checkpoint(const struct vclock *vclock);

void
engine_abort_checkpoint(void);

void
engine_collect_garbage(const struct vclock *vclock);

int
engine_backup(const struct vclock *vclock, engine_backup_cb cb, void *cb_arg);

void
engine_memory_stat(struct engine_memory_stat *stat);

void
engine_reset_stat(void);

/*
 * Virtual method stubs.
 */
struct engine_read_view *
generic_engine_create_read_view(struct engine *engine,
				const struct read_view_opts *opts);
int generic_engine_prepare_join(struct engine *, struct engine_join_ctx *);
int generic_engine_join(struct engine *, struct engine_join_ctx *,
			struct xstream *);
void generic_engine_complete_join(struct engine *, struct engine_join_ctx *);
int generic_engine_begin(struct engine *, struct txn *);
int generic_engine_begin_statement(struct engine *, struct txn *);
int generic_engine_prepare(struct engine *, struct txn *);
void generic_engine_commit(struct engine *, struct txn *);
void generic_engine_rollback_statement(struct engine *, struct txn *,
				       struct txn_stmt *);
void generic_engine_rollback(struct engine *, struct txn *);
void generic_engine_switch_to_ro(struct engine *);
int generic_engine_bootstrap(struct engine *);
int generic_engine_begin_initial_recovery(struct engine *,
					  const struct vclock *);
int generic_engine_begin_final_recovery(struct engine *);
int generic_engine_begin_hot_standby(struct engine *);
int generic_engine_end_recovery(struct engine *);
int generic_engine_begin_checkpoint(struct engine *, bool);
int generic_engine_wait_checkpoint(struct engine *, const struct vclock *);
void generic_engine_commit_checkpoint(struct engine *, const struct vclock *);
void generic_engine_abort_checkpoint(struct engine *);
void generic_engine_collect_garbage(struct engine *, const struct vclock *);
int generic_engine_backup(struct engine *, const struct vclock *,
			  engine_backup_cb, void *);
void generic_engine_memory_stat(struct engine *, struct engine_memory_stat *);
void generic_engine_reset_stat(struct engine *);
int generic_engine_check_space_def(struct space_def *);
void generic_engine_shutdown(struct engine *engine);

#if defined(__cplusplus)
} /* extern "C" */

static inline struct engine *
engine_find_xc(const char *name)
{
	struct engine *engine = engine_find(name);
	if (engine == NULL)
		diag_raise();
	return engine;
}

static inline struct space *
engine_create_space_xc(struct engine *engine, struct space_def *def,
		    struct rlist *key_list)
{
	struct space *space = engine_create_space(engine, def, key_list);
	if (space == NULL)
		diag_raise();
	return space;
}

static inline void
engine_begin_xc(struct engine *engine, struct txn *txn)
{
	if (engine_begin(engine, txn) != 0)
		diag_raise();
}

static inline void
engine_begin_statement_xc(struct engine *engine, struct txn *txn)
{
	if (engine_begin_statement(engine, txn) != 0)
		diag_raise();
}

static inline void
engine_prepare_xc(struct engine *engine, struct txn *txn)
{
	if (engine_prepare(engine, txn) != 0)
		diag_raise();
}

static inline void
engine_check_space_def_xc(struct engine *engine, struct space_def *def)
{
	if (engine_check_space_def(engine, def) != 0)
		diag_raise();
}

static inline void
engine_bootstrap_xc(void)
{
	if (engine_bootstrap() != 0)
		diag_raise();
}

static inline void
engine_begin_initial_recovery_xc(const struct vclock *recovery_vclock)
{
	if (engine_begin_initial_recovery(recovery_vclock) != 0)
		diag_raise();
}

static inline void
engine_begin_final_recovery_xc(void)
{
	if (engine_begin_final_recovery() != 0)
		diag_raise();
}

static inline void
engine_begin_hot_standby_xc(void)
{
	if (engine_begin_hot_standby() != 0)
		diag_raise();
}

static inline void
engine_end_recovery_xc(void)
{
	if (engine_end_recovery() != 0)
		diag_raise();
}

static inline void
engine_prepare_join_xc(struct engine_join_ctx *ctx)
{
	if (engine_prepare_join(ctx) != 0)
		diag_raise();
}

static inline void
engine_join_xc(struct engine_join_ctx *ctx, struct xstream *stream)
{
	if (engine_join(ctx, stream) != 0)
		diag_raise();
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_ENGINE_H_INCLUDED */
