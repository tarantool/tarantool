=====================================================================
                            C
=====================================================================

Here follow two examples of using Tarantool's high-level C API.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                         Example 1
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here is a complete C program that inserts :code:`[99999,'B']` into
space :code:`examples` via the high-level C API.

.. code-block:: c

    #include <stdio.h>
    #include <stdlib.h>
    
    #include <tarantool/tarantool.h>
    #include <tarantool/tnt_net.h>
    #include <tarantool/tnt_opt.h>

    void main() {
       struct tnt_stream *tnt = tnt_net(NULL);          /* See note = SETUP */
       tnt_set(tnt, TNT_OPT_URI, "localhost:3301");
       if (tnt_connect(tnt) < 0) {                      /* See note = CONNECT */
           printf("Connection refused\n");
           exit(-1);
       }
       struct tnt_stream *tuple = tnt_object(NULL);     /* See note = MAKE REQUEST */
       tnt_object_format(tuple, "[%d%s]", 99999, "B");
       tnt_insert(tnt, 999, tuple);                     /* See note = SEND REQUEST */
       tnt_flush(tnt);
       struct tnt_reply reply;  tnt_reply_init(&reply); /* See note = GET REPLY */
       tnt->read_reply(tnt, &reply);
       if (reply.code != 0) {
           printf("Insert failed %lu.\n", reply.code);
       }
       tnt_close(tnt);                                  /* See below = TEARDOWN */
       tnt_stream_free(tuple);
       tnt_stream_free(tnt);
    }

Paste the code into a file named :file:`example.c` and install ``tarantool-c``.
One way to install ``tarantool-c`` (using Ubuntu) is:

.. code-block:: console

    $ git clone git://github.com/tarantool/tarantool-c.git ~/tarantool-c
    $ cd ~/tarantool-c
    $ git submodule init
    $ git submodule update
    $ cmake .
    $ make
    $ make install

To compile and link the program, say:

.. code-block:: console

    $ # sometimes this is necessary:
    $ export LD_LIBRARY_PATH=/usr/local/lib
    $ gcc -o example example.c -ltarantool

Before trying to run,
check that the server is listening and that the space :code:`examples` exists, as 
:ref:`described earlier <connector-setting>`.
To run the program, say :code:`./example`. The program will connect
to the server, and will send the request.
If Tarantool is not running on localhost with listen address = 3301, the program
will print “Connection refused”.
If the insert fails, the program will print "Insert failed" and an error number
(see all error codes in the file 
`/src/box/errcode.h <https://github.com/tarantool/tarantool/blob/1.6/src/box/errcode.h>`_).

Here are notes corresponding to comments in the example program.

**SETUP:** The setup begins by creating a stream.

.. code-block:: c

    struct tnt_stream *tnt = tnt_net(NULL);
    tnt_set(tnt, TNT_OPT_URI, "localhost:3301");

In this program, the stream will be named :code:`tnt`.
Before connecting on the :code:`tnt` stream, some options may have to be set.
The most important option is TNT_OPT_URI.
In this program, the URI is ``localhost:3301``, since that is where the
Tarantool server is supposed to be listening.

Function description:

.. cssclass:: highlight
.. parsed-literal::

    `struct tnt_stream *tnt_net(struct tnt_stream *s)`
    `int tnt_set(struct tnt_stream *s, int option, variant option-value)`

**CONNECT:** Now that the stream named ``tnt`` exists and is associated with a
URI, this example program can connect to the server.

.. code-block:: c

    if (tnt_connect(tnt) < 0)
       { printf("Connection refused\n"); exit(-1); }

Function description:

.. cssclass:: highlight
.. parsed-literal::

    int tnt_connect(struct tnt_stream \*s)

The connection might fail for a variety of reasons, such as:
the server is not running, or the URI contains an invalid password.
If the connect fails, the return value will be -1.

**MAKE REQUEST:** Most requests require passing a structured value, such as
the contents of a tuple.

.. code-block:: c

    struct tnt_stream *tuple = tnt_object(NULL);
    tnt_object_format(tuple, "[%d%s]", 99999, "B");

In this program, the request will
be an INSERT, and the tuple contents will be an integer
and a string. This is a simple serial set of values, that
is, there are no sub-structures or arrays. Therefore it
is easy in this case to format what will be passed using
the same sort of arguments that one would use with a C
``printf()`` function: ``%d`` for the integer, ``%s`` for the string,
then the integer value, then a pointer to the string value.

Function description:

.. cssclass:: highlight
.. parsed-literal::

    ssize_t tnt_object_format(struct tnt_stream \*s, const char \*fmt, ...)

**SEND REQUEST:** The database-manipulation requests are analogous to the
requests in the box library.

.. code-block:: c

    tnt_insert(tnt, 999, tuple);
    tnt_flush(tnt);

In this program, the choice is to do an INSERT request, so
the program passes the ``tnt_stream`` that was used for connection
(:code:`tnt`) and the stream that was set up with :func:`tnt_object_format`
(:code:`tuple`).

