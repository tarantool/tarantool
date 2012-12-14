#ifndef INCLUDES_TARANTOOL_ARCHIVE_H
#define INCLUDES_TARANTOOL_ARCHIVE_H
#include <util.h>

struct txn;
struct tuple;


/**
 * @brief arc_init init of archive module, arc_dirname NULL means that module is disabled
 * @param arc_dirname path to module folder relative to work_dir
 * @param arc_filename_format date format pattern for archive files. For example %Y-%m-%d produce files like 2012-11-01-latest.arch
 * @param fsync_delay delay between fsync call on archive file.
 */
void arc_init(const char *arc_dirname,const char *arc_filename_format, double fsync_delay);

/**  **/
/**
 * @brief arc_start should be called when recovery finished and tarantool leaves standby mode and ready to work
 * @return 0 if all ok or -1 if io thread creation failed
 */
int arc_start();

/**
 * @brief arc_save_real_tm used to save real time when do recovery
 * @param tm real tm value for v11 row
 */
void arc_save_real_tm(double tm);

/**
 * @brief arc_do_txn should be called when commiting transaction
 * @param txn current transaction
 */
void arc_do_txn(struct txn *txn);

/**
 * @brief arc_free should be called to free module resources
 */
void arc_free();


#endif
