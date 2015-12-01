:title: IProto protocol
:slug: box-protocol
:save_as: doc/box-protocol.html
:url: doc/box-protocol.html
:template: documentation_rst

.. _iproto protocol:

--------------------------------------------------------------------------------
                               IProto Protocol
--------------------------------------------------------------------------------

================================================================================
                              Notion in diagrams
================================================================================

.. code-block:: bash

    0    X
    +----+
    |    | - X bytes
    +----+
     TYPE - type of MsgPack value (if it is MsgPack object)

    +====+
    |    | - Variable size MsgPack object
    +====+
     TYPE - type of MsgPack value

    +~~~~+
    |    | - Variable size MsgPack Array/Map
    +~~~~+
     TYPE - type of MsgPack value


MsgPack data types:

* **MP_INT** - Integer
* **MP_MAP** - Map
* **MP_ARR** - Array
* **MP_STRING** - String
* **MP_FIXSTR** - Fixed size string
* **MP_OBJECT** - Any MsgPack object


================================================================================
                                    Overview
================================================================================

IPROTO is a binary request/response protocol.

================================================================================
                                 Greeting Packet
================================================================================

.. code-block:: bash

    TARANTOOL'S GREETING:

    0                                     63
    +--------------------------------------+
    |                                      |
    | Tarantool Greeting (server version)  |
    |               64 bytes               |
    +---------------------+----------------+
    |                     |                |
    | BASE64 encoded SALT |      NULL      |
    |      44 bytes       |                |
    +---------------------+----------------+
    64                  107              127

The server begins the dialogue by sending a fixed-size (128 bytes) text greeting
to the client. The first 64 bytes of the greeting contain server version. The
second 44 bytes contain a base64-encoded random string, to use in authentication
packet. And it ends with 20 bytes of spaces.

================================================================================
                         Unified packet structure
================================================================================

Once a greeting is read, the protocol becomes pure request/response and features
a complete access to Tarantool functionality, including:

- request multiplexing, e.g. ability to asynchronously issue multiple requests
  via the same connection
- response format that supports zero-copy writes

For data structuring and encoding, the protocol uses msgpack data format, see
http://msgpack.org

Tarantool protocol mandates use of a few integer constants serving as keys in
maps used in the protocol. These constants are defined in `src/box/iproto_constants.h
<https://github.com/tarantool/tarantool/blob/master/src/box/iproto_constants.h>`_

Let's list them here too:

.. code-block:: bash

    -- user keys
    <code>          ::= 0x00
    <sync>          ::= 0x01
    <space_id>      ::= 0x10
    <index_id>      ::= 0x11
    <limit>         ::= 0x12
    <offset>        ::= 0x13
    <iterator>      ::= 0x14
    <key>           ::= 0x20
    <tuple>         ::= 0x21
    <function_name> ::= 0x22
    <username>      ::= 0x23
    <expression>    ::= 0x27
    <ops>           ::= 0x28
    <data>          ::= 0x30
    <error>         ::= 0x31

.. code-block:: bash

    -- -- Value for <code> key in request can be:
    -- User command codes
    <select>  ::= 0x01
    <insert>  ::= 0x02
    <replace> ::= 0x03
    <update>  ::= 0x04
    <delete>  ::= 0x05
    <call>    ::= 0x06
    <auth>    ::= 0x07
    <eval>    ::= 0x08
    <upsert>  ::= 0x09
    -- Admin command codes
    <ping>    ::= 0x40

    -- -- Value for <code> key in response can be:
    <OK>      ::= 0x00
    <ERROR>   ::= 0x8XXX


Both :code:`<header>` and :code:`<body>` are msgpack maps:

.. code-block:: bash

    Request/Response:

    0      5
    +------+ +============+ +===================================+
    |BODY +| |            | |                                   |
    |HEADER| |   HEADER   | |               BODY                |
    | SIZE | |            | |                                   |
    +------+ +============+ +===================================+
     MP_INT      MP_MAP                     MP_MAP

.. code-block:: bash

    UNIFIED HEADER:

    +================+================+
    |                |                |
    |   0x00: CODE   |   0x01: SYNC   |
    | MP_INT: MP_INT | MP_INT: MP_INT |
    |                |                |
    +================+================+
                   MP_MAP

They only differ in the allowed set of keys and values, the key defines the
type of value that follows. If a body has no keys, entire msgpack map for
the body may be missing. Such is the case, for example, in <ping> request.

================================================================================
                            Authentication
================================================================================

When a client connects to the server, the server responds with a 128-byte
text greeting message. Part of the greeting is base-64 encoded session salt -
a random string which can be used for authentication. The length of decoded
salt (44 bytes) exceeds the amount necessary to sign the authentication
message (first 20 bytes). An excess is reserved for future authentication
schemas.

