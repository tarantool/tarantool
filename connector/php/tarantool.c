/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2008 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Alexandre Kalendarev akalend@mail.ru                         |
  | Copyright (c) 2011                                                   |
  +----------------------------------------------------------------------+
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <inttypes.h>

#include <php.h>
#include <php_ini.h>
#include <php_network.h>
#include <ext/standard/info.h>
#include <zend_exceptions.h>

#include "tarantool.h"


/*============================================================================*
 * Tarantool extension structures defintion
 *============================================================================*/

/* I/O buffer */
struct io_buf {
	/* buffer's size */
	size_t size;
    /* buffer's capacity */
	size_t capacity;
	/* read position in the I/O buffer */
	size_t readed;
	/* buffer value */
	uint8_t *value;
};

/* tarantool object */
typedef struct tarantool_object {
	zend_object zo;
	/* host name */
	char *host;
	/* tarantool primary port */
	int port;
	/* tarantool admin port */
	int admin_port;
	/* tarantool primary connection */
	php_stream *stream;
	/* tarantool admin connecion */
	php_stream *admin_stream;
	/* I/O buffer */
	struct io_buf *io_buf;
	/* additional buffer for splice args */
	struct io_buf *splice_field;
} tarantool_object;

/* iproto header */
struct iproto_header {
	/* command code */
	uint32_t type;
	/* command length */
	uint32_t length;
	/* request id */
	uint32_t request_id;
} __attribute__((packed));

/* tarantool select command request */
struct tnt_select_request {
	/* space number */
	int32_t space_no;
	/* index number */
	int32_t index_no;
	/* select offset from begining */
	int32_t offset;
	/* maximail number tuples in responce */
	int32_t limit;
} __attribute__((packed));

/* tarantool insert command request */
struct tnt_insert_request {
	/* space number */
	int32_t space_no;
	/* flags */
	int32_t flags;
} __attribute__((packed));

/* tarantool update fields command request */
struct tnt_update_fields_request {
	/* space number */
	int32_t space_no;
	/* flags */
	int32_t flags;
} __attribute__((packed));

/* tarantool delete command request */
struct tnt_delete_request {
	/* space number */
	int32_t space_no;
	/* flags */
	int32_t flags;
} __attribute__((packed));

/* tarantool call command request */
struct tnt_call_request {
	/* flags */
	int32_t flags;
} __attribute__((packed));

/* tarantool command response */
struct tnt_response {
	/* return code */
	int32_t return_code;
	union {
		/* count */
		int32_t count;
		/* error message */
		char return_msg[0];
	};
} __attribute__((packed));


/*============================================================================*
 * Global variables definition
 *============================================================================*/


/*----------------------------------------------------------------------------*
 * Tarantool module variables
 *----------------------------------------------------------------------------*/

/* module functions list */
zend_function_entry tarantool_module_functions[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

/* tarantool module struct */
zend_module_entry tarantool_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"tarantool",
	tarantool_module_functions,
	PHP_MINIT(tarantool),
	PHP_MSHUTDOWN(tarantool),
	NULL,
	NULL,
	PHP_MINFO(tarantool),
#if ZEND_MODULE_API_NO >= 20010901
	"1.0",
#endif
	STANDARD_MODULE_PROPERTIES
};


#ifdef COMPILE_DL_TARANTOOL
ZEND_GET_MODULE(tarantool)
#endif


/*----------------------------------------------------------------------------*
 * Tarantool class variables
 *----------------------------------------------------------------------------*/

