/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ssl.h"

#include <assert.h>
#include <errno.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "diag.h"
#include "iostream.h"
#include "say.h"
#include "sio.h"
#include "ssl_error.h"
#include "trivia/util.h"
#include "uri/uri.h"

/** SSL connection iostream methods. */
static const struct iostream_vtab ssl_iostream_vtab;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
static OSSL_PROVIDER *legacy_provider = NULL;
static OSSL_PROVIDER *default_provider = NULL;
#endif

#if defined(EMBED_GOST_ENGINE)
extern void
ENGINE_load_gost(void);
#else
static inline void
ENGINE_load_gost(void) {}
#endif /* EMBED_GOST_ENGINE */

/** Used by Lua FFI. */
long
ssl_openssl_version_number(void)
{
	return OPENSSL_VERSION_NUMBER;
}

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
/**
 * Create an alias for SSL_get_peer_certificate(), which got renamed to
 * SSL_get1_peer_certificate() in OpenSSL 3.0. We should export both symbols for
 * the sake of backward compatibility. ssl.h contains a likewise define, remove
 * it.
 */

#undef SSL_get_peer_certificate

X509 *
SSL_get_peer_certificate(const SSL *s)
{
	return SSL_get1_peer_certificate(s);
}
#endif

void
ssl_init(void)
{
	/* NB: GOST engine must be loaded before OpenSSL initialization. */
	ENGINE_load_gost();
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	OpenSSL_add_all_digests();
	OpenSSL_add_all_ciphers();
	ERR_load_crypto_strings();
#else
	OPENSSL_init_crypto(0, NULL);
	OPENSSL_init_ssl(0, NULL);
#endif
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	/* Needed to enable legacy algorithms, such as MD4. */
	legacy_provider = OSSL_PROVIDER_load(NULL, "legacy");
	if (legacy_provider == NULL)
		say_error("cannot load the Legacy OpenSSL provider");
	default_provider = OSSL_PROVIDER_load(NULL, "default");
	if (default_provider == NULL)
		say_error("cannot load the Default OpenSSL provider");
#endif
}

void
ssl_free(void)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	if (legacy_provider != NULL)
		OSSL_PROVIDER_unload(legacy_provider);
	if (default_provider != NULL)
		OSSL_PROVIDER_unload(default_provider);
#endif
#ifdef OPENSSL_cleanup
	OPENSSL_cleanup();
#endif
}

/**
 * Dummy callback passed to SSL_CTX_set_default_passwd_cb.
 * Used to disable command-line prompt.
 */
static int
dummy_passwd_cb(char *buf, int size, int rwflag, void *u)
{
	/* Pass phrase protected keys are not supported yet. */
	(void)buf;
	(void)size;
	(void)rwflag;
	(void)u;
	return 0;
}

/**
 * Loads SSL private key and returns 0 on success or -1 with diag on error.
 *
 * The private key file may be encrypted. This function tries to decrypt
 * the key using passwords in the following order:
 *  1. String stored in the passwd argument. Skipped if passwd is NULL.
 *  2. Every line from the file specified by the passwd_file argument.
 *     Skipped if passwd_file is NULL.
 *  3. Empty password.
 */
static int
load_private_key(SSL_CTX *ssl_ctx, const char *key_file,
		 const char *passwd, const char *passwd_file)
{
	/*
	 * Set the password callback to NULL to make the SSL library use
	 * the callback userdata for a password.
	 */
	SSL_CTX_set_default_passwd_cb(ssl_ctx, NULL);

	if (passwd != NULL) {
		/*
		 * Try to load the key file using the password specified
		 * in the passwd argument.
		 */
		SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, (void *)passwd);
		int ret = SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file,
						      SSL_FILETYPE_PEM);
		SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, NULL);
		if (ret == 1)
			return 0;
	}
	if (passwd_file != NULL) {
		/*
		 * Try to load the key file using every password stored in
		 * the password file.
		 */
		FILE *f = fopen(passwd_file, "r");
		if (f == NULL) {
			diag_set(SystemError,
				 "Error reading SSL password file '%s'",
				 passwd_file);
			return -1;
		}
		char *buf = NULL;
		size_t buf_size = 0;
		bool is_error = false;
		bool is_loaded = false;
		while (true) {
			/* Read a line from the password file. */
			errno = 0;
			ssize_t len = getline(&buf, &buf_size, f);
			if (len <= 0) {
				if (errno == 0)
					break; /* EOF */
				diag_set(SystemError,
					 "Error reading SSL password file '%s'",
					 passwd_file);
				is_error = true;
				break;
			}
			char *s = buf;
			/* Trim a terminating new line. */
			if (s[len - 1] == '\n')
				s[len - 1] = '\0';
			/* Try to load the key file using the password. */
			SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, s);
			int ret = SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file,
							      SSL_FILETYPE_PEM);
			SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, NULL);
			if (ret == 1) {
				is_loaded = true;
				break;
			}
			/* Ignore the error and try another password. */
			ERR_clear_error();
		}
		free(buf);
		fclose(f);
		if (is_loaded)
			return 0;
		if (is_error)
			return -1;
	}
	/* Try to load the key file without a password. */
	SSL_CTX_set_default_passwd_cb(ssl_ctx, dummy_passwd_cb);
	int ret = SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file,
					      SSL_FILETYPE_PEM);
	SSL_CTX_set_default_passwd_cb(ssl_ctx, NULL);
	if (ret != 1) {
		diag_set(SSLError, "Error loading SSL private key '%s'",
			 key_file);
		return -1;
	}
	return 0;
}

