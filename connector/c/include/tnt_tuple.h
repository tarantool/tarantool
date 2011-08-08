#ifndef TNT_TUPLE_H_INCLUDED
#define TNT_TUPLE_H_INCLUDED

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
 * @defgroup Tuple
 * @brief Operations on Tuple and Tuples objects
 */

struct tnt_tuple_field {
	char *data;
	uint32_t size;
	uint32_t size_leb;
	STAILQ_ENTRY(tnt_tuple_field) next;
};

#define TNT_TUPLE_FIELD_SIZE(F) \
	((F)->size)
#define TNT_TUPLE_FIELD_DATA(F) \
	((F)->data)

struct tnt_tuple {
	uint32_t count;
	uint32_t size_enc;
	STAILQ_HEAD(,tnt_tuple_field) list;
	STAILQ_ENTRY(tnt_tuple) next;
};

#define TNT_TUPLE_COUNT(T) \
	((T)->count)
#define TNT_TUPLE_FOREACH(T, TI) \
	STAILQ_FOREACH(TI, &(T)->list, next)
#define TNT_TUPLE_LAST(T, TI) \
	(STAILQ_NEXT((TI), next) == NULL)

struct tnt_tuples {
	uint32_t count;
	STAILQ_HEAD(,tnt_tuple) list;
};

#define TNT_TUPLES_FOREACH(TS, TSI) \
	STAILQ_FOREACH(TSI, &(TS)->list, next)

/** @addtogroup Tuple
 *  @{
 */

/**
 * Initizalizes tuple object with specified count of fields
 *
 * @param tuple tuple object pointer
 * @param fields number of fields
 */
void tnt_tuple_init(struct tnt_tuple *tuple);

/**
 * Frees tuple object
 *
 * @param tuple tuple object pointer
 */
void tnt_tuple_free(struct tnt_tuple *tuple);

/**
 * Adds tuple field data
 *
 * @param tuple tuple object pointer
 * @param data data pointer
 * @param size data size
 * @returns tuple field on success, NULL on error
 */

struct tnt_tuple_field*
tnt_tuple_add(struct tnt_tuple *tuple, char *data, unsigned int size);

/**
 * Gets tuple field data
 *
 * @param tuple tuple object pointer
 * @param field field number
 * @returns field data pointer
 */
struct tnt_tuple_field*
tnt_tuple_get(struct tnt_tuple *tuple, unsigned int field);

/**
 * Adds formated tuple data field's
 *
 * @param tuple tuple object pointer
 * @param fmt printf-alike format (%s, %*s, %d, %l, %ll, %ul, %ull are supported)
 * @returns 0 on success, -1 on error
 */
int
tnt_tuplef(struct tnt_tuple *tuple, char *fmt, ...)
           __attribute__ ((format (printf, 2, 3)));
/** @} */

enum tnt_error
tnt_tuple_pack(struct tnt_tuple *tuple, char **data,
	       unsigned int *size);

enum tnt_error
tnt_tuple_pack_to(struct tnt_tuple *tuple, char *dest);

/** @addtogroup Tuple
 *  @{
 */

/**
 * Initializes tuples object
 *
 * @param tuples tuples object pointer
 */
void tnt_tuples_init(struct tnt_tuples *tuples);

/**
 * Frees tuples object including all allocated tuples
 *
 * @param tuples tuples object pointer
 */
void tnt_tuples_free(struct tnt_tuples *tuples);

/**
 * Allocate new tuple object
 *
 * @param tuples tuples object pointer
 */
struct tnt_tuple*
tnt_tuples_add(struct tnt_tuples *tuples);
/** @} */

enum tnt_error
tnt_tuples_pack(struct tnt_tuples *tuples, char **data,
		unsigned int *size);

enum tnt_error
tnt_tuples_unpack(struct tnt_tuples *tuples, char *data,
		  unsigned int size);

#endif /* TNT_TUPLE_H_INCLUDED */
