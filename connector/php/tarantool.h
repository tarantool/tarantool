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
#ifndef PHP_TARANTOOL_H
#define PHP_TARANTOOL_H

#ifdef ZTS
#include "TSRM.h"
#endif


/*============================================================================*
 * Constants
 *============================================================================*/

#define TARANTOOL_EXTENSION_VERSION "1.0"


/*----------------------------------------------------------------------------*
 * tbuf constants
 *----------------------------------------------------------------------------*/

enum {
	/* tbuf minimal capacity */
	IO_BUF_CAPACITY_MIN = 128,
	/* tbuf factor */
	IO_BUF_CAPACITY_FACTOR = 2,
};


/*----------------------------------------------------------------------------*
 * Connections constants
 *----------------------------------------------------------------------------*/

enum {
	/* timeout: seconds */
	TARANTOOL_TIMEOUT_SEC = 5,
	/* timeout: microseconds */
	TARANTOOL_TIMEOUT_USEC = 0,
	/* tarantool default primary port */
	TARANTOOL_DEFAULT_PORT = 33013,
	/* tarantool default readonly port */
	TARANTOOL_DEFAULT_RO_PORT = 33014,
	/* tarantool default adnim port */
	TARANTOOL_DEFAULT_ADMIN_PORT = 33015,
};

#define TARANTOOL_DEFAULT_HOST "localhost"


/*----------------------------------------------------------------------------*
 * Commands constants
 *----------------------------------------------------------------------------*/

/* tarantool/box flags */
enum {
	/* return resulting tuples */
	TARANTOOL_FLAGS_RETURN_TUPLE = 0x01,
	/* insert is add operation: errro will be raised if tuple exists */
	TARANTOOL_FLAGS_ADD = 0x02,
	/* insert is replace operation: errro will be raised if tuple doesn't exist */
	TARANTOOL_FLAGS_REPLACE = 0x04,
	/* doesn't write command to WAL */
	TARANTOOL_FLAGS_NOT_STORE = 0x10,
};

/* tarantool command codes */
enum {
	/* insert/replace command code */
	TARANTOOL_COMMAND_INSERT = 13,
	/* select command code */
	TARANTOOL_COMMAND_SELECT = 17,
	/* update fields command code */
	TARANTOOL_COMMAND_UPDATE = 19,
	/* delete command code */
	TARANTOOL_COMMAND_DELETE = 21,
	/* call lua function command code */
	TARANTOOL_COMMAND_CALL = 22,
	/* pid command code */
	TARANTOOL_COMMAND_PING = 65280,
};

/* update fields operation codes */
enum {
	/* update fields: assing field value operation code */
	TARANTOOL_OP_ASSIGN = 0,
	/* update fields: add operation code */
	TARANTOOL_OP_ADD = 1,
	/* update fields: and operation code */
	TARANTOOL_OP_AND = 2,
	/* update fields: xor operation code */
	TARANTOOL_OP_XOR = 3,
	/* update fields: or operation code */
	TARANTOOL_OP_OR	= 4,
	/* update fields: splice operation code */
	TARANTOOL_OP_SPLICE = 5,
};


/*----------------------------------------------------------------------------*
 * Amdin commands
 *----------------------------------------------------------------------------*/

/* admin protocol separator */
#define ADMIN_SEPARATOR "\r\n"
/* admin command begin token */
#define ADMIN_TOKEN_BEGIN "---"ADMIN_SEPARATOR
/* admin command end token */
#define ADMIN_TOKEN_END "..."ADMIN_SEPARATOR

/* show information admin command */
#define ADMIN_COMMAND_SHOW_INFO "show info"
/* show statistic admin command */
#define ADMIN_COMMAND_SHOW_STAT "show stat"
/* show configuration admin command */
#define ADMIN_COMMAND_SHOW_CONF "show configuration"


/*============================================================================*
 * Interaface decalaration
 *============================================================================*/


/*----------------------------------------------------------------------------*
 * Tarantool module interface
 *----------------------------------------------------------------------------*/

/* initialize module function */
PHP_MINIT_FUNCTION(tarantool);

/* shutdown module function */
PHP_MSHUTDOWN_FUNCTION(tarantool);

/* show information about this module */
PHP_MINFO_FUNCTION(tarantool);


/*----------------------------------------------------------------------------*
 * Tarantool class interface
 *----------------------------------------------------------------------------*/

/* class constructor */
PHP_METHOD(tarantool_class, __construct);

/* do select operation */
PHP_METHOD(tarantool_class, select);

/* do insert operation */
PHP_METHOD(tarantool_class, insert);

/* do update fields operation */
PHP_METHOD(tarantool_class, update_fields);

/* do delete operation */
PHP_METHOD(tarantool_class, delete);

/* call lua funtion operation */
PHP_METHOD(tarantool_class, call);

/* do admin command */
PHP_METHOD(tarantool_class, admin);


#ifdef ZTS
#define TARANTOOL_G(v) TSRMG(tarantool_globals_id, zend_tarantool_globals *, v)
#else
#define TARANTOOL_G(v) (tarantool_globals.v)
#endif

#endif /* PHP_TARANTOOL_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
