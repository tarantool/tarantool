#ifndef TNT_MAIN_H_INCLUDED
#define TNT_MAIN_H_INCLUDED

/*
 * Copyright (C) 2011 Mail.RU
 *
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

/**
 * @mainpage
 * 
 * libtnt - Tarantool DB client library.
 *
 */

/**
 * @defgroup Main
 * @brief Initizalization and connection
 */
struct tnt {
	struct tnt_opt opt;
	int connected;
	int fd;
	struct tnt_buf sbuf;
	struct tnt_buf rbuf;
	enum tnt_error error;
	int errno_;
};

/**
 * @defgroup Initialization
 * @ingroup  Main
 * @{
 */

/**
 * Allocates handler object 
 *
 * @returns handler pointer, or NULL on error
 */
struct tnt *tnt_alloc(void);

/**
 * Initializes handler object
 *
 * @param t handler pointer
 * @returns 0 on success, -1 on error
 */
int tnt_init(struct tnt *t);

/**
 * Frees handler object
 *
 * @param t handler pointer
 */
void tnt_free(struct tnt *t);

/**
 * Sets handler option
 *
 * @param t handler pointer
 * @param name option name
 * @returns 0 on success, -1 on error
 */
int tnt_set(struct tnt *t, enum tnt_opt_type name, ...);

/**
 * Sets memory allocator
 *
 * @param alloc allocator function pointer 
 * @returns previous allocator pointer
 */
void *tnt_set_allocator(tnt_allocator_t alloc);

/** @} */

/**
 * @defgroup Connection
 * @ingroup  Main
 * @{
 */

/**
 * Connects to server
 *
 * @param t handler pointer
 * @returns 0 on success, -1 on error
 */
int tnt_connect(struct tnt *t);

/**
 * Gets connection socket
 *
 * @param t handler pointer
 * @returns connection socket
 */
int tnt_fd(struct tnt *t);

/**
 * Sends all internal buffers to server
 *
 * @param t handler pointer
 * @returns 0 on success, -1 on error
 */
int tnt_flush(struct tnt *t);

/**
 * Closes connection to server
 *
 * @param t handler pointer
 */
void tnt_close(struct tnt *t);
/** @} */

/**
 * @defgroup Errors
 * @ingroup  Main
 * @{
 */

/**
 * Obtains library error code
 *
 * @param t handler pointer
 * @returns error code
 */
enum tnt_error tnt_error(struct tnt *t);

/**
 * Returns last saved errno value for system errors (TNT_ESYSTEM)
 *
 * @param t handler pointer
 * @returns errno
 */
int tnt_errno(struct tnt *t);

/**
 * Returns error description
 *
 * @param t handler pointer
 * @returns error description
 */
char *tnt_strerror(struct tnt *t);
/** @} */

#endif /* TNT_MAIN_H_INCLUDED */