struct ssl_iostream_ctx *
ssl_iostream_ctx_new(enum iostream_mode mode, const struct uri *uri)
{
	const char *cert_file = uri_param(uri, "ssl_cert_file", 0);
	const char *key_file = uri_param(uri, "ssl_key_file", 0);
	const char *ca_file = uri_param(uri, "ssl_ca_file", 0);
	const char *ciphers = uri_param(uri, "ssl_ciphers", 0);
	const char *passwd = uri_param(uri, "ssl_password", 0);
	const char *passwd_file = uri_param(uri, "ssl_password_file", 0);
	if (mode == IOSTREAM_SERVER && cert_file == NULL) {
		diag_set(IllegalParams, "SSL certificate missing");
		goto err;
	}
	if (mode == IOSTREAM_SERVER && key_file == NULL) {
		diag_set(IllegalParams, "SSL private key missing");
		goto err;
	}
	const SSL_METHOD *method;
	if (mode == IOSTREAM_SERVER) {
		method = TLS_server_method();
	} else {
		assert(mode == IOSTREAM_CLIENT);
		method = TLS_client_method();
	}
	SSL_CTX *ssl_ctx = SSL_CTX_new(method);
	if (ssl_ctx == NULL) {
		diag_set(SSLError, "SSL_CTX_new");
		goto err;
	}
	/*
	 * Require TLSv1.2, because other protocol versions don't seem to
	 * support the GOST cipher:
	 *
	 *   $ openssl ciphers -s -tls1_2 | tr ':' '\n' | grep GOST
	 *
	 * (Should we add a configuration parameter for this?)
	 */
	if (SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION) != 1 ||
	    SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_2_VERSION) != 1) {
		diag_set(SSLError, "Error setting SSL protocol version");
		goto err_ctx;
	}
	if (cert_file != NULL &&
	    SSL_CTX_use_certificate_file(ssl_ctx, cert_file,
					 SSL_FILETYPE_PEM) != 1) {
		diag_set(SSLError, "Error loading SSL certificate '%s'",
			 cert_file);
		goto err_ctx;
	}
	if (key_file != NULL &&
	    load_private_key(ssl_ctx, key_file, passwd, passwd_file) != 0) {
		goto err_ctx;
	}
	if (ca_file != NULL &&
	    SSL_CTX_load_verify_locations(ssl_ctx, ca_file, NULL) != 1) {
		diag_set(SSLError, "Error loading SSL CA '%s'", ca_file);
		goto err_ctx;
	}
	if (ca_file != NULL) {
		SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER |
				   SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
	}
	/*
	 * NB: SSL_CTX_set_cipher_list() only works for procol versions TLSv1.2
	 * and below. For TLSv1.3 we'd have to use SSL_CTX_set_ciphersuites()
	 * instead.
	 */
	if (ciphers != NULL &&
	    SSL_CTX_set_cipher_list(ssl_ctx, ciphers) != 1) {
		diag_set(SSLError, "Error setting SSL ciphers '%s'", ciphers);
		goto err_ctx;
	}
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
	/* Supported since 3.0 */
	SSL_CTX_set_options(ssl_ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
	return (struct ssl_iostream_ctx *)ssl_ctx;
err_ctx:
	SSL_CTX_free(ssl_ctx);
err:
	return NULL;
}

struct ssl_iostream_ctx *
ssl_iostream_ctx_dup(struct ssl_iostream_ctx *ssl_ctx_arg)
{
	SSL_CTX *ssl_ctx = (SSL_CTX *)ssl_ctx_arg;
	/*
	 * We bump the SSL_CTX reference counter instead of copying it.
	 * It's okay because SSL_CTX isn't modified after construction.
	 */
	if (ssl_ctx != NULL && SSL_CTX_up_ref(ssl_ctx) != 1)
		panic("Unexpected SSL_CTX_up_ref error");
	return (struct ssl_iostream_ctx *)ssl_ctx;
}

void
ssl_iostream_ctx_delete(struct ssl_iostream_ctx *ssl_ctx_arg)
{
	SSL_CTX *ssl_ctx = (SSL_CTX *)ssl_ctx_arg;
	SSL_CTX_free(ssl_ctx);
}