/* tarantool class methods */
const zend_function_entry tarantool_class_methods[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, update_fields, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, delete, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, admin, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

/* tarantool class */
zend_class_entry *tarantool_class_ptr;


/*============================================================================*
 * local functions declaration
 *============================================================================*/


/*----------------------------------------------------------------------------*
 * I/O buffer interface
 *----------------------------------------------------------------------------*/

/* create I/O buffer instance */
static struct io_buf *
io_buf_create();

/* destroy I/O buffer */
static void
io_buf_destroy(struct io_buf *buf);

/* reserv I/O buffer space */
inline static bool
io_buf_reserve(struct io_buf *buf, size_t n);

/* resize I/O buffer */
inline static bool
io_buf_resize(struct io_buf *buf, size_t n);

/* calculate next capacity for I/O buffer */
inline static size_t
io_buf_next_capacity(size_t n);

/* clean I/O buffer */
static void
io_buf_clean(struct io_buf *buf);

/* read struct from buffer */
static bool
io_buf_read_struct(struct io_buf *buf, void **ptr, size_t n);

/* read 32-bit integer from buffer */
static bool
io_buf_read_int32(struct io_buf *buf, int32_t *val);

/* read 64-bit integer from buffer */
static bool
io_buf_read_int64(struct io_buf *buf, int64_t *val);

/* read var integer from buffer */
static bool
io_buf_read_varint(struct io_buf *buf, int32_t *val);

/* read string from buffer */
static bool
io_buf_read_str(struct io_buf *buf, char **str, size_t len);

/* read fied from buffer */
static bool
io_buf_read_field(struct io_buf *buf, zval *tuple);

/* read tuple from buffer */
static bool
io_buf_read_tuple(struct io_buf *buf, zval **tuple);

/*
 * Write to I/O buffer functions
 */

/* write struct to I/O buffer */
static void *
io_buf_write_struct(struct io_buf *buf, size_t n);

/* write byte to I/O buffer */
static bool
io_buf_write_byte(struct io_buf *buf, int8_t value);

/* write 32-bit integer to I/O buffer */
static bool
io_buf_write_int32(struct io_buf *buf, int32_t value);

/* write 64-bit integer to I/O buffer */
static bool
io_buf_write_int64(struct io_buf *buf, int64_t value);

/* write varint to I/O buffer */
static bool
io_buf_write_varint(struct io_buf *buf, int32_t value);

/* write string to I/O buffer */
static bool
io_buf_write_str(struct io_buf *buf, uint8_t *str, size_t len);

/* write 32-bit integer as tuple's field to I/O buffer */
static bool
io_buf_write_field_int32(struct io_buf *buf, uint32_t value);

/* write 64-bit integer as tuple's field to I/O buffer */
static bool
io_buf_write_field_int64(struct io_buf *buf, uint64_t value);

/* write string tuple's field to I/O buffer */
static bool
io_buf_write_field_str(struct io_buf *buf, uint8_t *val, size_t len);

/* write tuple to I/O buffer */
static bool
io_buf_write_tuple_int(struct io_buf *buf, zval *tuple);

/* write tuple (string) to I/O buffer */
static bool
io_buf_write_tuple_str(struct io_buf *buf, zval *tuple);

/* write tuple (array) to I/O buffer */
static bool
io_buf_write_tuple_array(struct io_buf *buf, zval *tuple);

/* write tuple to I/O buffer */
static bool
io_buf_write_tuple(struct io_buf *buf, zval *tuple);

/* write array of tuples to I/O buffer */
static bool
io_buf_write_tuples_list_array(struct io_buf *buf, zval *tuples_list);

/* write tuples list to I/O buffer */
static bool
io_buf_write_tuples_list(struct io_buf *buf, zval *tuples_list);

/*
 * I/O buffer send/recv 
 */

/* send administation command request */
static bool
io_buf_send_yaml(php_stream *stream, struct io_buf *buf);

/* receive administration command response */
static bool
io_buf_recv_yaml(php_stream *stream, struct io_buf *buf);

/* send request by iproto */
static bool
io_buf_send_iproto(php_stream *stream, int32_t type, int32_t request_id, struct io_buf *buf);

/* receive response by iproto */
static bool
io_buf_recv_iproto(php_stream *stream, struct io_buf *buf);


/*----------------------------------------------------------------------------*
 * support local functions
 *----------------------------------------------------------------------------*/

/* tarantool class instance allocator */
static zend_object_value
alloc_tarantool_object(zend_class_entry *entry TSRMLS_DC);

/* free tarantool class instance */
static void
free_tarantool_object(tarantool_object *tnt TSRMLS_DC);

/* establic connection */
static php_stream *
establish_connection(char *host, int port);

/* find long by key in the hash table */
static bool
hash_fing_long(HashTable *hash, char *key, long *value);

/* find string by key in the hash table */
static bool
hash_fing_str(HashTable *hash, char *key, char **value, int *value_length);

/* find scalar by key in the hash table */
static bool
hash_fing_scalar(HashTable *hash, char *key, zval ***value);


/*============================================================================*
 * Interface definition
 *============================================================================*/


/*----------------------------------------------------------------------------*
 * Tarantool main module interface
 *----------------------------------------------------------------------------*/

/* initialize module function */
PHP_MINIT_FUNCTION(tarantool)
{
	/* register constants */

	/* register tarantool flags */
	REGISTER_LONG_CONSTANT("TARANTOOL_FLAGS_RETURN_TUPLE",
						   TARANTOOL_FLAGS_RETURN_TUPLE,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_FLAGS_ADD",
						   TARANTOOL_FLAGS_ADD,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_FLAGS_REPLACE",
						   TARANTOOL_FLAGS_REPLACE,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_FLAGS_NOT_STORE",
						   TARANTOOL_FLAGS_NOT_STORE,
						   CONST_CS | CONST_PERSISTENT);

	/* register tarantool update fields operations */
	REGISTER_LONG_CONSTANT("TARANTOOL_OP_ASSIGN",
						   TARANTOOL_OP_ASSIGN,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_OP_ADD",
						   TARANTOOL_OP_ADD,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_OP_AND",
						   TARANTOOL_OP_AND,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_OP_XOR",
						   TARANTOOL_OP_XOR,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_OP_OR",
						   TARANTOOL_OP_OR,
						   CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("TARANTOOL_OP_SPLICE",
						   TARANTOOL_OP_SPLICE,
						   CONST_CS | CONST_PERSISTENT);

	/* register classes */

	/* register tarantool class */
	zend_class_entry tarantool_class;
	INIT_CLASS_ENTRY(tarantool_class, "Tarantool", tarantool_class_methods);
	tarantool_class.create_object = alloc_tarantool_object;
	tarantool_class_ptr = zend_register_internal_class(&tarantool_class TSRMLS_CC);

	return SUCCESS;
}

/* shutdown module function */
PHP_MSHUTDOWN_FUNCTION(tarantool)
{
	return SUCCESS;
}

/* show information about this module */
PHP_MINFO_FUNCTION(tarantool)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "Tarantool/Box support", "enabled");
	php_info_print_table_row(2, "Extension version", TARANTOOL_EXTENSION_VERSION);
	php_info_print_table_end();
}


/*----------------------------------------------------------------------------*
 * Tarantool class interface
 *----------------------------------------------------------------------------*/

PHP_METHOD(tarantool_class, __construct)
{
	/*
	 * parse method's parameters
	 */
	zval *id;
	char *host = NULL;
	int host_len = 0;
	long port = 0;
	long admin_port = 0;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
									 getThis(),
									 "Osl|l",
									 &id,
									 tarantool_class_ptr,
									 &host,
									 &host_len,
									 &port,
									 &admin_port) == FAILURE) {
		return;
	}
	
	/*
	 * validate parameters
	 */

	/* check host name */
	if (host == NULL || host_len == 0) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"invalid tarantool's hostname");
		return;
	}

	/* validate port value */
	if (port <= 0 || port >= 65536) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"invalid primary port value: %li", port);
		return;
	}

	/* check admin port */
	if (admin_port) {
		/* validate port value */
		if (admin_port < 0 || admin_port >= 65536) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"invalid admin port value: %li", admin_port);
			return;
		}
	}

	/* initialzie object structure */
	tarantool_object *object = (tarantool_object *) zend_object_store_get_object(
		id TSRMLS_CC);
	object->host = estrdup(host);
	object->port = port;
	object->admin_port = admin_port;
	object->stream = NULL;
	object->admin_stream = NULL;
	object->io_buf = io_buf_create();
	if (!object->io_buf) {
		return;
	}
	object->splice_field = io_buf_create();
	if (!object->splice_field) {
		return;
	}

	return;
}

