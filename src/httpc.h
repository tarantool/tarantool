#ifndef TARNATOOL_HTTPC_H_INCLUDED
#define TARANTOOL_HTTPC_H_INCLUDED 1
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <small/ibuf.h>
#include <small/region.h>
#include <small/mempool.h>
#include <tarantool_ev.h>

#include "diag.h"

#include "curl.h"

/** {{{ Environment */

typedef void CURLM;
typedef void CURL;
struct curl_slist;

/**
 * HTTP Client Statistics
 */
struct httpc_stat {
	uint64_t total_requests;
	uint64_t http_200_responses;
	uint64_t http_other_responses;
	uint64_t failed_requests;
	uint64_t active_requests;
};

/**
 * HTTP Client Environment
 */
struct httpc_env {
	/** Number of a current active http requests. */
	int req_count;
	/** Flag that triggers cleanup. */
	int cleanup;
	/** Curl environment. */
	struct curl_env curl_env;
	/** Memory pool for requests */
	struct mempool req_pool;
	/** Statistics */
	struct httpc_stat stat;
};

/**
 * @brief Creates  new HTTP client environment
 * @param env pointer to a structure to initialize
 * @param max_conn The maximum number of entries in connection cache
 * @retval 0 on success
 * @retval -1 on error, check diag
 */
int
httpc_env_create(struct httpc_env *ctx, int max_conns, int max_total_conns);

/**
 * Finish HTTP client environment
 * @param env pointer to a structure to finish
 */
void
httpc_env_finish(struct httpc_env *env);

/**
 * Destroy HTTP client environment
 * @param env pointer to a structure to destroy
 */
void
httpc_env_destroy(struct httpc_env *env);

/** Environment }}} */

/** {{{ Request */

/**
 * HTTP request
 */
struct httpc_request {
	/** Environment */
	struct httpc_env *env;
	/** HTTP headers */
	struct curl_slist *headers;
	/** Buffer for data send. */
	struct ibuf send;
	/** curl request. */
	struct curl_request curl_request;
	/** HTTP status code */
	int status;
	/** Number of redirects that were followed. */
	int redirect_count;
	/** Error message */
	const char *reason;
	/** buffer of headers */
	struct region resp_headers;
	/** Buffer of received data. */
	struct region recv;
	/**
	 * Idle delay, in seconds, that the operating system will
	 * wait while the connection is idle before sending
	 * keepalive probes.
	 */
	long keep_alive_timeout;
	/**
	 * True when connection header must be set before
	 * execution automatically.
	 */
	bool set_connection_header;
	/**
	 * True when accept Any MIME header must be set
	 * before execution automatically.
	 */
	bool set_accept_header;
	/**
	 * True when Keep-Alive header must be set before
	 * execution automatically.
	 */
	bool set_keep_alive_header;
	/**
	 * It's the number of bytes of data in the body of the
	 * request. The value is equal to "Content-Length"
	 * header or -1 otherwise.
	 */
	long content_length;
	/** True when chunked io is enabled. */
	bool io;
	/**
	 * Additional buffer of received data to optimize
	 * chunked io memory allocations in the case of
	 * large bodies.
	 */
	struct ibuf io_recv;
	/** Condition for data chunk receive signaling. */
	struct fiber_cond io_recv_cond;
	/** True when send is activated. */
	bool io_send;
	/** True when send is already completed. */
	bool io_send_closed;
	/** Condition for data chunk send signaling. */
	struct fiber_cond io_send_cond;
	/** Condition for headers read signaling. */
	struct fiber_cond *io_headers_cond;
};

/**
 * @brief Create a new HTTP request
 * @param ctx - reference to context
 * @return a new HTTP request object
 */
struct httpc_request *
httpc_request_new(struct httpc_env *env, const char *method,
		  const char *url);

/**
 * @brief Delete HTTP request
 * @param req - reference to object
 * @details Should be called even if error in execute appeared
 */
void
httpc_request_delete(struct httpc_request *req);

/**
 * Set HTTP header
 * @param req request
 * @param fmt format string
 * @param ... format arguments
 */
int
httpc_set_header(struct httpc_request *req, const char *fmt, ...);

/**
 * Sets body of request
 * @param req request
 * @param body body
 * @param bytes sizeof body
 */
int
httpc_set_body(struct httpc_request *req, const char *body, size_t size);

/**
 * Set TCP keep-alive probing
 * @param req request
 * @param idle delay, in seconds, that the operating system will wait
 *        while the connection is idle before sending keepalive probes
 * @param interval the interval, in seconds, that the operating system
 *        will wait between sending keepalive probes
 * @details Does nothing on libcurl < 7.25.0
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_TCP_KEEPALIVE.html
 */
void
httpc_set_keepalive(struct httpc_request *req, long idle, long interval);

