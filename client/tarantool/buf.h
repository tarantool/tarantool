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

#define TC_BUF_INIT_SIZE 4096
#define TC_BUF_MULTIPLIER 2

size_t strip_end_ws(char *str);

struct tc_buf {
	size_t size;
	size_t used;
	char *data;
};

int tc_buf(struct tc_buf *buf);
void *tc_buf_realloc(void *data, size_t size);
int tc_buf_append(struct tc_buf *buf, void *str, size_t len);
size_t tc_buf_delete(struct tc_buf *buf, size_t num);
int tc_buf_isempty(struct tc_buf *buf);
void tc_buf_clear(struct tc_buf *buf);
void tc_buf_free(struct tc_buf *buf);

int tc_buf_str(struct tc_buf *buf);
int tc_buf_str_append(struct tc_buf *buf, char *str, size_t len);
size_t tc_buf_str_delete(struct tc_buf *buf, size_t num);
int tc_buf_str_stripws(struct tc_buf *buf);
int tc_buf_str_isempty(struct tc_buf *buf);

void tc_buf_cmdfy(struct tc_buf *buf, size_t num);
