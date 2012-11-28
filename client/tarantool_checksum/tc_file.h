#ifndef TC_FILE_H_INCLUDED
#define TC_FILE_H_INCLUDED

int tc_file_save(struct tc_spaces *s, uint64_t last_snap_lsn , uint64_t last_xlog_lsn, char *file);
int tc_file_load(struct tc_spaces *s, char *file,
		 uint64_t *last_xlog_lsn,
		 uint64_t *last_snap_lsn);

#endif