PHP_METHOD(tarantool_class, select)
{
	/* 
	 * parse methods parameters 
	 */
	zval *id;
	long space_no = 0;
	long index_no = 0;
	zval *keys_list = NULL;
	long limit = -1;
	long offset = 0;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
									 getThis(),
									 "Ollz|ll",
									 &id,
									 tarantool_class_ptr,
									 &space_no,
									 &index_no,
									 &keys_list,
									 &limit,
									 &offset) == FAILURE) {
		return;
	}

	tarantool_object *tnt = (tarantool_object *) zend_object_store_get_object(
		id TSRMLS_CC);

	/* check connection */
	if (!tnt->stream) {
		/* establis connection */
		tnt->stream = establish_connection(tnt->host, tnt->port);
		if (!tnt->stream)
			return;
	}

	/*
	 * send request
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* fill select command */
	/* fill command header */
	struct tnt_select_request *request = (struct tnt_select_request *) io_buf_write_struct(
		tnt->io_buf, sizeof(struct tnt_select_request));
	if (request == NULL)
		return;
	request->space_no = space_no;
	request->index_no = index_no;
	request->offset = offset;
	request->limit = limit;
	/* fill keys */
	if (!io_buf_write_tuples_list(tnt->io_buf, keys_list))
		return;

	/* send iproto request */
	if (!io_buf_send_iproto(tnt->stream, TARANTOOL_COMMAND_SELECT, 0, tnt->io_buf))
	  return;

	/*
	 * receive response
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* receive */
	if (!io_buf_recv_iproto(tnt->stream, tnt->io_buf))
		return;

	/* read response */
	struct tnt_response *response;
	if (!io_buf_read_struct(tnt->io_buf,
						  (void **) &response,
						  sizeof(struct tnt_response))) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"select failed: invalid response was received");
		return;
	}

	/* check return code */
	if (response->return_code) {
		/* error happen, throw exceprion */
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"select failed: %"PRIi32"(0x%08"PRIx32"): %s",
								response->return_code,
								response->return_code,
								response->return_msg);
		return;
	}

	if (array_init(return_value) != SUCCESS) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"select failed: create array failed");
		return;
	}

	/* put count to result array */
	add_assoc_long(return_value, "count", response->count);

	/* put tuple list to result array */
	zval *tuples_list;
	MAKE_STD_ZVAL(tuples_list);
	if (array_init(tuples_list) == FAILURE) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"select failed: create array failed");
		return;
	}

	/* read tuples for responce */
	int i;
	for (i = 0; i < response->count; ++i) {
		zval *tuple;
		if (!io_buf_read_tuple(tnt->io_buf, &tuple)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"select failed: invalid response was received");
			return;
		}
		add_next_index_zval(tuples_list, tuple);
	}

	add_assoc_zval(return_value, "tuples_list", tuples_list);
}

PHP_METHOD(tarantool_class, insert)
{
	/* 
	 * parse methods parameters 
	 */
	zval *id;
	long space_no = 0;
	long flags = 0;
	zval *tuple = NULL;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
									 getThis(),
									 "Ola|l",
									 &id,
									 tarantool_class_ptr,
									 &space_no,
									 &tuple,
									 &flags) == FAILURE) {
		return;
	}

	tarantool_object *tnt = (tarantool_object *) zend_object_store_get_object(
		id TSRMLS_CC);

	/* check connection */
	if (!tnt->stream) {
		/* establis connection */
		tnt->stream = establish_connection(tnt->host, tnt->port);
		if (!tnt->stream)
			return;
	}

	/*
	 * send request
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* fill insert command */
	struct tnt_insert_request *request = (struct tnt_insert_request *) io_buf_write_struct(
		tnt->io_buf, sizeof(struct tnt_insert_request));
	if (request == NULL)
		return;

	/* space number */
	request->space_no = space_no;
	/* flags */
	request->flags = flags;
	/* tuple */
	if (!io_buf_write_tuple(tnt->io_buf, tuple))
		return;

	/* send iproto request */
	if (!io_buf_send_iproto(tnt->stream, TARANTOOL_COMMAND_INSERT, 0, tnt->io_buf))
	  return;

	/*
	 * receive response
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* receive */
	if (!io_buf_recv_iproto(tnt->stream, tnt->io_buf))
		return;

	/* read response */
	struct tnt_response *response;
	if (!io_buf_read_struct(tnt->io_buf,
						  (void **) &response,
						  sizeof(struct tnt_response))) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"insert failed: invalid response was received");
		return;
	}

	/* check return code */
	if (response->return_code) {
		/* error happen, throw exceprion */
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"insert failed: %"PRIi32"(0x%08"PRIx32"): %s",
								response->return_code,
								response->return_code,
								response->return_msg);
		return;
	}
	
	/*
	 * fill return value
	 */

	if (array_init(return_value) != SUCCESS) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"insert failed: create array failed");
		return;
	}

	/* put count to result array */
	add_assoc_long(return_value, "count", response->count);	

	/* check "return tuple" flag */
	if (flags & TARANTOOL_FLAGS_RETURN_TUPLE) {
		/* ok, the responce should contain inserted tuple */
		if (!io_buf_read_tuple(tnt->io_buf, &tuple)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"insert failed: invalid response was received");
			return;
		}

		/* put returned tuple to result array */
		add_assoc_zval(return_value, "tuple", tuple);
	}
}

