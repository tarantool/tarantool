/*
** Tarantool interface, external linkage.
**
** Note: functions with "sqlite3" prefix in their names become static in
** amalgamated build with the help of a custom preprocessor tool,
** that's why we are using a weird naming schema.
*/

/* Storage interface. */
int tarantoolSqlite3CloseCursor(BtCursor *pCur);
const void *tarantoolSqlite3PayloadFetch(BtCursor *pCur, u32 *pAmt);
int tarantoolSqlite3First(BtCursor *pCur, int *pRes);
int tarantoolSqlite3Last(BtCursor *pCur, int *pRes);
int tarantoolSqlite3Next(BtCursor *pCur, int *pRes);
int tarantoolSqlite3Previous(BtCursor *pCur, int *pRes);
int tarantoolSqlite3Count(BtCursor *pCur, i64 *pnEntry);
/*
** TODO:
** moveToUnpacked
** insert
** delete
*/

