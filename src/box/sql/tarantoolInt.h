/*
 * Tarantool interface, external linkage.
 *
 * Note: functions with "sqlite3" prefix in their names become static in
 * amalgamated build with the help of a custom preprocessor tool,
 * that's why we are using a weird naming schema.
 */

#include <stdint.h>

/*
 * Tarantool system spaces.
 */
#define TARANTOOL_SYS_SEQUENCE_NAME "_sequence"
#define TARANTOOL_SYS_SPACE_SEQUENCE_NAME "_space_sequence"
#define TARANTOOL_SYS_SCHEMA_NAME  "_schema"
#define TARANTOOL_SYS_SPACE_NAME   "_space"
#define TARANTOOL_SYS_INDEX_NAME   "_index"
#define TARANTOOL_SYS_TRIGGER_NAME "_trigger"
#define TARANTOOL_SYS_TRUNCATE_NAME "_truncate"
#define TARANTOOL_SYS_SQL_STAT1_NAME "_sql_stat1"
#define TARANTOOL_SYS_SQL_STAT4_NAME "_sql_stat4"

/* Max space id seen so far. */
#define TARANTOOL_SYS_SCHEMA_MAXID_KEY "max_id"

/* Insert or replace operation types - necessary for vdbe */
#define TARANTOOL_INDEX_INSERT 1
#define TARANTOOL_INDEX_REPLACE 2

/*
 * SQLite uses the root page number to identify a Table or Index BTree.
 * We switched it to using Tarantool spaces and indices instead of the
 * BTrees. Hence the functions to encode index and space id in
 * a page number.
 */
#define SQLITE_PAGENO_FROM_SPACEID_AND_INDEXID(spaceid, iid) \
  (((unsigned)(spaceid) << 10) | (iid))

#define SQLITE_PAGENO_TO_SPACEID(pgno) \
  ((unsigned)(pgno) >> 10)

#define SQLITE_PAGENO_TO_INDEXID(pgno) \
  ((pgno) & 1023)

/* Load database schema from Tarantool. */
void tarantoolSqlite3LoadSchema(InitData * init);

/* Misc */
const char *tarantoolErrorMessage();

int is_tarantool_error(int rc);

/* Storage interface. */
const void *tarantoolSqlite3PayloadFetch(BtCursor * pCur, u32 * pAmt);

/**
 * Try to get a current tuple field using its field map.
 * @param pCur Btree cursor holding a tuple.
 * @param fieldno Number of a field to get.
 * @param[out] field_size Result field size.
 * @retval not NULL MessagePack field.
 * @retval     NULL Can not use field_map - it does not contain
 *         offset to @a fieldno.
 */
const void *
tarantoolSqlite3TupleColumnFast(BtCursor *pCur, u32 fieldno, u32 *field_size);

int tarantoolSqlite3First(BtCursor * pCur, int *pRes);
int tarantoolSqlite3Last(BtCursor * pCur, int *pRes);
int tarantoolSqlite3Next(BtCursor * pCur, int *pRes);
int tarantoolSqlite3Previous(BtCursor * pCur, int *pRes);
int tarantoolSqlite3MovetoUnpacked(BtCursor * pCur, UnpackedRecord * pIdxKey,
				   int *pRes);
int tarantoolSqlite3Count(BtCursor * pCur, i64 * pnEntry);
int tarantoolSqlite3Insert(struct space *space, const char *tuple,
			   const char *tuple_end);
int tarantoolSqlite3Replace(struct space *space, const char *tuple,
			    const char *tuple_end);
int tarantoolSqlite3Delete(BtCursor * pCur, u8 flags);
int
sql_delete_by_key(struct space *space, char *key, uint32_t key_size);
int tarantoolSqlite3ClearTable(struct space *space);

/* Rename table pTab with zNewName by inserting new tuple to _space.
 * SQL statement, which creates table with new name is saved in pzSqlStmt.
 */
int tarantoolSqlite3RenameTable(int iTab, const char *zNewName, char **zSqlStmt);

/* Alter trigger statement after rename table. */
int tarantoolSqlite3RenameTrigger(const char *zTriggerName,
				  const char *zOldName, const char *zNewName);

/* Alter create table statement of child foreign key table by
 * replacing parent table name in create table statement.*/
int tarantoolSqlite3RenameParentTable(int iTab, const char *zOldParentName,
				      const char *zNewParentName);

/* Interface for ephemeral tables. */
int tarantoolSqlite3EphemeralCreate(BtCursor * pCur, uint32_t filed_count,
				    struct key_def *def);
/**
 * Insert tuple into ephemeral space.
 * In contrast to ordinary spaces, there is no need to create and
 * fill request or handle transaction routine.
 *
 * @param space Ephemeral space.
 * @param tuple Tuple to be inserted.
 * @param tuple_end End of tuple to be inserted.
 *
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
 */
int tarantoolSqlite3EphemeralInsert(struct space *space, const char *tuple,
				    const char *tuple_end);
int tarantoolSqlite3EphemeralDelete(BtCursor * pCur);
int tarantoolSqlite3EphemeralCount(BtCursor * pCur, i64 * pnEntry);
int tarantoolSqlite3EphemeralDrop(BtCursor * pCur);
int tarantoolSqlite3EphemeralClearTable(BtCursor * pCur);
int tarantoolSqlite3EphemeralGetMaxId(BtCursor * pCur, uint32_t fieldno,
				       uint64_t * max_id);

/**
 * Performs exactly as extract_key + sqlite3VdbeCompareMsgpack,
 * only faster.
 *
 * @param pCur cursor which point to tuple to compare.
 * @param pUnpacked Unpacked record to compare with.
 *
 * @retval Comparison result.
 */
int
tarantoolSqlite3IdxKeyCompare(struct BtCursor *cursor,
			      struct UnpackedRecord *unpacked);

/**
 * The function assumes the cursor is open on _schema.
 * Increment max_id and store updated value it output parameter.
 * @param[out] New space id, available for usage.
 */
int
tarantoolSqlite3IncrementMaxid(uint64_t *space_max_id);

/*
 * Render "format" array for _space entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeTableFormat(Table * pTable, void *buf);

/*
 * Format "opts" dictionary for _space entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeTableOpts(Table * pTable, const char *zSql, char *buf);

/*
 * Format "parts" array for _index entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeIdxParts(Index * index, void *buf);

/*
 * Format "opts" dictionary for _index entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeIdxOpts(Index * index, const char *zSql, void *buf);

/**
 * Extract next id from _sequence space.
 * If index is empty - return 0 in max_id and success status
 *
 * @param[out] max_id Fetched value.
 * @retval 0 on success, -1 otherwise.
 */
int
tarantoolSqlNextSeqId(uint64_t *max_id);
