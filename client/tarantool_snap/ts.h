#ifndef TS_H_INCLUDED
#define TS_H_INCLUDED

struct ts {
	struct ts_options opts;
	struct ts_spaces s;
	struct ts_reftable rt;
	uint64_t last_snap_lsn;
	uint64_t last_xlog_lsn;
};

#endif
