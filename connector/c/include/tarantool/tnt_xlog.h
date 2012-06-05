#ifndef TNT_XLOG_H_INCLUDED
#define TNT_XLOG_H_INCLUDED

enum tnt_xlog_error {
	TNT_XLOG_EOK,
	TNT_XLOG_EFAIL,
	TNT_XLOG_EMEMORY,
	TNT_XLOG_ETYPE,
	TNT_XLOG_EVERSION,
	TNT_XLOG_ECORRUPT,
	TNT_XLOG_ESYSTEM,
	TNT_XLOG_LAST
};

struct tnt_xlog_header_v11 {
	uint32_t crc32_hdr;
	uint64_t lsn;
	double tm;
	uint32_t len;
	uint32_t crc32_data;
} __attribute__((packed));

struct tnt_xlog_row_v11 {
	uint16_t tag;
	uint64_t cookie;
	uint16_t op;
} __attribute__((packed));

struct tnt_stream_xlog {
	char *file;
	FILE *fd;
	off_t offset;
	struct tnt_xlog_header_v11 hdr;
	struct tnt_xlog_row_v11 row;
	enum tnt_xlog_error error;
	int errno_;
};

#define TNT_SXLOG_CAST(S) ((struct tnt_stream_xlog*)(S)->data)

struct tnt_stream *tnt_xlog(struct tnt_stream *s);

int tnt_xlog_open(struct tnt_stream *s, char *file);
void tnt_xlog_close(struct tnt_stream *s);

enum tnt_xlog_error tnt_xlog_error(struct tnt_stream *s);
char *tnt_xlog_strerror(struct tnt_stream *s);
int tnt_xlog_errno(struct tnt_stream *s);

#endif /* TNT_XLOG_H_INCLUDED */
