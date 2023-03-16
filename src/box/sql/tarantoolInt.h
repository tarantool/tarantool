/*
 * Tarantool interface, external linkage.
 *
 * Note: functions with "sql" prefix in their names become static in
 * amalgamated build with the help of a custom preprocessor tool,
 * that's why we are using a weird naming schema.
 */

#include <stdint.h>

/** Structure describing field dependencies for foreign keys. */
struct field_link {
	/**
	 * There are two ways to access parent/child fields -
	 * as array of two elements and as named fields.
	 */
	union {
		struct {
			/** Fieldno of the parent field. */
			uint32_t parent_field;
			/** Fieldno of the child field. */
			uint32_t child_field;
		};
		uint32_t fields[2];
	};
};

/** Definition of foreign key constraint. */
struct fk_constraint_def {
	/** Id of space containing the REFERENCES clause (child). */
	uint32_t child_id;
	/** Id of space that the key points to (parent). */
	uint32_t parent_id;
	/** Number of fields in this key. */
	uint32_t field_count;
	/** True if it is a field constraint, false otherwise. */
	bool is_field_fk;
	/** Mapping of fields in child to fields in parent. */
	struct field_link *links;
	/** Name of the constraint. */
	char name[0];
};

/** Check constraint definition. */
struct ck_constraint_def {
	/** The 0-terminated string that defines check constraint expression. */
	char *expr_str;
	/** The id of the space this check constraint is defined for. */
	uint32_t space_id;
	/** True if this is a field constraint, false otherwise. */
	bool is_field_ck;
	/** Fieldno of the field contains the constraint. */
	uint32_t fieldno;
	/** The 0-terminated string, a name of the check constraint. */
	char name[0];
};

/* Storage interface. */
const void *tarantoolsqlPayloadFetch(BtCursor * pCur, u32 * pAmt);

int tarantoolsqlFirst(BtCursor * pCur, int *pRes);
int tarantoolsqlLast(BtCursor * pCur, int *pRes);
int tarantoolsqlNext(BtCursor * pCur, int *pRes);
int tarantoolsqlPrevious(BtCursor * pCur, int *pRes);
int tarantoolsqlMovetoUnpacked(BtCursor * pCur, UnpackedRecord * pIdxKey,
				   int *pRes);
int64_t
tarantoolsqlCount(struct BtCursor *pCur);
int tarantoolsqlInsert(struct space *space, const char *tuple,
			   const char *tuple_end);
int tarantoolsqlReplace(struct space *space, const char *tuple,
			    const char *tuple_end);

/** Execute one DELETE operation. */
int
tarantoolsqlDelete(struct BtCursor *pCur);

int
sql_cursor_seek(struct BtCursor *cur, struct Mem *mems, uint32_t len, int *res);

/**
 * Delete entry from space by its key.
 *
 * @param space Space which contains record to be deleted.
 * @param iid Index id.
 * @param key Key of record to be deleted.
 * @param key_size Size of key.
 *
 * @retval 0 on success, -1 otherwise.
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
 * @retval 0 Success.
 * @retval -1 Error.
 */
int
sql_rename_table(uint32_t space_id, const char *new_name);

/* Alter trigger statement after rename table. */
int tarantoolsqlRenameTrigger(const char *zTriggerName,
				  const char *zOldName, const char *zNewName);

/**
 * Insert tuple into ephemeral space.
 * In contrast to ordinary spaces, there is no need to create and
 * fill request or handle transaction routine.
 *
 * @param space Ephemeral space.
 * @param tuple Tuple to be inserted.
 * @param tuple_end End of tuple to be inserted.
 *
 * @retval 0 on success, -1 otherwise.
 */
int tarantoolsqlEphemeralInsert(struct space *space, const char *tuple,
				    const char *tuple_end);
int tarantoolsqlEphemeralDelete(BtCursor * pCur);
int64_t
tarantoolsqlEphemeralCount(struct BtCursor *pCur);
void
tarantoolsqlEphemeralDrop(BtCursor * pCur);

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
 * Encode format as entry to be inserted to _space on @region.
 * @param region Region to use.
 * @param def Space definition to encode.
 * @param[out] size Size of result allocation.
 *
 * @retval NULL Error.
 * @retval not NULL Pointer to msgpack on success.
 */
char *
sql_encode_table(struct region *region, struct space_def *def, uint32_t *size);

/**
 * Encode "opts" dictionary for _space entry on @region.
 * @param region Region to use.
 * @param def Space definition containing opts to encode.
 * @param[out] size Size of result allocation.
 *
 * @retval NULL Error.
 * @retval not NULL Pointer to msgpack on success.
 */
char *
sql_encode_table_opts(struct region *region, struct space_def *def,
		      uint32_t *size);

/**
 * Encode links of given foreign key constraint into MsgPack.
 *
 * @param fk FK def to encode links of.
 * @param[out] Size size of result allocation.
 *
 * @retval not NULL Pointer to msgpack on success.
 */
char *
fk_constraint_encode_links(const struct fk_constraint_def *fk, uint32_t *size);

/**
 * Drop the check constraint or foreign key. This function drops tuple and field
 * constraints. If there is more than one constraint with the given name, one of
 * them will be dropped.
 */
int
sql_constraint_drop(uint32_t space_id, const char *name);

/**
 * Create new foreign key.
 *
 * @param name Name of the foreign key.
 * @param child_id ID of the child space.
 * @param parent_id ID of the parent space.
 * @param child_fieldno Fieldno of the field in the child space where the new
 *        foreign key constraint will be created if mapping is NULL.
 * @param parent_fieldno Fieldno of the field in the parent space that the field
 *        constraint will refer to if mapping is NULL.
 * @param mapping Mapping for tuple foreign key. If null, then a field
 *        constraint is created.
 */
int
sql_foreign_key_create(const char *name, uint32_t child_id, uint32_t parent_id,
		       uint32_t child_fieldno, uint32_t parent_fieldno,
		       const char *mapping);

/**
 * Create new check constraint.
 *
 * @param name Name of the check constraint.
 * @param space_id ID of the space.
 * @param func_id ID of the constraint function.
 * @param fieldno Fieldno of the field in the space where the new check
 *        constraint will be created if is_field_ck is true.
 * @param is_field_ck If true, then a field constraint is created, otherwise a
 *        tuple constraint is created.
 */
int
sql_check_create(const char *name, uint32_t space_id, uint32_t func_id,
		 uint32_t fieldno, bool is_field_ck);

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