.. code-block:: bash

    PREPARE SCRAMBLE:

        LEN(ENCODED_SALT) = 44;
        LEN(SCRAMBLE)     = 20;

    prepare 'chap-sha1' scramble:

        salt = base64_decode(encoded_salt);
        step_1 = sha1(password);
        step_2 = sha1(step_1);
        step_3 = sha1(salt, step_2);
        scramble = xor(step_1, step_3);
        return scramble;

    AUTHORIZATION BODY: CODE = 0x07

    +==================+====================================+
    |                  |        +-------------+-----------+ |
    |  (KEY)           | (TUPLE)|  len == 9   | len == 20 | |
    |   0x23:USERNAME  |   0x21:| "chap-sha1" |  SCRAMBLE | |
    | MP_INT:MP_STRING | MP_INT:|  MP_STRING  | MP_STRING | |
    |                  |        +-------------+-----------+ |
    |                  |                   MP_ARRAY         |
    +==================+====================================+
                            MP_MAP

:code:`<key>` holds the user name. :code:`<tuple>` must be an array of 2 fields:
authentication mechanism ("chap-sha1" is the only supported mechanism right now)
and password, encrypted according to the specified mechanism. Authentication in
Tarantool is optional, if no authentication is performed, session user is 'guest'.
The server responds to authentication packet with a standard response with 0 tuples.

================================================================================
                                  Requests
================================================================================

* SELECT: CODE - 0x01
  Find tuples matching the search pattern

.. code-block:: bash

    SELECT BODY:

    +==================+==================+==================+
    |                  |                  |                  |
    |   0x10: SPACE_ID |   0x11: INDEX_ID |   0x12: LIMIT    |
    | MP_INT: MP_INT   | MP_INT: MP_INT   | MP_INT: MP_INT   |
    |                  |                  |                  |
    +==================+==================+==================+
    |                  |                  |                  |
    |   0x13: OFFSET   |   0x14: ITERATOR |   0x20: KEY      |
    | MP_INT: MP_INT   | MP_INT: MP_INT   | MP_INT: MP_ARRAY |
    |                  |                  |                  |
    +==================+==================+==================+
                              MP_MAP

* INSERT:  CODE - 0x02
  Inserts tuple into the space, if no tuple with same unique keys exists. Otherwise throw *duplicate key* error.
* REPLACE: CODE - 0x03
  Insert a tuple into the space or replace an existing one.

.. code-block:: bash


    INSERT/REPLACE BODY:

    +==================+==================+
    |                  |                  |
    |   0x10: SPACE_ID |   0x21: TUPLE    |
    | MP_INT: MP_INT   | MP_INT: MP_ARRAY |
    |                  |                  |
    +==================+==================+
                     MP_MAP

* UPDATE: CODE - 0x04
  Update a tuple

.. code-block:: bash

    UPDATE BODY:

    +==================+=======================+
    |                  |                       |
    |   0x10: SPACE_ID |   0x11: INDEX_ID      |
    | MP_INT: MP_INT   | MP_INT: MP_INT        |
    |                  |                       |
    +==================+=======================+
    |                  |          +~~~~~~~~~~+ |
    |                  |          |          | |
    |                  | (TUPLE)  |    OP    | |
    |   0x20: KEY      |    0x21: |          | |
    | MP_INT: MP_ARRAY |  MP_INT: +~~~~~~~~~~+ |
    |                  |            MP_ARRAY   |
    +==================+=======================+
                     MP_MAP

.. code-block:: bash

    OP:
        Works only for integer fields:
        * Addition    OP = '+' . space[key][field_no] += argument
        * Subtraction OP = '-' . space[key][field_no] -= argument
        * Bitwise AND OP = '&' . space[key][field_no] &= argument
        * Bitwise XOR OP = '^' . space[key][field_no] ^= argument
        * Bitwise OR  OP = '|' . space[key][field_no] |= argument
        Works on any fields:
        * Delete      OP = '#'
          delete <argument> fields starting
          from <field_no> in the space[<key>]

    0           2
    +-----------+==========+==========+
    |           |          |          |
    |    OP     | FIELD_NO | ARGUMENT |
    | MP_FIXSTR |  MP_INT  |  MP_INT  |
    |           |          |          |
    +-----------+==========+==========+
                  MP_ARRAY

