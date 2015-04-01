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

    .. code-block:: lua

        tarantool> box.error({code=555, reason='Arbitrary message'})
        ---
        - error: Arbitrary message
        ...
        tarantool> box.error()
        ---
        - error: Arbitrary message
        ...
        tarantool> box.error(box.error.FUNCTION_ACCESS_DENIED, 'A', 'B', 'C')
        ---
        - error: A access denied for user 'B' to function 'C'
        ...
