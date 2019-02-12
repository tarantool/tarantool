/*
 * Tarantool interface, external linkage.
 *
 * Note: functions with "sql" prefix in their names become static in
 * amalgamated build with the help of a custom preprocessor tool,
 * that's why we are using a weird naming schema.
 */

#include <stdint.h>

struct fkey_def;

/* Misc */
const char *tarantoolErrorMessage();

int is_tarantool_error(int rc);

/* Storage interface. */
const void *tarantoolsqlPayloadFetch(BtCursor * pCur, u32 * pAmt);

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
tarantoolsqlTupleColumnFast(BtCursor *pCur, u32 fieldno, u32 *field_size);

int tarantoolsqlFirst(BtCursor * pCur, int *pRes);
int tarantoolsqlLast(BtCursor * pCur, int *pRes);
int tarantoolsqlNext(BtCursor * pCur, int *pRes);
int tarantoolsqlPrevious(BtCursor * pCur, int *pRes);
int tarantoolsqlMovetoUnpacked(BtCursor * pCur, UnpackedRecord * pIdxKey,
				   int *pRes);
int tarantoolsqlCount(BtCursor * pCur, i64 * pnEntry);
int tarantoolsqlInsert(struct space *space, const char *tuple,
			   const char *tuple_end);
int tarantoolsqlReplace(struct space *space, const char *tuple,
			    const char *tuple_end);
int tarantoolsqlDelete(BtCursor * pCur, u8 flags);

/**
 * Delete entry from space by its key.
 *
 * @param space Space which contains record to be deleted.
 * @param iid Index id.
 * @param key Key of record to be deleted.
 * @param key_size Size of key.
 *
 * @retval SQL_OK on success, SQL_TARANTOOL_DELETE_FAIL
 *         otherwise.
 */
int
sql_delete_by_key(struct space *space, uint32_t iid, char *key,
		  uint32_t key_size);

int tarantoolsqlClearTable(struct space *space, uint32_t *tuple_count);

/**
 * Rename the table in _space.
 * @param space_id Table's space identifier.
 * @param new_name new name of table
 *
 * @retval 0 on success, SQL_TARANTOOL_ERROR otherwise.
 */
int
sql_rename_table(uint32_t space_id, const char *new_name);

/* Alter trigger statement after rename table. */
int tarantoolsqlRenameTrigger(const char *zTriggerName,
				  const char *zOldName, const char *zNewName);

/**
 * Create ephemeral space. Features of ephemeral spaces: id == 0,
 * name == "ephemeral", memtx engine (in future it can be changed,
 * but now only memtx engine is supported), primary index which
 * covers all fields and no secondary indexes, given field number
 * and collation sequence. All fields are scalar and nullable.
 *
 * @param field_count Number of fields in ephemeral space.
 * @param key_info Keys description for new ephemeral space.
 *
 * @retval Pointer to created space, NULL if error.
 */
struct space *
sql_ephemeral_space_create(uint32_t filed_count, struct sql_key_info *key_info);

/**
 * Insert tuple into ephemeral space.
 * In contrast to ordinary spaces, there is no need to create and
 * fill request or handle transaction routine.
 *
 * @param space Ephemeral space.
 * @param tuple Tuple to be inserted.
 * @param tuple_end End of tuple to be inserted.
 *
 * @retval SQL_OK on success, SQL_TARANTOOL_ERROR otherwise.
 */
int tarantoolsqlEphemeralInsert(struct space *space, const char *tuple,
				    const char *tuple_end);
int tarantoolsqlEphemeralDelete(BtCursor * pCur);
int tarantoolsqlEphemeralCount(BtCursor * pCur, i64 * pnEntry);
int tarantoolsqlEphemeralDrop(BtCursor * pCur);
int tarantoolsqlEphemeralClearTable(BtCursor * pCur);

/**
 * Performs exactly as extract_key + sqlVdbeCompareMsgpack,
 * only faster.
 *
 * @param pCur cursor which point to tuple to compare.
 * @param pUnpacked Unpacked record to compare with.
 *
 * @retval Comparison result.
 */
int
tarantoolsqlIdxKeyCompare(struct BtCursor *cursor,
			      struct UnpackedRecord *unpacked);

/**
 * The function assumes the cursor is open on _schema.
 * Increment max_id and store updated value it output parameter.
 * @param[out] New space id, available for usage.
 */
int
tarantoolsqlIncrementMaxid(uint64_t *space_max_id);

/**
 * Encode format as entry to be inserted to _space on @region.
 * @param region Region to use.
 * @param table Table to encode.
 * @param[out] size Size of result allocation.
 *
 * @retval NULL Error.
 * @retval not NULL Pointer to msgpack on success.
 */
char *
sql_encode_table(struct region *region, struct Table *table, uint32_t *size);

/**
 * Encode "opts" dictionary for _space entry on @region.
 * @param region Region to use.
 * @param table Table containing opts to encode.
 * @param sql Source request to encode.
 * @param[out] size Size of result allocation.
 *
 * @retval NULL Error.
 * @retval not NULL Pointer to msgpack on success.
 */
char *
sql_encode_table_opts(struct region *region, struct Table *table,
		      const char *sql, uint32_t *size);

/**
 * Encode links of given foreign key constraint into MsgPack on
 * @region.
 * @param region Wegion to use.
 * @param def FK def to encode links of.
 * @param type Links type to encode.
 * @param[out] Size size of result allocation.
 *
 * @retval NULL Error.
 * @retval not NULL Pointer to msgpack on success.
 */
char *
fkey_encode_links(struct region *region, const struct fkey_def *def, int type,
		  uint32_t *size);

/**
 * Encode index parts of given foreign key constraint into
 * MsgPack on @region.
 * @param region Region to use.
 * @param index Index to encode.
 * @param[out] size Size of result allocation.
 *
 * @retval NULL Error.
 * @retval not NULL Pointer to msgpack on success
 */
char *
sql_encode_index_parts(struct region *region, const struct field_def *fields,
		       const struct index_def *idx_def, uint32_t *size);

/**
 * Encode "opts" dictionary for _index entry on @region.
 *
 * @param region region to use.
 * @param opts Options to encode.
 * @param[out] size size of result allocation.
 * @retval NULL on error.
 * @retval not NULL pointer to msgpack on success
 */
char *
sql_encode_index_opts(struct region *region, const struct index_opts *opts,
		      uint32_t *size);

/**
 * Extract next id from _sequence space.
 * If index is empty - return 0 in max_id and success status
 *
 * @param[out] max_id Fetched value.
 * @retval 0 on success, -1 otherwise.
 */
int
tarantoolSqlNextSeqId(uint64_t *max_id);
