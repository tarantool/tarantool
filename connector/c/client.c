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

static
struct sockaddr_in
get_sockaddr_in(const char *hostname, unsigned short port) {
  struct sockaddr_in result;

  memset((void*)(&result), 0, sizeof(result));
  result.sin_family = AF_INET;
  result.sin_port   = htons(port);

  struct hostent *host = gethostbyname(hostname);
  if (host != 0)
	  memcpy((void*)(&result.sin_addr),
		 (void*)(host->h_addr), host->h_length);

  return result;
}

struct tnt_connection *tnt_connect(const char *hostname, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
	  return NULL;

  int opt = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
	  return NULL;

  struct sockaddr_in addr = get_sockaddr_in(hostname, port);
  if (connect(fd, (struct sockaddr*)&addr, sizeof addr))
	  return NULL;

  struct tnt_connection *conn = malloc(sizeof(struct tnt_connection));
  conn->data_port = fd;
  return conn;
}

void tnt_disconnect(struct tnt_connection *conn) {
  close(conn->data_port);
  free(conn);
}

int tnt_execute_raw(struct tnt_connection *conn, const char *message,
		    size_t len) {
  if (send(conn->data_port, message, len, 0) < 0)
	  return 3;

  char buf[2048];
  if (recv(conn->data_port, buf, 2048, 0) < 16)
	  return 3;

  int ret_code = buf[12];
  int b = 256;
  int i = 13;
  while (i < 16) {
	  ret_code += (buf[i++] * b);
	  b *= 256;
  }
  return ret_code; // see iproto.h
}
