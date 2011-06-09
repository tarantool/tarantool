dnl $Id$
dnl config.m4 for extension tarantool




 PHP_ARG_WITH(tarantool, for tarantool support,
 [  --with-tarantool             Include tarantool support])

dnl Otherwise use enable:

dnl PHP_ARG_ENABLE(tarantool, whether to enable tarantool support,
dnl Make sure that the comment is aligned:
dnl [  --enable-tarantool           Enable tarantool support])

if test "$PHP_TARANTOOL" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-tarantool -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/tarantool.h"  # you most likely want to change this
  dnl if test -r $PHP_TARANTOOL/$SEARCH_FOR; then # path given as parameter
  dnl   TARANTOOL_DIR=$PHP_TARANTOOL
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for tarantool files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       TARANTOOL_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$TARANTOOL_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the tarantool distribution])
  dnl fi

  dnl # --with-tarantool -> add include path
  dnl PHP_ADD_INCLUDE($TARANTOOL_DIR/include)

  dnl # --with-tarantool -> check for lib and symbol presence
  dnl LIBNAME=tarantool # you may want to change this
  dnl LIBSYMBOL=tarantool # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $TARANTOOL_DIR/lib, TARANTOOL_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_TARANTOOLLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong tarantool lib version or lib not found])
  dnl ],[
  dnl   -L$TARANTOOL_DIR/lib -lm
  dnl ])
  dnl
  dnl PHP_SUBST(TARANTOOL_SHARED_LIBADD)

  PHP_NEW_EXTENSION(tarantool, tarantool.c, $ext_shared)
fi
