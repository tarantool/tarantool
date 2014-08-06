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

#include <disk_fiber_io.h>
#include <coeio.h>
#include <fiber.h>

struct fiber_eio {
	ssize_t result;
	int errorno;
	struct fiber *fiber;
	bool done;
};


int
dfio_complete(eio_req *req)
{
	struct fiber_eio *fio = (struct fiber_eio *)req->data;

	fio->result = req->result;
	fio->errorno = req->errorno;
	fio->done = true;

	fiber_wakeup(fio->fiber);
	return 0;
}

static ssize_t
dfio_wait_done(eio_req *req, struct fiber_eio *eio)
{
	if (!req) {
		errno = ENOMEM;
		return -1;
	}

	while (!eio->done)
		fiber_yield();
	errno = eio->errorno;

	say_info("Done evio operation");
	return eio->result;
}

int
dfio_open(const char *path, int flags, mode_t mode)
{
	struct fiber_eio eio = { 0, 0, fiber(), false };

	eio_req *req = eio_open(path, flags, mode, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

ssize_t
dfio_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	struct fiber_eio eio = { 0, 0, fiber(), false };
	say_info("Write %s (%zu bytes)", (const char *)buf, count);
	eio_req *req = eio_write(fd,
		(void *)buf, count, offset, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

ssize_t
dfio_pread(int fd, void *buf, size_t count, off_t offset)
{
	struct fiber_eio eio = { 0, 0, fiber(), false };
	say_info("Read %s (%zu bytes)", (const char *)buf, count);
	eio_req *req = eio_read(fd, buf, count,
		offset, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}
