#ifndef TNT_RPL_H_INCLUDED
#define TNT_RPL_H_INCLUDED

enum tnt_rpl_error {
	TNT_RPL_EOK,
	TNT_RPL_EFAIL,
	TNT_RPL_EMEMORY,
	TNT_RPL_EVERSION,
	TNT_RPL_ESERIAL,
	TNT_RPL_ESYSTEM,
	TNT_RPL_LAST
};

struct tnt_stream_rpl {
	struct tnt_xlog_header_v11 hdr;
	struct tnt_xlog_row_v11 row;
	struct tnt_stream *net;
};

#define TNT_RPL_CAST(S) ((struct tnt_stream_rpl*)(S)->data)

struct tnt_stream *tnt_rpl(struct tnt_stream *s);
struct tnt_stream *tnt_rpl_net(struct tnt_stream *s);

int tnt_rpl_open(struct tnt_stream *s, uint64_t lsn);
void tnt_rpl_close(struct tnt_stream *s);

#endif /* TNT_XLOG_H_INCLUDED */