PHP_METHOD(tarantool_class, update_fields)
{
	/* 
	 * parse methods parameters 
	 */
	zval *id;
	long space_no = 0;
	long flags = 0;
	zval *tuple = NULL;
	zval *op_list = NULL;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
									 getThis(),
									 "Olza|l",
									 &id,
									 tarantool_class_ptr,
									 &space_no,
									 &tuple,
									 &op_list,
									 &flags) == FAILURE) {
		return;
	}

	tarantool_object *tnt = (tarantool_object *) zend_object_store_get_object(
		id TSRMLS_CC);

	/* check connection */
	if (!tnt->stream) {
		/* establis connection */
		tnt->stream = establish_connection(tnt->host, tnt->port);
		if (!tnt->stream)
			return;
	}

	/*
	 * send request
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* fill insert command */
	struct tnt_update_fields_request *request = (struct tnt_update_fields_request *) io_buf_write_struct(
		tnt->io_buf, sizeof(struct tnt_update_fields_request));
	if (request == NULL)
		return;

	/* space number */
	request->space_no = space_no;
	/* flags */
	request->flags = flags;
	/* tuple */
	if (!io_buf_write_tuple(tnt->io_buf, tuple))
		return;

	HashTable *op_list_array = Z_ARRVAL_P(op_list);
	int op_count = zend_hash_num_elements(op_list_array);

	/* write number of update fields operaion */
	if (!io_buf_write_int32(tnt->io_buf, op_count))
		return;

	HashPosition itr;
	zval **op;
	for (zend_hash_internal_pointer_reset_ex(op_list_array, &itr);
		 zend_hash_get_current_data_ex(op_list_array, (void **) &op, &itr) == SUCCESS;
		 zend_hash_move_forward_ex(op_list_array, &itr)) {
		/* check operation type */
		if (Z_TYPE_PP(op) != IS_ARRAY) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"invalid operations list");
			return;
		}

		HashTable *op_array = Z_ARRVAL_PP(op);
		long field_no;
		long opcode;

		if (!hash_fing_long(op_array, "field", &field_no)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"can't find 'field' in the update field operation");
			return;
		}

		if (!hash_fing_long(op_array, "op", &opcode)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"can't find 'op' in the update field operation");
			return;
		}

		/* write field number */
		if (!io_buf_write_int32(tnt->io_buf, field_no))
			return;

		/* write operation code */
		if (!io_buf_write_byte(tnt->io_buf, opcode))
			return;

		zval **assing_arg = NULL;
		long arith_arg;
		long splice_offset;
		long splice_length;
		char *splice_list;
		int splice_list_len;
		switch (opcode) {
		case TARANTOOL_OP_ASSIGN:
			if (!hash_fing_scalar(op_array, "arg", &assing_arg)) {
				zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
										"can't find 'arg' in the update field operation");
				return;
			}
			if (Z_TYPE_PP(assing_arg) == IS_LONG) {
				/* write as interger */
				if (!io_buf_write_field_str(tnt->io_buf, (uint8_t *) &Z_LVAL_PP(assing_arg), sizeof(int32_t)))
					return;
			} else {
				/* write as string */
				if (!io_buf_write_field_str(tnt->io_buf, (uint8_t *) Z_STRVAL_PP(assing_arg), Z_STRLEN_PP(assing_arg)))
					return;
			}
			break;
		case TARANTOOL_OP_ADD:
		case TARANTOOL_OP_AND:
		case TARANTOOL_OP_XOR:
		case TARANTOOL_OP_OR:
			if (!hash_fing_long(op_array, "arg", &arith_arg)) {
				zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
										"can't find 'arg' in the update field operation");
				return;
			}
			/* write arith arg */
			if (!io_buf_write_field_str(tnt->io_buf, (uint8_t *) &arith_arg, sizeof(int32_t)))
				return;
			break;
		case TARANTOOL_OP_SPLICE:
			/*
			 * read splice args
			 */

			/* read offset */
			if (!hash_fing_long(op_array, "offset", &splice_offset)) {
				zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
										"can't find 'offset' in the update field operation");
				return;
			}
			/* read length */
			if (!hash_fing_long(op_array, "length", &splice_length)) {
				zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
										"can't find 'length' in the update field operation");
				return;
			}
			/* read list */
			if (!hash_fing_str(op_array, "list", &splice_list, &splice_list_len)) {
				zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
										"can't find 'list' in the update field operation");
				return;
			}

			/*
			 * write splice args
			 */
			io_buf_clean(tnt->splice_field);

			/* write offset to separate buffer */
			if (!io_buf_write_field_str(tnt->splice_field, (uint8_t *) &splice_offset, sizeof(int32_t)))
				return;
			/* write length to separate buffer */
			if (!io_buf_write_field_str(tnt->splice_field, (uint8_t *) &splice_length, sizeof(int32_t)))
				return;
			/* write list to separate buffer */
			if (!io_buf_write_field_str(tnt->splice_field, (uint8_t *) splice_list, splice_list_len))
				return;

			/* write splice args as alone field */
			if (!io_buf_write_field_str(tnt->io_buf, tnt->splice_field->value, tnt->splice_field->size))
				return;

			break;
		default:
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"invalid operaion code %i", opcode);
			return;
		}
	}

	/* send iproto request */
	if (!io_buf_send_iproto(tnt->stream, TARANTOOL_COMMAND_UPDATE, 0, tnt->io_buf))
	  return;

	/*
	 * receive response
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* receive */
	if (!io_buf_recv_iproto(tnt->stream, tnt->io_buf))
		return;

	/* read response */
	struct tnt_response *response;
	if (!io_buf_read_struct(tnt->io_buf,
						  (void **) &response,
						  sizeof(struct tnt_response))) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"update fields failed: invalid response was received");
		return;
	}

	/* check return code */
	if (response->return_code) {
		/* error happen, throw exceprion */
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"update fields failed: %"PRIi32"(0x%08"PRIx32"): %s",
								response->return_code,
								response->return_code,
								response->return_msg);
		return;
	}
	
	/*
	 * fill return value
	 */

	if (array_init(return_value) != SUCCESS) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"update fields failed: create array failed");
		return;
	}

	/* put count to result array */
	add_assoc_long(return_value, "count", response->count);	

	/* check "return tuple" flag */
	if ((response->count > 0) && (flags & TARANTOOL_FLAGS_RETURN_TUPLE)) {
		/* ok, the responce should contain inserted tuple */
		if (!io_buf_read_tuple(tnt->io_buf, &tuple)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"update fields failed: invalid response was received");
			return;
		}

		/* put returned tuple to result array */
		add_assoc_zval(return_value, "tuple", tuple);
	}
}

PHP_METHOD(tarantool_class, delete)
{
	/* 
	 * parse methods parameters 
	 */
	zval *id;
	long space_no = 0;
	long flags = 0;
	zval *tuple = NULL;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
									 getThis(),
									 "Olz|l",
									 &id,
									 tarantool_class_ptr,
									 &space_no,
									 &tuple,
									 &flags) == FAILURE) {
		return;
	}

	tarantool_object *tnt = (tarantool_object *) zend_object_store_get_object(
		id TSRMLS_CC);

	/* check connection */
	if (!tnt->stream) {
		/* establis connection */
		tnt->stream = establish_connection(tnt->host, tnt->port);
		if (!tnt->stream)
			return;
	}

	/*
	 * send request
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* fill delete command */
	struct tnt_delete_request *request = (struct tnt_delete_request *) io_buf_write_struct(
		tnt->io_buf, sizeof(struct tnt_delete_request));
	if (request == NULL)
		return;

	/* space number */
	request->space_no = space_no;
	/* flags */
	request->flags = flags;
	/* tuple */
	if (!io_buf_write_tuple(tnt->io_buf, tuple))
		return;

	/* send iproto request */
	if (!io_buf_send_iproto(tnt->stream, TARANTOOL_COMMAND_DELETE, 0, tnt->io_buf))
		return;

	/*
	 * receive response
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* receive */
	if (!io_buf_recv_iproto(tnt->stream, tnt->io_buf))
		return;

	/* read response */
	struct tnt_response *response;
	if (!io_buf_read_struct(tnt->io_buf,
						  (void **) &response,
						  sizeof(struct tnt_response))) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"delete failed: invalid response was received");
		return;
	}

	/* check return code */
	if (response->return_code) {
		/* error happen, throw exceprion */
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"delete failed: %"PRIi32"(0x%08"PRIx32"): %s",
								response->return_code,
								response->return_code,
								response->return_msg);
		return;
	}
	
	/*
	 * fill return value
	 */

	if (array_init(return_value) != SUCCESS) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"delete failed: create array failed");
		return;
	}

	/* put count to result array */
	add_assoc_long(return_value, "count", response->count);

	/* check "return tuple" flag */
	if ((response->count) > 0 && (flags & TARANTOOL_FLAGS_RETURN_TUPLE)) {
		/* ok, the responce should contain inserted tuple */
		if (!io_buf_read_tuple(tnt->io_buf, &tuple)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"delete failed: invalid response was received");
			return;
		}

		/* put returned tuple to result array */
		add_assoc_zval(return_value, "tuple", tuple);
	}
}