/**
 * Set the "low speed time" - the time that the transfer speed should be
 * below the "low speed limit" for the library to consider it too slow and
 * abort.
 * @param req request
 * @param low_speed_time low speed time
 * @details If the download receives less than "low speed limit" bytes/second
 * during "low speed time" seconds, the operations is aborted.
 * You could i.e if you have a pretty high speed Connection,
 * abort if it is less than 2000 bytes/sec during 20 seconds;
 * @see httpc_set_low_speed_limit()
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_LOW_SPEED_TIME.html
 */
void
httpc_set_low_speed_time(struct httpc_request *req, long low_speed_time);

/**
 * Set the "low speed limit" - the average transfer speed in bytes per second
 * that the transfer should be below during "low speed time" seconds for the
 * library to consider it to be too slow and abort.
 * @param req request
 * @param low_speed_limit low speed limit
 * @details If the download receives less than "low speed limit" bytes/second
 * during "low speed time" seconds, the operations is aborted.
 * You could i.e if you have a pretty high speed Connection,
 * abort if it is less than 2000 bytes/sec during 20 seconds.
 * @see httpc_set_low_speed_time()
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_LOW_SPEED_LIMIT.html
 */
void
httpc_set_low_speed_limit(struct httpc_request *req, long low_speed_limit);

/**
 * Enables/Disables libcurl verbose mode
 * @param req request
 * @param verbose flag
 */
void
httpc_set_verbose(struct httpc_request *req, bool verbose);

/**
 * Specify directory holding CA certificates
 * @param req request
 * @param ca_path path to directory holding one or more certificates
 * to verify the peer with. The application does not have to keep the string
 * around after setting this option.
 */
void
httpc_set_ca_path(struct httpc_request *req, const char *ca_path);

/**
 * Specify path to Certificate Authority (CA) bundle
 * @param req request
 * @param ca_file - File holding one or more certificates
 * to verify the peer with. The application does not have to keep the string
 * around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_CAINFO.html
 */
void
httpc_set_ca_file(struct httpc_request *req, const char *ca_file);

/**
 * Specify path to Unix domain socket
 * @param req request
 * @param unix_socket path to Unix domain socket used as connection
 * endpoint instead of TCP. The application does not have to keep the string
 * around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_UNIX_SOCKET_PATH.html
 * @return 0 on success
 */
int
httpc_set_unix_socket(struct httpc_request *req, const char *unix_socket);

/**
 * Enables/disables verification of the certificate's name (CN) against host
 * @param req request
 * @param verify flag
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html
 */
void
httpc_set_verify_host(struct httpc_request *req, long verify);

/**
 * Enables/disables verification of the peer's SSL certificate
 * @param req request
 * @param verify flag
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
 */
void
httpc_set_verify_peer(struct httpc_request *req, long verify);

/**
 * Specify path to private key for TLS ans SSL client certificate
 * @param req request
 * @param ssl_key - path to the private key. The application does not have to
 * keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSLKEY.html
 */
void
httpc_set_ssl_key(struct httpc_request *req, const char *ssl_key);

/**
 * Specify path to SSL client certificate
 * @param req request
 * @param ssl_cert - path to the client certificate. The application does not
 * have to keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_SSLCERT.html
 */
void
httpc_set_ssl_cert(struct httpc_request *req, const char *ssl_cert);

/**
 * Specify a proxy to use (optionally may be prefixed with a scheme -
 * e.g. http:// or https://).
 *
 * If this option is not set a value from the corresponding
 * environment variable will be used. Environment variable names are:
 * 'http_proxy', 'https_proxy', 'ftp_proxy' etc. 'all_proxy' variable
 * is used if no protocol specific proxy was set.
 *
 * Setting this option to an empty string will explicitly disable the
 * use of a proxy, even if there is an environment variable set for it.
 *
 * @param req request
 * @param proxy - a host name or an IP address. The application does not
 * have to keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html
 */
void
httpc_set_proxy(struct httpc_request *req, const char *proxy);

/**
 * Specify a port number the proxy listens on
 * @param req request
 * @param port - a port number the proxy listens on
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_PROXYPORT.html
 */
void
httpc_set_proxy_port(struct httpc_request *req, long port);

/**
 * Specify a user name and a password to use in authentication
 * @param req request
 * @param user_pwd - a login details string for the connection.
 * The format is: [user name]:[password]. The application does not
 * have to keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_USERPWD.html
 */
void
httpc_set_proxy_user_pwd(struct httpc_request *req, const char *user_pwd);