int
ssl_iostream_create(struct iostream *io, int fd, enum iostream_mode mode,
		    const struct ssl_iostream_ctx *ssl_ctx_arg)
{
	iostream_clear(io);
	SSL_CTX *ssl_ctx = (SSL_CTX *)ssl_ctx_arg;
	SSL *ssl = SSL_new(ssl_ctx);
	if (ssl == NULL) {
		diag_set(SSLError, "SSL_new");
		return -1;
	}
	if (SSL_set_fd(ssl, fd) != 1) {
		diag_set(SSLError, "SSL_set_fd");
		SSL_free(ssl);
		return -1;
	}
	if (mode == IOSTREAM_SERVER) {
		SSL_set_accept_state(ssl);
	} else {
		assert(mode == IOSTREAM_CLIENT);
		SSL_set_connect_state(ssl);
	}
	assert(fd >= 0);
	io->fd = fd;
	io->flags = IOSTREAM_IS_ENCRYPTED;
	io->data = ssl;
	io->vtab = &ssl_iostream_vtab;
	return 0;
}

/** iostrem_vtab::destroy */
static void
ssl_iostream_destroy(struct iostream *io)
{
	SSL *ssl = io->data;
	SSL_free(ssl);
}

/** iostrem_vtab::read */
static ssize_t
ssl_iostream_read(struct iostream *io, void *buf, size_t count)
{
	SSL *ssl = io->data;
	errno = 0;
	size_t n_read;
	int ret = SSL_read_ex(ssl, buf, count, &n_read);
	if (ret == 1)
		return n_read;
	int err = SSL_get_error(ssl, ret);
	switch (err) {
	case SSL_ERROR_ZERO_RETURN:
		return 0;
	case SSL_ERROR_WANT_READ:
		return IOSTREAM_WANT_READ;
	case SSL_ERROR_WANT_WRITE:
		return IOSTREAM_WANT_WRITE;
	case SSL_ERROR_SSL:
		diag_set(SSLError, "SSL_read(%zd), called on %s",
			 count, sio_socketname(io->fd));
		return IOSTREAM_ERROR;
	default:
		assert(err == SSL_ERROR_SYSCALL);
		if (errno == 0) {
			/*
			 * The remote end closed the socket for writing.
			 * The OpenSSL library treats this situation as
			 * a system error with errno = 0. We ignore it.
			 */
			return 0;
		}
		diag_set(SocketError, sio_socketname(io->fd),
			 "SSL_read(%zd)", count);
		return IOSTREAM_ERROR;
	}
}

/** iostrem_vtab::write */
static ssize_t
ssl_iostream_write(struct iostream *io, const void *buf, size_t count)
{
	SSL *ssl = io->data;
	size_t n_written;
	int ret = SSL_write_ex(ssl, buf, count, &n_written);
	if (ret == 1)
		return n_written;
	int err = SSL_get_error(ssl, ret);
	switch (err) {
	case SSL_ERROR_WANT_READ:
		return IOSTREAM_WANT_READ;
	case SSL_ERROR_WANT_WRITE:
		return IOSTREAM_WANT_WRITE;
	/*
	 * With SSL_OP_IGNORE_UNEXPECTED_EOF option writing to peer
	 * that does not support SSL returns SSL_ERROR_ZERO_RETURN.
	 */
	case SSL_ERROR_ZERO_RETURN:
	case SSL_ERROR_SSL:
		diag_set(SSLError, "SSL_write(%zd), called on %s",
			 count, sio_socketname(io->fd));
		return IOSTREAM_ERROR;
	default:
		assert(err == SSL_ERROR_SYSCALL);
		if (errno == 0) {
			/*
			 * The remote end closed the socket for reading.
			 * The OpenSSL library treats this situation as
			 * a system error with errno = 0. We report it
			 * as EPIPE.
			 */
			errno = EPIPE;
		}
		diag_set(SocketError, sio_socketname(io->fd),
			 "SSL_write(%zd)", count);
		return IOSTREAM_ERROR;
	}
}

/** iostrem_vtab::writev */
static ssize_t
ssl_iostream_writev(struct iostream *io, const struct iovec *iov, int iovcnt)
{
	size_t n_written = 0;
	for (int i = 0; i < iovcnt; i++) {
		ssize_t ret = ssl_iostream_write(io, iov[i].iov_base,
						 iov[i].iov_len);
		if (ret < 0) {
			if (n_written > 0)
				break;
			return ret;
		}
		n_written += ret;
	}
	return n_written;
}

static const struct iostream_vtab ssl_iostream_vtab = {
	/* .destroy = */ ssl_iostream_destroy,
	/* .read = */ ssl_iostream_read,
	/* .write = */ ssl_iostream_write,
	/* .writev = */ ssl_iostream_writev,
};