PHP_METHOD(tarantool_class, call)
{
	/* 
	 * parse methods parameters 
	 */
	zval *id;
	char *proc_name = NULL;
	int proc_name_len = 0;
	zval *tuple = NULL;
	long flags = 0;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
									 getThis(),
									 "Osz|l",
									 &id,
									 tarantool_class_ptr,
									 &proc_name, &proc_name_len,
									 &tuple,
									 &flags) == FAILURE) {
		return;
	}

	tarantool_object *tnt = (tarantool_object *) zend_object_store_get_object(
		id TSRMLS_CC);

	/* check connection */
	if (!tnt->stream) {
		/* establis connection */
		tnt->stream = establish_connection(tnt->host, tnt->port);
		if (!tnt->stream)
			return;
	}

	/*
	 * send request
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* fill insert command */
	struct tnt_call_request *request = (struct tnt_call_request *) io_buf_write_struct(
		tnt->io_buf, sizeof(struct tnt_call_request));
	if (request == NULL)
		return;

	/* flags */
	request->flags = flags;
	/* proc name */
	if (!io_buf_write_field_str(tnt->io_buf, proc_name, proc_name_len))
		return;
	/* tuple */
	if (!io_buf_write_tuple(tnt->io_buf, tuple))
		return;

	/* send iproto request */
	if (!io_buf_send_iproto(tnt->stream, TARANTOOL_COMMAND_CALL, 0, tnt->io_buf))
	  return;


	/*
	 * receive response
	 */

	/* clean-up buffer */
	io_buf_clean(tnt->io_buf);

	/* receive */
	if (!io_buf_recv_iproto(tnt->stream, tnt->io_buf))
		return;

	/* read response */
	struct tnt_response *response;
	if (!io_buf_read_struct(tnt->io_buf,
						  (void **) &response,
						  sizeof(struct tnt_response))) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"call failed: invalid response was received");
		return;
	}

	/* check return code */
	if (response->return_code) {
		/* error happen, throw exceprion */
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"call failed: %"PRIi32"(0x%08"PRIx32"): %s",
								response->return_code,
								response->return_code,
								response->return_msg);
		return;
	}

	if (array_init(return_value) != SUCCESS) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"call failed: create array failed");
		return;
	}

	/* put count to result array */
	add_assoc_long(return_value, "count", response->count);

	/* put tuple list to result array */
	zval *tuples_list;
	MAKE_STD_ZVAL(tuples_list);
	if (array_init(tuples_list) == FAILURE) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"call failed: create array failed");
		return;
	}

	/* read tuples for responce */
	int i;
	for (i = 0; i < response->count; ++i) {
		zval *tuple;
		if (!io_buf_read_tuple(tnt->io_buf, &tuple)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"call failed: invalid response was received");
			return;
		}
		add_next_index_zval(tuples_list, tuple);
	}

	add_assoc_zval(return_value, "tuples_list", tuples_list);
}

PHP_METHOD(tarantool_class, admin)
{
	/* parse methods parameters */
	zval *id;
	char *cmd = NULL;
	int cmd_len = 0;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
									 getThis(),
									 "Os",
									 &id,
									 tarantool_class_ptr,
									 &cmd, &cmd_len) == FAILURE) {
		return;
	}

	tarantool_object *tnt = (tarantool_object *) zend_object_store_get_object(
		id TSRMLS_CC);

	/* check admin port */
	if (!tnt->admin_port) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"admin command not allowed for this commection");
		return;
	}

	/* check connection */
	if (!tnt->admin_stream) {
		/* establis connection */
		tnt->admin_stream = establish_connection(tnt->host, tnt->admin_port);
		if (!tnt->admin_stream)
			return;

		/* set string eol */
		php_stream_locate_eol(tnt->admin_stream,
							  ADMIN_SEPARATOR,
							  strlen(ADMIN_SEPARATOR) TSRMLS_DC);
	}

	/* send request */
	io_buf_clean(tnt->io_buf);
	if (!io_buf_write_str(tnt->io_buf, cmd, cmd_len))
		return;
	if (!io_buf_write_str(tnt->io_buf, ADMIN_SEPARATOR, strlen(ADMIN_SEPARATOR)))
		return;
	if (!io_buf_send_yaml(tnt->admin_stream, tnt->io_buf))
		return;

	/* recv response */
	io_buf_clean(tnt->io_buf);
	if (!io_buf_recv_yaml(tnt->admin_stream, tnt->io_buf))
		return;

	char *response = estrndup(tnt->io_buf->value, tnt->io_buf->size);
	RETURN_STRING(response, 0);
}


/*============================================================================*
 * local functions definition
 *============================================================================*/


/*----------------------------------------------------------------------------*
 * Buffer interface
 *----------------------------------------------------------------------------*/

static struct io_buf *
io_buf_create()
{
	struct io_buf *buf = (struct io_buf *) emalloc(sizeof(struct io_buf));
	if (!buf) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"allocation memory fail: %s (%i)", strerror(errno), errno);
		goto failure;
	}

	buf->size = 0;
	buf->capacity = io_buf_next_capacity(buf->size);
	buf->readed = 0;
	buf->value = (uint8_t *) emalloc(buf->capacity);
	if (!buf->value) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"allocation memory fail: %s (%i)", strerror(errno), errno);
		goto failure;
	}

	return buf;

failure:
	if (buf) {
		if (buf->value)
			efree(buf->value);

		efree(buf);
	}

	return NULL;
}

static void
io_buf_destroy(struct io_buf *buf)
{
	if (!buf)
		return;

	if (buf->value)
		efree(buf->value);

	efree(buf);
}

