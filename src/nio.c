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
#include <sys/types.h>
#include <sys/uio.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include <say.h>
#include <nio.h>

static const char *
nfilename(int fd)
{
#ifdef TARGET_OS_LINUX
	char proc_path[32];
	static __thread char filename_path[PATH_MAX];

	sprintf(proc_path, "/proc/self/fd/%d", fd);

	ssize_t sz = readlink(proc_path, filename_path,
			      sizeof(filename_path));

	if (sz >= 0) {
		filename_path[sz] = '\0';
		return filename_path;
	}
#endif /* TARGET_OS_LINUX */
	return ""; /* Not implemented. */
}

ssize_t
nread(int fd, void *buf, size_t count)
{
	ssize_t to_read = (ssize_t) count;
	while (to_read > 0) {
		ssize_t nrd = read(fd, buf, to_read);
		if (nrd < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return count != to_read ? count - to_read : -1;
			say_syserror("read, [%s]", nfilename(fd));
			return -1; /* XXX: file position is unspecified */
		}
		if (nrd == 0)
			break;

		buf += nrd;
		to_read -= nrd;
	}
	return count - to_read;
}

ssize_t
nwrite(int fd, const void *buf, size_t count)
{
	ssize_t to_write = (ssize_t) count;
	while (to_write > 0) {
		ssize_t nwr = write(fd, buf, to_write);
		if (nwr < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return count != to_write ? count - to_write : -1;
			say_syserror("write, [%s]", nfilename(fd));
			return -1; /* XXX: file position is unspecified */
		}
		if (nwr == 0)
			break;

		buf += nwr;
		to_write -= nwr;
	}
	return count - to_write;
}


ssize_t
nwritev(int fd, struct iovec *iov, int iovcnt)
{
	assert(iov && iovcnt >= 0);
	ssize_t nwr;
restart:
	nwr = writev(fd, iov, iovcnt);
	if (nwr < 0) {
		if (errno == EINTR) {
			errno = 0;
			goto restart;
		}
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			say_syserror("writev, [%s]", nfilename(fd));
	}
	return nwr;
}

off_t
nlseek(int fd, off_t offset, int whence)
{
	off_t effective_offset = lseek(fd, offset, whence);

	if (effective_offset == -1) {
		say_syserror("lseek, offset=%"PRI_OFFT", whence=%d, [%s]",
			     (u64) offset, whence,
			     nfilename(fd));
	} else if (whence == SEEK_SET && effective_offset != offset) {
		say_error("lseek, offset set to unexpected value: "
			  "requested %"PRI_OFFT", effective %"PRI_OFFT", "
			  "[%s]",
			  offset, effective_offset, nfilename(fd));
	}
	return effective_offset;
}

struct nbatch *
nbatch_alloc(long max_iov)
{
	struct nbatch *batch = (struct nbatch *)
		malloc(sizeof(struct nbatch) +
		       sizeof(struct iovec) * max_iov);
	if (batch == NULL)
		return NULL;
	batch->bytes = batch->rows = batch->max_rows = 0;
	batch->max_iov = max_iov;
	return batch;
}

void
nbatch_start(struct nbatch *batch, long max_rows)
{
	batch->bytes = batch->rows = 0;
	batch->max_rows = max_rows;
}

void
nbatch_add(struct nbatch *batch, void *row, ssize_t row_len)
{
	assert(batch->max_rows > 0);
	assert(! nbatch_is_full(batch));

	batch->iov[batch->rows].iov_base = row;
	batch->iov[batch->rows].iov_len = row_len;
	batch->rows++;
	batch->bytes += row_len;
}

int
nbatch_write(struct nbatch *batch, int fd)
{
	ssize_t bytes_written = nwritev(fd, batch->iov, batch->rows);
	if (bytes_written <= 0)
		return 0;

	if (bytes_written == batch->bytes)
		return batch->rows;

	ssize_t good_bytes = 0;
	struct iovec *iov = batch->iov;
	while (iov < batch->iov + batch->rows) {
		if (good_bytes + iov->iov_len > bytes_written)
			break;
		good_bytes += iov->iov_len;
		iov++;
	}
	/*
	 * Unwind file position back to ensure we do not leave
	 * partially written rows.
	 */
	nlseek(fd, good_bytes - bytes_written, SEEK_CUR);
	return iov - batch->iov;
}
