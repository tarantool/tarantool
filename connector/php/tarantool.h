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

/* $Id: header 252479 2008-02-07 19:39:50Z iliaa $ */

#ifndef PHP_TARANTOOL_H
#define PHP_TARANTOOL_H

extern zend_module_entry tarantool_module_entry;
#define phpext_tarantool_ptr &tarantool_module_entry

#ifdef PHP_WIN32
#	define PHP_TARANTOOL_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_TARANTOOL_API __attribute__ ((visibility("default")))
#else
#	define PHP_TARANTOOL_API
#endif

#define TARANTOOL_TIMEOUT  5 // sec
#define TARANTOOL_DEF_PORT 33013
#define TARANTOOL_ADMIN_PORT 33015

#define TARANTOOL_DEF_HOST "localhost"
#define TARANTOOL_BUFSIZE  2048 
#define TARANTOOL_SMALL_BUFSIZE  256 

#define TARANTOOL_INSERT  13 
#define TARANTOOL_SELECT  17 
#define TARANTOOL_UPDATE  19 
#define TARANTOOL_DELETE  20 
#define TARANTOOL_CALL    22
#define TARANTOOL_PING	  65280

#define TARANTOOL_REQUEST_ID  8 

#define TARANTOOL_OP_ASSIGN 0 
#define TARANTOOL_OP_ADD	1 
#define TARANTOOL_OP_AND	2
#define TARANTOOL_OP_XOR	3
#define TARANTOOL_OP_OR		4

#define TARANTOOL_SHOW_INFO "show info\r\n"
#define TARANTOOL_SHOW_INFO_SIZE sizeof(TARANTOOL_SHOW_INFO) 

#define TARANTOOL_SHOW_STAT "show stat\r\n"
#define TARANTOOL_SHOW_STAT_SIZE sizeof(TARANTOOL_SHOW_STAT) 

#define TARANTOOL_SHOW_CONF "show configuration\n"
#define TARANTOOL_SHOW_CONF_SIZE sizeof(TARANTOOL_SHOW_CONF) 


#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(tarantool);
PHP_MSHUTDOWN_FUNCTION(tarantool);

PHP_MINFO_FUNCTION(tarantool);

PHP_METHOD( tarantool_class, __construct);

PHP_METHOD( tarantool_class, insert);
PHP_METHOD( tarantool_class, select);
PHP_METHOD( tarantool_class, mselect);
PHP_METHOD( tarantool_class, call);
PHP_METHOD( tarantool_class, getTuple);
PHP_METHOD( tarantool_class, delete);
PHP_METHOD( tarantool_class, update);
PHP_METHOD( tarantool_class, inc);
PHP_METHOD( tarantool_class, getError);
PHP_METHOD( tarantool_class, getInfo);
PHP_METHOD( tarantool_class, getStat);
PHP_METHOD( tarantool_class, getConf);

#ifdef ZTS
#define TARANTOOL_G(v) TSRMG(tarantool_globals_id, zend_tarantool_globals *, v)
#else
#define TARANTOOL_G(v) (tarantool_globals.v)
#endif

#endif	
/* PHP_TARANTOOL_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