/**
 * Specify a comma separated list of host names that do not require a proxy
 * to get reached, even if one is specified by 'proxy' option. The only
 * wildcard available is a single * character, which matches all hosts, and
 * effectively disables the proxy.
 *
 * 'no_proxy' environment variable will be used if this option is not set.
 *
 * Setting this option to an empty string will
 * explicitly enable the proxy for all host names, even if there is an
 * environment variable set for it.
 *
 * @param req request
 * @param no_proxy - a comma separated list of host names. The application
 * does not have to keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_NOPROXY.html
 */
void
httpc_set_no_proxy(struct httpc_request *req, const char *no_proxy);

/**
 * Specify source interface for outgoing traffic
 * @param req request
 * @param interface - interface name to use as outgoing network interface.
 * The name can be an interface name, an IP address, or a host name.
 * The application does not have to keep the string around after setting this option.
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_INTERFACE.html
 */
void
httpc_set_interface(struct httpc_request *req, const char *interface);

/**
 * Specify whether the client will follow 'Location' header that
 * a server sends as part of an 3xx response.
 * @param req request
 * @param follow flag
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_FOLLOWLOCATION.html
 */
void
httpc_set_follow_location(struct httpc_request *req, long follow);

/**
 * Enable automatic decompression of HTTP responses: set the
 * contents of the Accept-Encoding header sent in a HTTP request
 * and enable decoding of a response when a Content-Encoding
 * header is received.
 *
 * This is a request, not an order; the server may or may not do
 * it. Servers might respond with Content-Encoding even without
 * getting an Accept-Encoding in the request. Servers might
 * respond with a different Content-Encoding than what was asked
 * for in the request.
 *
 * @param req request
 * @param encoding - specify what encoding you'd like. This param
 * can be an empty string which means Accept-Encoding header will
 * contain all built-in supported encodings. This param can be
 * comma-separated list of accepted encodings, like:
 * "br, gzip, deflate".
 *
 * Bundled libcurl supports "identity", meaning non-compressed,
 * "deflate" which requests the server to compress its response
 * using the zlib algorithm and "gzip" which requests the gzip
 * algorithm. System libcurl also possibly supports "br" which
 * is brotli.
 *
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_ACCEPT_ENCODING.html
 */
void
httpc_set_accept_encoding(struct httpc_request *req, const char *encoding);

/**
 * Enable a chunked io interface for the request. It allows to
 * send and receive data via chunks.
 *
 * @param req    Request.
 * @param method HTTP request method name.
 * @retval  0 On success.
 * @retval -1 On error.
 * @see https://en.wikipedia.org/wiki/Chunked_transfer_encoding
 * @see https://curl.haxx.se/libcurl/c/CURLOPT_UPLOAD.html
 */
int
httpc_set_io(struct httpc_request *req, const char *method);

/**
 * Read a next chunk of data.
 *
 * @param req     Reference to request object with enabled io.
 * @param buf     Reference to a data buffer.
 * @param len     Max data length to read to the buffer.
 * @param timeout Timeout of waiting for data receive.
 * @retval >0 The number of bytes read.
 * @retval  0 The request is completed.
 * @retval -1 Error.
 */
ssize_t
httpc_request_io_read(struct httpc_request *req, char *buf, size_t len,
		      double timeout);

/**
 * Write chunk of data.
 *
 * @param req     Reference to request object with enabled io.
 * @param ptr     Reference to a data buffer.
 * @param len     Data length to send from the buffer, 0 is the
 * indicator of the end of sending data.
 * @param timeout Timeout of waiting for data send.
 * @retval >0 The number of bytes written.
 * @retval  0 The request is completed.
 * @retval -1 Error.
 */
ssize_t
httpc_request_io_write(struct httpc_request *req, const char *ptr, size_t len,
		       double timeout);

/**
 * Finish io request.
 *
 * @param req     Reference to request object with enabled io.
 * @param timeout Timeout of waiting for libcurl api.
 * @retval  0 On success.
 * @retval -1 On error.
 */
int
httpc_request_io_finish(struct httpc_request *req, double timeout);

/**
 * This function starts async HTTP request.
 *
 * @param req     Reference to request object with filled fields.
 * @param timeout Timeout of waiting for server headers in case
 * of io interface.
 * @retval  0 On success.
 * @retval -1 On error.
 */
int
httpc_request_start(struct httpc_request *req, double timeout);

/**
 * This function waits for the CURL request to be completed or
 * aborts the request by timeout.
 *
 * @param req     Reference to request object with filled fields.
 * @param timeout Timeout of waiting for libcurl api.
 * @retval  0 On success.
 * @retval -1 On error.
 */
int
httpc_request_finish(struct httpc_request *req, double timeout);

/**
 * This function does async HTTP request
 * @param req     - reference to request object with filled fields
 * @param timeout - timeout of waiting for libcurl api
 * @return 0 for success or NULL
 */
int
httpc_execute(struct httpc_request *req, double timeout);

/** Request }}} */

#endif /* TARANTOOL_HTTPC_H_INCLUDED */