inline static bool
io_buf_reserve(struct io_buf *buf, size_t n)
{
	if (buf->capacity > n)
		return true;

	size_t new_capacity = io_buf_next_capacity(n);
	uint8_t *new_value = (uint8_t *) erealloc(buf->value, new_capacity);
	if (!new_value) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"allocation memory fail: %s (%i)", strerror(errno), errno);
		return false;
	}

	buf->capacity = new_capacity;
	buf->value = new_value;
	return true;
}

inline static bool
io_buf_resize(struct io_buf *buf, size_t n)
{
	io_buf_reserve(buf, n);
	buf->size = n;
	return true;
}

inline static size_t
io_buf_next_capacity(size_t n)
{
	size_t capacity = IO_BUF_CAPACITY_MIN;
	while (capacity < n)
		capacity *= IO_BUF_CAPACITY_FACTOR;
	return capacity;
}

static void
io_buf_clean(struct io_buf *buf)
{
	buf->size = 0;
	buf->readed = 0;
}

static bool
io_buf_read_struct(struct io_buf *buf, void **ptr, size_t n)
{
	size_t last = buf->size - buf->readed;
	if (last < n)
		return false;
	*ptr = buf->value + buf->readed;
	buf->readed += n;
	return true;
}

static bool
io_buf_read_int32(struct io_buf *buf, int32_t *val)
{
	size_t last = buf->size - buf->readed;
	if (last < sizeof(int32_t))
		return false;
	*val = *(int32_t *)(buf->value + buf->readed);
	buf->readed += sizeof(int32_t);
	return true;
}

static bool
io_buf_read_int64(struct io_buf *buf, int64_t *val)
{
	size_t last = buf->size - buf->readed;
	if (last < sizeof(int64_t))
		return false;
	*val = *(int64_t *)(buf->value + buf->readed);
	buf->readed += sizeof(int64_t);
	return true;
}

static bool
io_buf_read_varint(struct io_buf *buf, int32_t *val)
{
	uint8_t *b = buf->value + buf->readed;
	size_t size = buf->size - buf->readed;

	if (size < 1)
		return false;

	if (!(b[0] & 0x80)) {
		buf->readed += 1;
		*val = (b[0] & 0x7f);
		return true;
	}

	if (size < 2)
		return false;

	if (!(b[1] & 0x80)) {
		buf->readed += 2;
		*val = (b[0] & 0x7f) << 7 | (b[1] & 0x7f);
		return true;
	}

	if (size < 3)
		return false;

	if (!(b[2] & 0x80)) {
		buf->readed += 3;
		*val = (b[0] & 0x7f) << 14 | (b[1] & 0x7f) << 7 | (b[2] & 0x7f);
		return true;
	}

	if (size < 4)
		return false;

	if (!(b[3] & 0x80)) {
		buf->readed += 4; 
		*val = (b[0] & 0x7f) << 21 | (b[1] & 0x7f) << 14 |
			(b[2] & 0x7f) << 7 | (b[3] & 0x7f);
		return true;
	}

	if (size < 5)
		return false;

	if (!(b[4] & 0x80)) {
		buf->readed += 5;
		*val = (b[0] & 0x7f) << 28 | (b[1] & 0x7f) << 21 |
			(b[2] & 0x7f) << 14 | (b[3] & 0x7f) << 7 | (b[4] & 0x7f);
		return true;
	}

	return false;
}

static bool
io_buf_read_str(struct io_buf *buf, char **str, size_t len)
{
	size_t last = buf->size - buf->readed;
	if (last < len)
		return false;
	*str = (char *)(buf->value + buf->readed);
	buf->readed += len;
	return true;
}

static bool
io_buf_read_field(struct io_buf *buf, zval *tuple)
{
	int32_t field_length;

	if (!io_buf_read_varint(buf, &field_length))
		return false;

	int32_t i32_val;
	int64_t i64_val;
	char *str_val;
	switch (field_length) {
	case sizeof(int32_t):
		if (!io_buf_read_int32(buf, &i32_val))
			return false;
		add_next_index_long(tuple, i32_val);
		break;
	case sizeof(int64_t):
		if (!io_buf_read_int64(buf, &i64_val))
			return false;
		add_next_index_long(tuple, (long) i64_val);
		break;
	default:
		if (!io_buf_read_str(buf, &str_val, field_length))
			return false;
		add_next_index_stringl(tuple, str_val, field_length, true);
	}

	return true;
}

static bool
io_buf_read_tuple(struct io_buf *buf, zval **tuple)
{
	MAKE_STD_ZVAL(*tuple);
	if (array_init(*tuple) == FAILURE) {
		return false;
	}

	int32_t size;
	if (!io_buf_read_int32(buf, &size))
		return false;

	int32_t cardinality;
	if (!io_buf_read_int32(buf, &cardinality))
		return false;

	while (cardinality > 0) {
		if (!io_buf_read_field(buf, *tuple))
			return false;
		cardinality -= 1;
	}

	return true;
}

static void *
io_buf_write_struct(struct io_buf *buf, size_t n)
{
	if (!io_buf_reserve(buf, buf->size + n))
		return NULL;
	void *ptr = buf->value + buf->size;
	buf->size += n;
	return ptr;
}

static bool
io_buf_write_byte(struct io_buf *buf, int8_t value)
{
	if (!io_buf_reserve(buf, buf->size + sizeof(int8_t)))
		return false;
	*(int8_t *)(buf->value + buf->size) = value;
	buf->size += sizeof(uint8_t);
	return true;
}

static bool
io_buf_write_int32(struct io_buf *buf, int32_t value)
{
	if (!io_buf_reserve(buf, buf->size + sizeof(int32_t)))
		return false;
	*(int32_t *)(buf->value + buf->size) = value;
	buf->size += sizeof(int32_t);
	return true;
}

static bool
io_buf_write_int64(struct io_buf *buf, int64_t value)
{
	if (!io_buf_reserve(buf, buf->size + sizeof(int64_t)))
		return false;
	*(int64_t *)(buf->value + buf->size) = value;
	buf->size += sizeof(int64_t);
	return true;
}

static bool
io_buf_write_varint(struct io_buf *buf, int32_t value)
{
	if (!io_buf_reserve(buf, buf->size + 5))
		/* reseve maximal varint size (5 bytes) */
		return false;

	if (value >= (1 << 7)) {
		if (value >= (1 << 14)) {
			if (value >= (1 << 21)) {
				if (value >= (1 << 28))
					io_buf_write_byte(buf, (int8_t)(value >> 28) | 0x80);
				io_buf_write_byte(buf, (int8_t)(value >> 21) | 0x80);
			}
			io_buf_write_byte(buf, (int8_t)((value >> 14) | 0x80));
		}
		io_buf_write_byte(buf, (int8_t)((value >> 7) | 0x80));
	}
	io_buf_write_byte(buf, (int8_t)((value) & 0x7F));

	return true;
}

