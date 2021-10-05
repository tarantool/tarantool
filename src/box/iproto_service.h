#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
struct iproto_service_array;

struct iproto_service_array *
iproto_service_array_new(void);

void
iproto_service_array_delete(struct iproto_service_array *array);

void
iproto_service_array_init(struct iproto_service_array *array,
			  evio_accept_f on_accept,
			  void *on_accept_param);

const char *
iproto_service_array_fill_listen_info(struct iproto_service_array *array,
				      char *buf);

void
iproto_service_array_attach(struct iproto_service_array *dst,
			    const struct iproto_service_array *src);

void
iproto_service_array_detach(struct iproto_service_array *array);

void
iproto_service_array_check_listen(struct iproto_service_array *array);

void
iproto_service_array_start_listen(struct iproto_service_array *array);

void
iproto_service_array_stop_listen(struct iproto_service_array *array);

int
iproto_service_array_bind(struct iproto_service_array *array,
			  const struct  cfg_uri_array *uri_array);
