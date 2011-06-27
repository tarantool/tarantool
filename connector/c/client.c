/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <connector/c/client.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/*
 * Please keep as many data structures defined in .c as possible
 * to increase forward ABI compatibility.
 */


struct tnt_connection
{
	/** The socket used to get connected to the server. */
	int data_port;
};


/** A helper to get the DNS resolution done.
 */

static
struct sockaddr_in
get_sockaddr_in(const char *hostname, unsigned short port)
{
	struct sockaddr_in result;

	memset((void*)(&result), 0, sizeof(result));
	result.sin_family = AF_INET;
	result.sin_port   = htons(port);

	/* @todo: start using gethostbyname_r */
	struct hostent *host = gethostbyname(hostname);
	if (host != 0)
		memcpy((void*)(&result.sin_addr),
		       (void*)(host->h_addr), host->h_length);

	return result;
}


struct tnt_connection *tnt_connect(const char *hostname, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return NULL;

	/*
	 * We set TCP_NODELAY since we're not strictly
	 * request/response.
         */
	int opt = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
		return NULL;

	struct sockaddr_in addr = get_sockaddr_in(hostname, port);
	if (connect(fd, (struct sockaddr*)&addr, sizeof addr))
		return NULL;

	struct tnt_connection *tnt = malloc(sizeof(struct tnt_connection));
	if (tnt == NULL) {
		close(fd);
		return NULL;
	}
	tnt->data_port = fd;
	return tnt;
}


void tnt_disconnect(struct tnt_connection *tnt)
{
	close(tnt->data_port);
	free(tnt);
}


/** Send the binary blob message to the server, read the response
 * and learn as much from it as possible.
 *
 * @return 0 on success, 1 on error.
 */

int tnt_execute_raw(struct tnt_connection *tnt, const char *message,
		    size_t len, struct tnt_result *tnt_res)
{
	if (send(tnt->data_port, message, len, 0) < 0)
		return -1;

	static char buf[2048];

	if (recv(tnt->data_port, buf, 2048, 0) < 16)
		return -1;

	if (tnt_res) {
		memset(tnt_res, 0, sizeof *tnt_res);

		/* @fixme: we may want to support big-endian some
		 * day. */
		tnt_res->errcode = * (uint32_t*) (buf+12); /* see iproto.h */
		if (tnt_res->errcode)
			tnt_res->errmsg = (const char *)(buf + 16);
		else
			tnt_res->errmsg = "";
	}
	return 0;
}