static bool
io_buf_write_str(struct io_buf *buf, uint8_t *str, size_t len)
{
	if (!io_buf_reserve(buf, buf->size + len))
		return false;

	memcpy(buf->value + buf->size, str, len);
	buf->size += len;
	return true;
}

static bool
io_buf_write_field_int32(struct io_buf *buf, uint32_t value)
{
	/* write field length (4 bytes) */
	if (!io_buf_write_varint(buf, sizeof(int32_t)))
		return false;
	/* write field value */
	if (!io_buf_write_int32(buf, value))
		return false;
	return true;
}

static bool
io_buf_write_field_int64(struct io_buf *buf, uint64_t value)
{
	/* write field length (8 bytes) */
	if (!io_buf_write_varint(buf, sizeof(int64_t)))
		return false;
	/* write field value */
	if (!io_buf_write_int64(buf, value))
		return false;
	return true;
}

static bool
io_buf_write_field_str(struct io_buf *buf, uint8_t *field_value, size_t field_length)
{
	/* write field length (string length) */
	if (!io_buf_write_varint(buf, (int32_t)field_length))
		return false;
	/* write field value (string) */
	if (!io_buf_write_str(buf, field_value, field_length))
		return false;
	return true;
}

static bool
io_buf_write_tuple_int(struct io_buf *buf, zval *tuple)
{
	/* single field tuple: (int) */
	long long_value = Z_LVAL_P(tuple);
	/* write tuple cardinality */
	if (!io_buf_write_int32(buf, 1))
		return false;
	/* write field */
	if ((unsigned long)long_value <= 0xffffffffllu) {
		if (!io_buf_write_field_int32(buf, (uint32_t)long_value))
			return false;
	} else {
		if (!io_buf_write_field_int64(buf, (uint64_t)long_value))
			return false;
	}

	return true;
}

static bool
io_buf_write_tuple_str(struct io_buf *buf, zval *tuple)
{
	/* single field tuple: (string) */
	char *str_value = Z_STRVAL_P(tuple);
	size_t str_length = Z_STRLEN_P(tuple);
	/* write tuple cardinality */
	if (!io_buf_write_int32(buf, 1))
		return false;
	/* write field */
	if (!io_buf_write_field_str(buf, str_value, str_length))
		return false;

	return true;
}

static bool
io_buf_write_tuple_array(struct io_buf *buf, zval *tuple)
{
	/* multyply tuple array */
	HashTable *hash = Z_ARRVAL_P(tuple);
	HashPosition itr;
	zval **field;
	/* put tuple cardinality */
	io_buf_write_int32(buf, zend_hash_num_elements(hash));
	for (zend_hash_internal_pointer_reset_ex(hash, &itr);
		 zend_hash_get_current_data_ex(hash, (void **) &field, &itr) == SUCCESS;
		 zend_hash_move_forward_ex(hash, &itr)) {
		char *str_value;
		size_t str_length;
		long long_value;

		switch (Z_TYPE_PP(field)) {
		case IS_STRING:
			/* string field */
			str_value = Z_STRVAL_PP(field);
			str_length = Z_STRLEN_PP(field);
			io_buf_write_field_str(buf, str_value, str_length);
			break;
		case IS_LONG:
			/* integer field */
			long_value = Z_LVAL_PP(field);
			io_buf_write_field_str(buf, (uint8_t *)&long_value, sizeof(int32_t));
			break;
		default:
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"unsupported field type");
			return false;
		}
	}

	return true;
}

static bool
io_buf_write_tuple(struct io_buf *buf, zval *tuple)
{
	/* write tuple by type */
	switch (Z_TYPE_P(tuple)) {
	case IS_LONG:
		/* write integer as tuple */
		return io_buf_write_tuple_int(buf, tuple);
	case IS_STRING:
		/* write string as tuple */
		return io_buf_write_tuple_str(buf, tuple);
	case IS_ARRAY:
		/* write array as tuple */
		return io_buf_write_tuple_array(buf, tuple);
	default:
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"unsupported tuple type");
		return false;		
	}

	return true;
}

static bool
io_buf_write_tuples_list_array(struct io_buf *buf, zval *tuples_list)
{
	HashTable *hash = Z_ARRVAL_P(tuples_list);
	HashPosition itr;
	zval **tuple;

	/* write number of tuples */
	if (!io_buf_write_int32(buf, zend_hash_num_elements(hash)))
		return false;

	/* write tuples */
	for (zend_hash_internal_pointer_reset_ex(hash, &itr);
		 zend_hash_get_current_data_ex(hash, (void **) &tuple, &itr) == SUCCESS;
		 zend_hash_move_forward_ex(hash, &itr)) {
		if (Z_TYPE_PP(tuple) != IS_ARRAY) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"invalid tuples list: expected array of array");
			return false;
		}

		if (!io_buf_write_tuple_array(buf, *tuple))
			return false;
	}

	return true;
}


static bool
io_buf_write_tuples_list(struct io_buf *buf, zval *tuples_list)
{
	HashTable *hash;
	HashPosition itr;
	zval **tuple;

	switch (Z_TYPE_P(tuples_list)) {
	case IS_LONG:
		/* single tuple: long */
		/* write number of tuples */
		if (!io_buf_write_int32(buf, 1))
			return false;
		/* write tuple */
		if (!io_buf_write_tuple_int(buf, tuples_list))
			return false;
		break;
	case IS_STRING:
		/* single tuple: string */
		/* write number of tuples */
		if (!io_buf_write_int32(buf, 1))
			return false;		
		/* write tuple */
		if (!io_buf_write_tuple_str(buf, tuples_list))
			return false;
		break;
	case IS_ARRAY:
		/* array: migth be single or multi tuples array */
		hash = Z_ARRVAL_P(tuples_list);
		zend_hash_internal_pointer_reset_ex(hash, &itr);		
		if (zend_hash_get_current_data_ex(hash, (void **) &tuple, &itr) != SUCCESS) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"invalid tuples list: empty array");
			return false;
		}

		/* check type of the first element */
		switch (Z_TYPE_PP(tuple)) {
		case IS_STRING:
		case IS_LONG:
			/* single tuple: array */
			/* write tuples count */
			if (!io_buf_write_int32(buf, 1))
				return false;
			/* write tuple */
			if (!io_buf_write_tuple_array(buf, tuples_list))
				return false;
			break;
		case IS_ARRAY:
			/* multi tuples list */
			if (!io_buf_write_tuples_list_array(buf, tuples_list))
				return false;
			break;
		default:
			/* invalid element type */
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
									"unsupported tuple type");
			return false;
		}

		break;
	default:
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"unsupported tuple type");
		return false;		
	}

	return true;
}