.. code-block:: bash

        * Insert      OP = '!'
          insert <argument> before <field_no>
        * Assign      OP = '='
          assign <argument> to field <field_no>.
          will extend the tuple if <field_no> == <max_field_no> + 1

    0           2
    +-----------+==========+===========+
    |           |          |           |
    |    OP     | FIELD_NO | ARGUMENT  |
    | MP_FIXSTR |  MP_INT  | MP_OBJECT |
    |           |          |           |
    +-----------+==========+===========+
                  MP_ARRAY

        Works on string fields:
        * Splice      OP = ':'
          take the string from space[key][field_no] and
          substitute <offset> bytes from <position> with <argument>

.. code-block:: bash

    0           2
    +-----------+==========+==========+========+==========+
    |           |          |          |        |          |
    |    ':'    | FIELD_NO | POSITION | OFFSET | ARGUMENT |
    | MP_FIXSTR |  MP_INT  |  MP_INT  | MP_INT |  MP_STR  |
    |           |          |          |        |          |
    +-----------+==========+==========+========+==========+
                             MP_ARRAY


It's an error to specify an argument of a type that differs from expected type.

* DELETE: CODE - 0x05
  Delete a tuple

.. code-block:: bash

    DELETE BODY:

    +==================+==================+==================+
    |                  |                  |                  |
    |   0x10: SPACE_ID |   0x11: INDEX_ID |   0x20: KEY      |
    | MP_INT: MP_INT   | MP_INT: MP_INT   | MP_INT: MP_ARRAY |
    |                  |                  |                  |
    +==================+==================+==================+
                              MP_MAP


* CALL: CODE - 0x06
  Call a stored function

.. code-block:: bash

    CALL BODY:

    +=======================+==================+
    |                       |                  |
    |   0x22: FUNCTION_NAME |   0x21: TUPLE    |
    | MP_INT: MP_STRING     | MP_INT: MP_ARRAY |
    |                       |                  |
    +=======================+==================+
                        MP_MAP


* EVAL: CODE - 0x08
  Evaulate Lua expression

.. code-block:: bash

    EVAL BODY:

    +=======================+==================+
    |                       |                  |
    |   0x27: EXPRESSION    |   0x21: TUPLE    |
    | MP_INT: MP_STRING     | MP_INT: MP_ARRAY |
    |                       |                  |
    +=======================+==================+
                        MP_MAP


* UPSERT: CODE - 0x09
  Update tuple if it would be found elsewhere try to insert tuple. Always use primary index for key.

.. code-block:: bash

    UPSERT BODY:

    +==================+==================+==========================+
    |                  |                  |             +~~~~~~~~~~+ |
    |                  |                  |             |          | |
    |   0x10: SPACE_ID |   0x21: TUPLE    |       (OPS) |    OP    | |
    | MP_INT: MP_INT   | MP_INT: MP_ARRAY |       0x28: |          | |
    |                  |                  |     MP_INT: +~~~~~~~~~~+ |
    |                  |                  |               MP_ARRAY   |
    +==================+==================+==========================+
                                    MP_MAP

    Operations structure same as for UPDATE operation.
       0           2
    +-----------+==========+==========+
    |           |          |          |
    |    OP     | FIELD_NO | ARGUMENT |
    | MP_FIXSTR |  MP_INT  |  MP_INT  |
    |           |          |          |
    +-----------+==========+==========+
                  MP_ARRAY

    Supported operations:

    '+' - add a value to a numeric field. If the filed is not numeric, it's
          changed to 0 first. If the field does not exist, the operation is
          skipped. There is no error in case of overflow either, the value
          simply wraps around in C style. The range of the integer is MsgPack:
          from -2^63 to 2^64-1
    '-' - same as the previous, but subtract a value
    '=' - assign a field to a value. The field must exist, if it does not exist,
          the operation is skipped.
    '!' - insert a field. It's only possible to insert a field if this create no
          nil "gaps" between fields. E.g. it's possible to add a field between
          existing fields or as the last field of the tuple.
    '#' - delete a field. If the field does not exist, the operation is skipped.
          It's not possible to change with update operations a part of the primary
          key (this is validated before performing upsert).


================================================================================
                         Response packet structure
================================================================================

We'll show whole packets here:

.. code-block:: bash


    OK:    LEN + HEADER + BODY

    0      5                                          OPTIONAL
    +------++================+================++===================+
    |      ||                |                ||                   |
    | BODY ||   0x00: 0x00   |   0x01: SYNC   ||   0x30: DATA      |
    |HEADER|| MP_INT: MP_INT | MP_INT: MP_INT || MP_INT: MP_OBJECT |
    | SIZE ||                |                ||                   |
    +------++================+================++===================+
     MP_INT                MP_MAP                      MP_MAP

Set of tuples in the response :code:`<data>` expects a msgpack array of tuples as value
EVAL command returns arbitrary `MP_ARRAY` with arbitrary MsgPack values.

