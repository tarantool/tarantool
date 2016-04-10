
=====================================================================
         Interpreting function return values
=====================================================================


For all Connectors, calling a function
via Tarantool causes a return in MsgPack format.
If the function is called using the connector's API,
some conversions may occur.
All scalar values are returned as tuples
(with a MsgPack type-identifier followed by a
value); all non-scalar values are returned as
a group of tuples (with a MsgPack array-identifier
followed by the scalar values). 
If the function is called via the binary protocol
command layer -- "eval" -- rather than via the connector's API,
no conversions occur.

In the following example, a Lua function will be created.
Since it will be accessed externally by a 'guest' user,
a :codenormal:`grant` of an execute privilege will be necessary.
The function returns an empty array, a scalar string,
two booleans, and a short integer. The values are
the ones described in the msgpack section in the table
:ref:`Common Types and MsgPack Encodings <common-types-and-msgpack-encodings>`.

:codenormal:`tarantool>` :codebold:`box.cfg{listen=3301}` |br|
:codenormal:`2016-03-03 18:45:52.802 [27381] main/101/interactive I> ready to accept requests` |br|
:codenormal:`---` |br|
:codenormal:`...` |br|
:codenormal:`tarantool>` :codebold:`function f() return {},'a',false,true,127; end` |br|
:codenormal:`---` |br|
:codenormal:`...` |br|
:codenormal:`tarantool>` :codebold:`box.schema.func.create('f')` |br|
:codenormal:`---` |br|
:codenormal:`...` |br|
:codenormal:`tarantool>` :codebold:`box.schema.user.grant('guest','execute','function','f')` |br|
:codenormal:`---` |br|
:codenormal:`...`

Here is a C program which calls the function.
Although C is being used for the example,
the result would be precisely the same if the calling
program was written in Perl, PHP, Python, Go, or Java.

.. code-block:: c

  #include <stdio.h>` |br|
  #include <stdlib.h>` |br|
  #include <tarantool/tarantool.h>` |br|
  #include <tarantool/tnt_net.h>` |br|
  #include <tarantool/tnt_opt.h>` |br|
  void main() {` |br|
    struct tnt_stream *tnt = tnt_net(NULL);            /* SETUP */`
    tnt_set(tnt, TNT_OPT_URI, "localhost:3301");`
     if (tnt_connect(tnt) < 0) {                        /* CONNECT */`
         printf("Connection refused\n");`
         exit(-1);`
     }
     struct tnt_stream *tuple = tnt_object(NULL);       /* MAKE REQUEST */
     struct tnt_stream *arg; arg = tnt_object(NULL);
     tnt_object_add_array(arg, 0);
     struct tnt_request *req1 = tnt_request_call(NULL); /* CALL function f() */
     tnt_request_set_funcz(req1, "f");
     tnt_request_set_tuple(req1, arg);
     uint64_t sync1 = tnt_request_compile(tnt, req1);
     tnt_flush(tnt);                                    /* SEND REQUEST */
     struct tnt_reply reply;  tnt_reply_init(&reply);   /* GET REPLY */
     tnt->read_reply(tnt, &reply);
     if (reply.code != 0) {
       printf("Call failed %lu.\n", reply.code);
       exit(-1);
     }
     const unsigned char *p= (unsigned char*)reply.data;/* PRINT REPLY */
     while (p < (unsigned char *) reply.data_end)
     {
       printf("%x ", *p);
       ++p;
     }
     printf("\n");
     tnt_close(tnt);                                    /* TEARDOWN */
     tnt_stream_free(tuple);
     tnt_stream_free(tnt);
  }

When this program is executed, it will print: |br|
:codenormal:`dd 0 0 0 5 90 91 a1 61 91 c2 91 c3 91 7f` |br|
The first five bytes -- :codenormal:`dd 0 0 0 5` -- are the
msgpack encoding for "32-bit array header with
value 5" (see the msgpack Specification_ page).
The rest are as described in the
Common Types and MsgPack Encodings table.

.. _Specification: http://github.com/msgpack/msgpack/blob/master/spec.md