/*
 * I/O buffer send/recv 
 */

static bool
io_buf_send_yaml(php_stream *stream, struct io_buf *buf)
{
	if (php_stream_write(stream,
						 buf->value,
						 buf->size) != buf->size) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"send message fail");
		return false;
	}

	/* flush request */
	php_stream_flush(stream);

	return true;
}

static bool
io_buf_recv_yaml(php_stream *stream, struct io_buf *buf)
{
	char *line = php_stream_get_line(stream, NULL, 0, NULL);
	while (strcmp(line, ADMIN_TOKEN_BEGIN) != 0) {
		line = php_stream_get_line(stream, NULL, 0, NULL);
	}

	line = php_stream_get_line(stream, NULL, 0, NULL);
	while (strcmp(line, ADMIN_TOKEN_END) != 0) {
		io_buf_write_str(buf, line, strlen(line));
		line = php_stream_get_line(stream, NULL, 0, NULL);
	}

	return true;
}

static bool
io_buf_send_iproto(php_stream *stream, int32_t type, int32_t request_id, struct io_buf *buf)
{
	/* send iproto header */
	struct iproto_header header;
	header.type = type;
	header.length = buf->size;
	header.request_id = request_id;

	size_t length = sizeof(struct iproto_header);
	if (php_stream_write(stream, (char *) &header, length) != length) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"send requset failed");
		return false;
	}

	/* send requets */
	if (php_stream_write(stream, buf->value, buf->size) != buf->size) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"send requset failed");
		return false;
	}

	/* flush request */
	php_stream_flush(stream);

	return true;
}

static bool
io_buf_recv_iproto(php_stream *stream, struct io_buf *buf)
{
	/* receiving header */
	struct iproto_header header;
	size_t length = sizeof(struct iproto_header);
	if (php_stream_read(stream, (char *) &header, length) != length) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"receive response failed");
		return false;
	}

	/* receiving body */
	if (!io_buf_resize(buf, header.length))
		return false;
	if (php_stream_read(stream, buf->value, buf->size) != buf->size) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"receive response failed");
		return false;
	}

	return true;
}


/*----------------------------------------------------------------------------*
 * support local functions
 *----------------------------------------------------------------------------*/

static zend_object_value
alloc_tarantool_object(zend_class_entry *entry TSRMLS_DC)
{
	zend_object_value new_value;

	/* allocate and clean-up instance */
	tarantool_object *tnt = (tarantool_object *) emalloc(sizeof(tarantool_object));
	/* TODO: emalloc result must be checked */
	memset(tnt, 0, sizeof(tarantool_object));

	/* initialize class instance */
	zend_object_std_init(&tnt->zo, entry TSRMLS_CC);
	new_value.handle = zend_objects_store_put(
		tnt,
		(zend_objects_store_dtor_t) zend_objects_destroy_object, 
		(zend_objects_free_object_storage_t) free_tarantool_object,
		NULL TSRMLS_CC);
 	new_value.handlers = zend_get_std_object_handlers();
	return new_value;
}

static void
free_tarantool_object(tarantool_object *tnt TSRMLS_DC)
{
	if (tnt == NULL)
		return;

	if (tnt->stream)
		php_stream_close(tnt->stream);

	if (tnt->admin_stream)
		php_stream_close(tnt->admin_stream);

	io_buf_destroy(tnt->io_buf);
	io_buf_destroy(tnt->splice_field);
	efree(tnt);
}

static php_stream *
establish_connection(char *host, int port)
{
	char *msg = NULL;
	/* initialize connection parameters */
	char *dest_addr = NULL;
	size_t dest_addr_len = spprintf(&dest_addr, 0, "tcp://%s:%d", host, port);
	int options = ENFORCE_SAFE_MODE | REPORT_ERRORS;
	int flags = STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT;
	struct timeval timeout = {
		.tv_sec = TARANTOOL_TIMEOUT_SEC,
		.tv_usec = TARANTOOL_TIMEOUT_USEC,
	};
	char *error_msg = NULL;
	int error_code = 0;

	/* establish connection */
	php_stream *stream = php_stream_xport_create(dest_addr, dest_addr_len,
												 options, flags,
												 NULL, &timeout, NULL,
												 &error_msg, &error_code);
	efree(dest_addr);

	/* check result */
	if (error_code && error_msg) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"establist connection fail: %s", error_msg);
		goto process_error;
	}

	/* set socket flag 'TCP_NODELAY' */
	int socketd = ((php_netstream_data_t*)stream->abstract)->socket;
	flags = 1;
	int result = setsockopt(socketd, IPPROTO_TCP, TCP_NODELAY, (char *) &flags, sizeof(int));
	if (result != 0) {
		char error_buf[64];
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_DC,
								"establist connection fail: setsockopt %s", strerror_r(errno, error_buf, sizeof(error_buf)));
		goto process_error;
	}

	return stream;

process_error:

	if (error_msg)
		efree(error_msg);

	if (stream)
		php_stream_close(stream);

	return NULL;
}

static bool
hash_fing_long(HashTable *hash, char *key, long *value)
{
	zval **zvalue = NULL;
	if (zend_hash_find(hash, key, strlen(key) + 1, (void **)&zvalue) != SUCCESS)
		return false;
	if (Z_TYPE_PP(zvalue) != IS_LONG)
		return false;
	*value = Z_LVAL_PP(zvalue);
	return true;
}

static bool
hash_fing_str(HashTable *hash, char *key, char **value, int *value_length)
{
	zval **zvalue = NULL;
	if (zend_hash_find(hash, key, strlen(key) + 1, (void **)&zvalue) != SUCCESS)
		return false;
	if (Z_TYPE_PP(zvalue) != IS_STRING)
		return false;
	*value = Z_STRVAL_PP(zvalue);
	*value_length = Z_STRLEN_PP(zvalue);
	return true;
}

static bool
hash_fing_scalar(HashTable *hash, char *key, zval ***value)
{
	if (zend_hash_find(hash, key, strlen(key) + 1, (void **)value) != SUCCESS)
		return false;
	if (Z_TYPE_PP(*value) != IS_STRING && Z_TYPE_PP(*value) != IS_LONG)
		return false;
	return true;
}


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