Function description:

.. cssclass:: highlight
.. parsed-literal::

    ssize_t tnt_insert(struct tnt_stream \*s, uint32_t space, struct tnt_stream \*tuple)
    ssize_t tnt_replace(struct tnt_stream \*s, uint32_t space, struct tnt_stream \*tuple)
    ssize_t tnt_select(struct tnt_stream \*s, uint32_t space, uint32_t index,
                       uint32_t limit, uint32_t offset, uint8_t iterator,
                       struct tnt_stream \*key)
    ssize_t tnt_update(struct tnt_stream \*s, uint32_t space, uint32_t index,
                       struct tnt_stream \*key, struct tnt_stream \*ops)

**GET REPLY:** For most requests, the client will receive a reply containing some
indication whether the result was successful, and a set of tuples.

.. code-block:: c

    struct tnt_reply reply;  tnt_reply_init(&reply);
    tnt->read_reply(tnt, &reply);
    if (reply.code != 0)
       { printf("Insert failed %lu.\n", reply.code); }

This program checks for success but does not decode the rest of the reply.

Function description:

.. cssclass:: highlight
.. parsed-literal::

    struct tnt_reply \*tnt_reply_init(struct tnt_reply \*r)
    tnt->read_reply(struct tnt_stream \*s, struct tnt_reply \*r)
    void tnt_reply_free(struct tnt_reply \*r)

**TEARDOWN:** When a session ends, the connection that was made with
:func:`tnt_connect()` should be closed, and the objects that were made in the setup
should be destroyed.

.. code-block:: c

    tnt_close(tnt);
    tnt_stream_free(tuple);
    tnt_stream_free(tnt);

Function description:

.. cssclass:: highlight
.. parsed-literal::

    void tnt_close(struct tnt_stream \*s)
    void tnt_stream_free(struct tnt_stream \*s)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                         Example 2
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here is a complete C program that selects, using index key :code:`[99999]`, from
space :code:`examples` via the high-level C API.
To display the results, the program uses functions in the
`MsgPuck`_ library which allow decoding of `MessagePack`_  arrays.

.. code-block:: c

    #include <stdio.h>
    #include <stdlib.h>
    #include <tarantool/tarantool.h>
    #include <tarantool/tnt_net.h>
    #include <tarantool/tnt_opt.h>

    #define MP_SOURCE 1
    #include <msgpuck.h>

    void main() {
        struct tnt_stream *tnt = tnt_net(NULL);
        tnt_set(tnt, TNT_OPT_URI, "localhost:3301");
        if (tnt_connect(tnt) < 0) {
            printf("Connection refused\n");
            exit(1);
        }
        struct tnt_stream *tuple = tnt_object(NULL);
        tnt_object_format(tuple, "[%d]", 99999); /* tuple = search key */
        tnt_select(tnt, 999, 0, (2^32) - 1, 0, 0, tuple);
        tnt_flush(tnt);
        struct tnt_reply reply; tnt_reply_init(&reply);
        tnt->read_reply(tnt, &reply);
        if (reply.code != 0) {
            printf("Select failed.\n");
            exit(1);
        }
        char field_type;
        field_type = mp_typeof(*reply.data);
        if (field_type != MP_ARRAY) {
            printf("no tuple array\n");
            exit(1);
        }
        long unsigned int row_count;
        uint32_t tuple_count = mp_decode_array(&reply.data);
        printf("tuple count=%u\n", tuple_count);
        unsigned int i, j;
        for (i = 0; i < tuple_count; ++i) {
            field_type = mp_typeof(*reply.data);
            if (field_type != MP_ARRAY) {
                printf("no field array\n");
                exit(1);
            }
            uint32_t field_count = mp_decode_array(&reply.data);
            printf("  field count=%u\n", field_count);
            for (j = 0; j < field_count; ++j) {
                field_type = mp_typeof(*reply.data);
                if (field_type == MP_UINT) {
                    uint64_t num_value = mp_decode_uint(&reply.data);
                    printf("    value=%lu.\n", num_value);
                } else if (field_type == MP_STR) {
                    const char *str_value;
                    uint32_t str_value_length;
                    str_value = mp_decode_str(&reply.data, &str_value_length);
                    printf("    value=%.*s.\n", str_value_length, str_value);
                } else {
                    printf("wrong field type\n");
                    exit(1);
                }
            }
        }
        tnt_close(tnt);
        tnt_stream_free(tuple);
        tnt_stream_free(tnt);
    }

Similarly to the first example, paste the code into a file named 
:file:`example2.c`.

To compile and link the program, say:

.. code-block:: console

    $ gcc -o example2 example2.c -ltarantool

To run the program, say :code:`./example2`.

The two example programs only show a few requests and do not show all that's
necessary for good practice. See more in the ``tarantool-c`` documentation at 
http://github.com/tarantool/tarantool-c.

.. _MsgPuck: http://rtsisyk.github.io/msgpuck/
.. _MessagePack: https://en.wikipedia.org/wiki/MessagePack
