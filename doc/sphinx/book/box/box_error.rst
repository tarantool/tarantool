-------------------------------------------------------------------------------
                            Package `box.error`
-------------------------------------------------------------------------------

The ``box.error`` function is for raising an error. The difference between this
function and Lua's built-in ``error()`` function is that when the error reaches
the client, its error code is preserved. In contrast, a Lua error would always
be presented to the client as ``ER_PROC_LUA``.

.. module:: box.error

.. function:: box.error{reason=string [, code=number]}

    When called with a Lua-table argument, the code and reason have any
    user-desired values. The result will be those values.

    :param integer  code:
    :param string reason:

.. function:: box.error()

    When called without arguments, ``box.error()`` re-throws whatever the last
    error was.

.. function:: box.error(code, errtext [, errtext ...])

    Emulate a request error, with text based on one of the pre-defined Tarantool
    errors defined in the file `errcode.h
    <https://github.com/tarantool/tarantool/blob/master/src/box/errcode.h>`_ in
    the source tree. Lua constants which correspond to those Tarantool errors are
    defined as members of ``box.error``, for example ``box.error.NO_SUCH_USER == 45``.

    :param number       code: number of a pre-defined error
    :param string errtext(s): part of the message which will accompany the error

    For example:

    the ``NO_SUCH_USER`` message is "``User '%s' is not found``" -- it includes
    one "``%s``" component which will be replaced with errtext. Thus a call to
    ``box.error(box.error.NO_SUCH_USER, 'joe')`` or ``box.error(45, 'joe')``
    will result in an error with the accompanying message
    "``User 'joe' is not found``".

    :except: whatever is specified in errcode-number.

    EXAMPLE


    | :codenormal:`tarantool>` :codebold:`box.error({code=555, reason='Arbitrary message'})`
    | :codenormal:`---`
    | :codenormal:`- error: Arbitrary message`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.error()`
    | :codenormal:`---`
    | :codenormal:`- error: Arbitrary message`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.error(box.error.FUNCTION_ACCESS_DENIED, 'A', 'B', 'C')`
    | :codenormal:`---`
    | :codenormal:`- error: A access denied for user 'B' to function 'C'`
    | :codenormal:`...`

.. function:: box.error.last()

    Returns a description of the last error, as a Lua table
    with three members: "type" (string) error's C++ class,
    "message" (string) error's message, "code" (numeric) error's number.
    Additionally, if the error is a system error (for example due to a
    failure in socket or file io), there is a fourth member:
    "errno" (number) C standard error number.

    rtype: table

.. function:: box.error.clear()

    Clears the record of errors, so functions like `box.error()`
    or `box.error.last()` will have no effect.

    EXAMPLE

    | :codenormal:`tarantool>` :codebold:`box.error({code=555, reason='Arbitrary message'})`
    | :codenormal:`---`
    | :codenormal:`- error: Arbitrary message`
    | :codenormal:`..`
    |
    | :codenormal:`tarantool>` :codebold:`box.error.last()`
    | :codenormal:`---`
    | :codenormal:`- type: ClientError`
    | :codenormal:`message: Arbitrary message`
    | :codenormal:`code: 555`
    | :codenormal:`...`
    |
    | :codenormal:`tarantool>` :codebold:`box.error.clear()`
    | :codenormal:`---`
    | :codenormal:`...`
    |
    | :codenormal:`tarantool>` :codebold:`box.error.last()`
    | :codenormal:`---`
    | :codenormal:`- null`
    | :codenormal:`...`