.. code-block:: bash

    ERROR: LEN + HEADER + BODY

    0      5
    +------++================+================++===================+
    |      ||                |                ||                   |
    | BODY ||   0x00: 0x8XXX |   0x01: SYNC   ||   0x31: ERROR     |
    |HEADER|| MP_INT: MP_INT | MP_INT: MP_INT || MP_INT: MP_STRING |
    | SIZE ||                |                ||                   |
    +------++================+================++===================+
     MP_INT                MP_MAP                      MP_MAP

    Where 0xXXX is ERRCODE.

Error message is present in the response only if there is an error :code:`<error>`
expects as value a msgpack string

Convenience macros which define hexadecimal constants for return codes
can be found in `src/box/errcode.h
<https://github.com/tarantool/tarantool/blob/master/src/box/errcode.h>`_

================================================================================
                         Replication packet structure
================================================================================

.. code-block:: bash

    -- replication keys
    <server_id>     ::= 0x02
    <lsn>           ::= 0x03
    <timestamp>     ::= 0x04
    <server_uuid>   ::= 0x24
    <cluster_uuid>  ::= 0x25
    <vclock>        ::= 0x26

.. code-block:: bash

    -- replication codes
    <join>      ::= 0x41
    <subscribe> ::= 0x42


.. code-block:: bash

    JOIN:

    In the beginning you must send JOIN
                             HEADER                          BODY
    +================+================+===================++-------+
    |                |                |    SERVER_UUID    ||       |
    |   0x00: 0x41   |   0x01: SYNC   |   0x24: UUID      || EMPTY |
    | MP_INT: MP_INT | MP_INT: MP_INT | MP_INT: MP_STRING ||       |
    |                |                |                   ||       |
    +================+================+===================++-------+
                   MP_MAP                                   MP_MAP

    Then server, which we connect to, will send last SNAP file by, simply,
    creating a number of INSERTs (with additional LSN and ServerID)
    (don't reply). Then it'll send a vclock's MP_MAP and close a socket.

    +================+================++============================+
    |                |                ||        +~~~~~~~~~~~~~~~~~+ |
    |                |                ||        |                 | |
    |   0x00: 0x00   |   0x01: SYNC   ||   0x26:| SRV_ID: SRV_LSN | |
    | MP_INT: MP_INT | MP_INT: MP_INT || MP_INT:| MP_INT: MP_INT  | |
    |                |                ||        +~~~~~~~~~~~~~~~~~+ |
    |                |                ||               MP_MAP       |
    +================+================++============================+
                   MP_MAP                      MP_MAP

    SUBSCRIBE:

    Then you must send SUBSCRIBE:

                                  HEADER
    +===================+===================+
    |                   |                   |
    |     0x00: 0x41    |    0x01: SYNC     |
    |   MP_INT: MP_INT  |  MP_INT: MP_INT   |
    |                   |                   |
    +===================+===================+
    |    SERVER_UUID    |    CLUSTER_UUID   |
    |   0x24: UUID      |   0x25: UUID      |
    | MP_INT: MP_STRING | MP_INT: MP_STRING |
    |                   |                   |
    +===================+===================+
                     MP_MAP

          BODY
    +================+
    |                |
    |   0x26: VCLOCK |
    | MP_INT: MP_INT |
    |                |
    +================+
          MP_MAP

    Then you must process every query that'll came through other masters.
    Every request between masters will have Additional LSN and SERVER_ID.

================================================================================
                                XLOG / SNAP
================================================================================

XLOG and SNAP have the same format. They start with:

.. code-block:: bash

    SNAP\n
    0.12\n
    Server: e6eda543-eda7-4a82-8bf4-7ddd442a9275\n
    VClock: {1: 0}\n
    \n
    ...

So, **Header** of an SNAP/XLOG consists of:

.. code-block:: bash

    <format>\n
    <format_version>\n
    Server: <server_uuid>\n
    VClock: <vclock_map>\n
    \n


There are two markers: tuple beginning - **0xd5ba0bab** and EOF marker - **0xd510aded**. So, next, between **Header** and EOF marker there's data with the following schema:

.. code-block:: bash

    0            3 4                                         17
    +-------------+========+============+===========+=========+
    |             |        |            |           |         |
    | 0xd5ba0bab  | LENGTH | CRC32 PREV | CRC32 CUR | PADDING |
    |             |        |            |           |         |
    +-------------+========+============+===========+=========+
      MP_FIXEXT2    MP_INT     MP_INT       MP_INT      ---

    +============+ +===================================+
    |            | |                                   |
    |   HEADER   | |                BODY               |
    |            | |                                   |
    +============+ +===================================+
        MP_MAP                     MP_MAP
