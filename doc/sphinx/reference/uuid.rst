-------------------------------------------------------------------------------
                            Package `uuid`
-------------------------------------------------------------------------------

A "UUID" is a `Universally unique identifier`_. If an application requires that
a value be unique only within a single computer or on a single database, then a
simple counter is better than a UUID, because getting a UUID is time-consuming
(it requires a syscall_). For clusters of computers, or widely distributed
applications, UUIDs are better.

The functions that can return a UUID are:

    * :func:`uuid() <uuid.__call>`
    * :func:`uuid.bin()`
    * :func:`uuid.str()`

The functions that can convert between different types of UUID are:

    * :func:`uuid_object:bin() <uuid_object.bin>`
    * :func:`uuid_object:str() <uuid_object.str>`
    * :func:`uuid.fromstr()`
    * :func:`uuid.frombin()`

The function that can determine whether a UUID is an all-zero value is:

    * :func:`uuid_object:isnil() <uuid_object.isnil>`

.. module:: uuid

.. data:: nil

    A nil object

.. function:: __call()

    :return: a UUID
    :rtype: cdata

.. function:: bin()

    :return: a UUID
    :rtype: 16-byte string

.. function:: str()

    :return: a UUID
    :rtype: 36-byte binary string

.. function:: fromstr(uuid_str)

    :param uuid_str: UUID in 36-byte hexadecimal string
    :return: converted UUID
    :rtype: cdata

.. function:: frombin(uuid_bin)

    :param uuid_str: UUID in 16-byte binary string
    :return: converted UUID
    :rtype: cdata

.. class:: uuid_object

    .. method:: bin([byte-order])

        :param byte-order: |br| 'l' - little-endian,
                           |br| 'b' - big-endian,
                           |br| 'h' - endianness depends on host (default),
                           |br| 'n' - endianness depends on network

        :return: UUID converted from cdata input value.
        :rtype: 16-byte binary string

    .. method:: str()

        :return: UUID converted from cdata input value.
        :rtype: 36-byte hexadecimal string

    .. method:: isnil()

        The all-zero UUID value can be expressed as uuid.NULL, or as
        ``uuid.fromstr('00000000-0000-0000-0000-000000000000')``.
        The comparison with an all-zero value can also be expressed as
        ``uuid_with_type_cdata == uuid.NULL``.

        :return: true if the value is all zero, otherwise false.
        :rtype: bool

=================================================
                    Example
=================================================

.. code-block:: tarantoolsession

    tarantool> uuid = require('uuid')
    ---
    ...
    tarantool> uuid(), uuid.bin(), uuid.str()
    ---
    - 16ffedc8-cbae-4f93-a05e-349f3ab70baa
    - !!binary FvG+Vy1MfUC6kIyeM81DYw==
    - 67c999d2-5dce-4e58-be16-ac1bcb93160f
    ...
    tarantool> uu = uuid()
    ---
    ...
    tarantool> #uui:bin(), #uu:str(), type(uu), uu:isnil()
    ---
    - 16
    - 36
    - cdata
    - false
    ...

.. _Universally unique identifier: https://en.wikipedia.org/wiki/Universally_unique_identifier
.. _syscall: https://en.wikipedia.org/wiki/Syscall
