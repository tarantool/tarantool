#ifndef TNT_LOG_H_INCLUDED
#define TNT_LOG_H_INCLUDED

/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define TNT_LOG_MAGIC_XLOG "XLOG\n"
#define TNT_LOG_MAGIC_SNAP "SNAP\n"
#define TNT_LOG_VERSION "0.11\n"

enum tnt_log_error {
	TNT_LOG_EOK,
	TNT_LOG_EFAIL,
	TNT_LOG_EMEMORY,
	TNT_LOG_ETYPE,
	TNT_LOG_EVERSION,
	TNT_LOG_ECORRUPT,
	TNT_LOG_ESYSTEM,
	TNT_LOG_LAST
};

struct tnt_log_header_v11 {
	uint32_t crc32_hdr;
	uint64_t lsn;
	double tm;
	uint32_t len;
	uint32_t crc32_data;
} __attribute__((packed));

struct tnt_log_row_v11 {
	uint16_t tag;
	uint64_t cookie;
	uint16_t op;
} __attribute__((packed));

struct tnt_log_row_snap_v11 {
	uint16_t tag;
	uint64_t cookie;
	uint32_t space;
	uint32_t tuple_size;
	uint32_t data_size;
} __attribute__((packed));

enum tnt_log_type {
	TNT_LOG_NONE,
	TNT_LOG_XLOG,
	TNT_LOG_SNAPSHOT
};

union tnt_log_value {
	struct tnt_request r;
	struct tnt_tuple t;
};

struct tnt_log_row {
	struct tnt_log_header_v11 hdr;
	struct tnt_log_row_v11 row;
	struct tnt_log_row_snap_v11 row_snap;
	union tnt_log_value *value;
};

struct tnt_log {
	enum tnt_log_type type;
	FILE *fd;
	off_t current_offset;
	off_t offset;
	int (*read)(struct tnt_log *l, char **buf, uint32_t *size);
	int (*process)(struct tnt_log *l, char *buf, uint32_t size,
		       union tnt_log_value *value);
	struct tnt_log_row current;
	union tnt_log_value current_value;
	enum tnt_log_error error;
	int errno_;
};

extern const uint32_t tnt_log_marker_v11;
extern const uint32_t tnt_log_marker_eof_v11;

enum tnt_log_type tnt_log_guess(char *file);

enum tnt_log_error
tnt_log_open(struct tnt_log *l, char *file, enum tnt_log_type type);
int tnt_log_seek(struct tnt_log *l, off_t offset);
void tnt_log_close(struct tnt_log *l);

struct tnt_log_row *tnt_log_next(struct tnt_log *l);
struct tnt_log_row *tnt_log_next_to(struct tnt_log *l, union tnt_log_value *value);

enum tnt_log_error tnt_log_error(struct tnt_log *l);
char *tnt_log_strerror(struct tnt_log *l);
int tnt_log_errno(struct tnt_log *l);

#endif /* TNT_LOG_H_INCLUDED */
