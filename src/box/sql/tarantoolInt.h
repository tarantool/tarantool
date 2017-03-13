/*
** Tarantool interface, external linkage.
**
** Note: functions with "sqlite3" prefix in their names become static in
** amalgamated build with the help of a custom preprocessor tool,
** that's why we are using a weird naming schema.
*/

/*
 * Tarantool system spaces.
 */
#define TARANTOOL_SYS_SCHEMA_NAME  "_schema"
#define TARANTOOL_SYS_SPACE_NAME   "_space"
#define TARANTOOL_SYS_INDEX_NAME   "_index"

/* Max space id seen so far. */
#define TARANTOOL_SYS_SCHEMA_MAXID_KEY "max_id"

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
void tarantoolSqlite3LoadSchema(InitData *init);

/* Misc */
const char *tarantoolErrorMessage();

/* Storage interface. */
int tarantoolSqlite3CloseCursor(BtCursor *pCur);
const void *tarantoolSqlite3PayloadFetch(BtCursor *pCur, u32 *pAmt);
int tarantoolSqlite3First(BtCursor *pCur, int *pRes);
int tarantoolSqlite3Last(BtCursor *pCur, int *pRes);
int tarantoolSqlite3Next(BtCursor *pCur, int *pRes);
int tarantoolSqlite3Previous(BtCursor *pCur, int *pRes);
int tarantoolSqlite3MovetoUnpacked(BtCursor *pCur, UnpackedRecord *pIdxKey,
                                   int *pRes);
int tarantoolSqlite3Count(BtCursor *pCur, i64 *pnEntry);
int tarantoolSqlite3Insert(BtCursor *pCur, const BtreePayload *pX);
int tarantoolSqlite3Delete(BtCursor *pCur, u8 flags);
int tarantoolSqlite3ClearTable(int iTable);

/* Compare against the index key under a cursor -
 * the key may span non-adjacent fields in a random order,
 * ex: [4]-[1]-[2]
 */
int tarantoolSqlite3IdxKeyCompare(BtCursor *pCur, UnpackedRecord *pUnpacked,
                                  int *res);

/*
 * The function assumes the cursor is open on _schema.
 * Increment max_id and store updated tuple in the cursor
 * object.
 */
int tarantoolSqlite3IncrementMaxid(BtCursor *pCur);


/*
 * Render "format" array for _space entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeTableFormat(Table *pTable, void *buf);

/*
 * Format "opts" dictionary for _space entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeTableOpts(Table *pTable, const char *zSql, void *buf);

/*
 * Format "parts" array for _index entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeIdxParts(Index *index, void *buf);

/*
 * Format "opts" dictionary for _index entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 */
int tarantoolSqlite3MakeIdxOpts(Index *index, const char *zSql, void *buf);
