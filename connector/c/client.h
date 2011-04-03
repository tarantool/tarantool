#ifndef TARANTOOL_CONNECTOR_CLIENT_H_INCLUDED
# define TARANTOOL_CONNECTOR_CLIENT_H_INCLUDED
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

/*
 * C client library for tarantool.
 */

#include <stddef.h>

/**
 * A connection with a Tarantool server.
 */
struct tnt_connection {
	int data_port;
};

/**
 * Open a connection with a Tarantool server.
 *
 * @param hostname the hostname of the server.
 * @param port the port of the server.
 *
 * @return a newly opened connection or NULL if there was an error.
 */
struct tnt_connection *tnt_connect(const char *hostname, int port);

/**
 * Close a connection.
 *
 * @param conn the connection.
 */
void tnt_disconnect(struct tnt_connection *conn);

/**
 * Execute a statement on a Tarantool server.
 *
 * @param conn the connection.
 * @param message a raw message following Tarantool protocol.
 * @param len the length of the message.
 *
 * @return the return code if the statement was successufully sent
 * (see Tarantool protocol) or 3 if an error occured.
 */
int tnt_execute_raw(struct tnt_connection *conn, const char *message,
		    size_t len);

#endif /* TARANTOOL_CONNECTOR_CLIENT_H_INCLUDED */
