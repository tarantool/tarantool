.. _box_schema:

-------------------------------------------------------------------------------
                             Package `box.schema`
-------------------------------------------------------------------------------

.. module:: box.schema

The ``box.schema`` package has data-definition functions
for spaces, users, roles, and function tuples.

.. _schema-space-create:

.. function:: box.schema.space.create(space-name [, {options} ])

    Create a space.

    :param string space-name: name of space, which should not be a number
                                and should not contain special characters
    :param table options: see "Options for box.schema.space.create" chart, below

    :return: space object
    :rtype: userdata

    .. container:: table

        **Options for box.schema.space.create**

        .. rst-class:: left-align-column-1
        .. rst-class:: left-align-column-2
        .. rst-class:: left-align-column-3
        .. rst-class:: left-align-column-4

        +---------------+--------------------------------+---------+---------------------+
        | Name          | Effect                         | Type    | Default             |
        +===============+================================+=========+=====================+
        | temporary     | space is temporary             | boolean | false               |
        +---------------+--------------------------------+---------+---------------------+
        | id            | unique identifier              | number  | last space's id, +1 |
        +---------------+--------------------------------+---------+---------------------+
        | field_count   | fixed field count              | number  | 0 i.e. not fixed    |
        +---------------+--------------------------------+---------+---------------------+
        | if_not_exists | no error if                    | boolean | false               |
        |               | duplicate name                 |         |                     |
        +---------------+--------------------------------+---------+---------------------+
        | engine        | storage engine =               | string  | 'memtx'             |
        |               | :ref:`'memtx' or 'phia'      |         |                     |
        |               | <two-storage-engines>`         |         |                     |
        +---------------+--------------------------------+---------+---------------------+
        | user          | user name                      | string  | current user's name |
        +---------------+--------------------------------+---------+---------------------+
        | format        | field names+types              | table   | (blank)             |
        +---------------+--------------------------------+---------+---------------------+

    :param num space-id: the numeric identifier established by box.schema.space.create

    Note: for symmetry, there are other box.schema functions targeting
    space objects, for example :samp:`box.schema.space.drop({space-id})`
    will drop a space. However, the common approach is to use functions
    attached to the space objects, for example
    :func:`space_object:drop() <space_object.drop>`.

    Note re storage engine: phia does not support temporary spaces.

=================================================
                    Example
=================================================

.. code-block:: tarantoolsession

    tarantool> s = box.schema.space.create('space55')
    ---
    ...
    tarantool> s = box.schema.space.create('space55', {
             >   id = 555,
             >   temporary = false
             > })
    ---
    - error: Space 'space55' already exists
    ...
    tarantool> s = box.schema.space.create('space55', {
             >   if_not_exists = true
             > })

For an illustration with the :code:`format` clause, see
:data:`box.space._space <box.space._space>` example.

After a space is created, usually the next step is to
:func:`create an index <space_object.create_index>` for it, and then it is
available for insert, select, and all the other :mod:`box.space` functions.


.. function:: box.schema.user.create(user-name [, {options} ])

    Create a user.
    For explanation of how Tarantool maintains user data, see
    section :ref:`Users and the _user space <authentication-users>`.

    :param string user-name: name of user, which should not be a number
                                and should not contain special characters
    :param table options: if_not_exists, password

    :return: nil

    **Examples:**

    .. code-block:: lua

        box.schema.user.create('Lena') 
        box.schema.user.create('Lena', {password = 'X'})
        box.schema.user.create('Lena', {if_not_exists = false})

.. function:: box.schema.user.drop(user-name [, {options} ])

    Drop a user.
    For explanation of how Tarantool maintains user data, see
    section :ref:`Users and the _user space <authentication-users>`.

    :param string user-name: the name of the user
    :param table options: if_exists

    **Example:**

    .. code-block:: lua

        box.schema.user.drop('Lena')
        box.schema.user.drop('Lena',{if_exists=false})

.. function:: box.schema.user.exists(user-name)

    Return true if a user exists; return false if a user does not exist.

    :param string user-name: the name of the user
    :rtype: bool

    **Example:**

    .. code-block:: lua

        box.schema.user.exists('Lena')

.. function:: box.schema.user.grant(user-name, privileges)

    Grant :ref:`privileges <privileges>` to a user.

    :param string user-name: the name of the user
    :param string privileges: either privilege,object-type,object-name
                              or privilege,'universe' where privilege =
                              'read' or 'write' or 'execute' or a combination
                              and object-type = 'space' or 'function'.
                              Or: role-name.

    **Examples:**

        box.schema.user.grant('Lena', 'read', 'space', 'tester') |br|
        box.schema.user.grant('Lena', 'execute', 'function', 'f') |br|
        box.schema.user.grant('Lena', 'read,write', 'universe') |br|
        box.schema.user.grant('Lena', 'Accountant')

.. function:: box.schema.user.revoke(user-name, privileges)

    Revoke :ref:`privileges <privileges>` from a user.

    :param string user-name: the name of the user
    :param string privileges: either privilege,object-type,object-name
                              or privilege,'universe' where privilege =
                              'read' or 'write' or 'execute' or a combination
                              and object-type = 'space' or 'function'.
                              Or: role-name.

    **Examples:**

        box.schema.user.revoke('Lena', 'read', 'space', 'tester') |br|
        box.schema.user.revoke('Lena', 'execute', 'function', 'f') |br|
        box.schema.user.revoke('Lena', 'read,write', 'universe') |br|
        box.schema.user.revoke('Lena', 'Accountant')

.. function:: box.schema.user.password(password)

    Return a hash of a password.

    :param string password: password
    :rtype: string

    **Example:**

        box.schema.user.password('ЛЕНА')

.. function:: box.schema.user.passwd([user-name,] password)

    Associate a password with the user who is currently logged in.
    or with another user.
    Users who wish to change their own passwords should
    use box.schema.user.passwd(password).
    Administrators who wish to change passwords of other users should
    use box.schema.user.passwd(user-name, password).

    :param string user-name: user-name
    :param string password: password

    **Examples:**

        box.schema.user.passwd('ЛЕНА') |br|
        box.schema.user.passwd('Lena', 'ЛЕНА')

.. function:: box.schema.user.info([user-name])

    Return a description of a user's privileges.

    :param string user-name: the name of the user.
                             This is optional; if it is not
                             supplied, then the information
                             will be for the user who is
                             currently logged in.

    **Example:**

        box.schema.user.info() |br|
        box.schema.user.info('Lena')

.. function:: box.schema.role.create(role-name [, {options} ])

    Create a role.
    For explanation of how Tarantool maintains role data, see
    section :ref:`Roles <authentication-roles>`.

    :param string role-name: name of role, which should not be a number
                                and should not contain special characters
    :param table options: if_not_exists

    :return: nil

    **Examples:**

        box.schema.role.create('Accountant') |br|
        box.schema.role.create('Accountant', {if_not_exists = false})

.. function:: box.schema.role.drop(role-name)

    Drop a role.
    For explanation of how Tarantool maintains role data, see
    section :ref:`Roles <authentication-roles>`.

    :param string role-name: the name of the role

    **Example:**

        box.schema.role.drop('Accountant')

.. function:: box.schema.role.exists(role-name)

    Return true if a role exists; return false if a role does not exist.

    :param string role-name: the name of the role
    :rtype: bool

    **Example:**

        box.schema.role.exists('Accountant')

.. function:: box.schema.role.grant(role-name, privileges)

    Grant :ref:`privileges <privileges>` to a role.

    :param string role-name: the name of the role
    :param string privileges: either privilege,object-type,object-name
                              or privilege,'universe' where privilege =
                              'read' or 'write' or 'execute' or a combination
                              and object-type = 'space' or 'function'.
                              Or: role-name.

    **Examples:**

        box.schema.role.grant('Accountant', 'read', 'space', 'tester') |br|
        box.schema.role.grant('Accountant', 'execute', 'function', 'f') |br|
        box.schema.role.grant('Accountant', 'read,write', 'universe') |br|
        box.schema.role.grant('public', 'Accountant')

.. function:: box.schema.role.revoke(role-name, privileges)

    Revoke :ref:`privileges <privileges>` to a role.

    :param string role-name: the name of the role
    :param string privileges: either privilege,object-type,object-name
                              or privilege,'universe' where privilege =
                              'read' or 'write' or 'execute' or a combination
                              and object-type = 'space' or 'function'

    **Examples:**

        box.schema.role.revoke('Accountant', 'read', 'space', 'tester') |br|
        box.schema.role.revoke('Accountant', 'execute', 'function', 'f') |br|
        box.schema.role.revoke('Accountant', 'read,write', 'universe') |br|
        box.schema.role.revoke('public', 'Accountant')

.. function:: box.schema.role.info([role-name])

    Return a description of a role's privileges.

    :param string role-name: the name of the role.

    **Example:**

        box.schema.role.info('Accountant')

.. function:: box.schema.func.create(func-name [, {options} ])

    Create a function tuple.
    This does not create the function itself -- that is done with Lua --
    but if it is necessary to grant privileges for a function,
    box.schema.func.create must be done first.
    For explanation of how Tarantool maintains function data, see
    section :ref:`Functions and the _func space <authentication-funcs>`.

    :param string func-name: name of function, which should not be a number
                                and should not contain special characters
    :param table options: if_not_exists, setuid, language

    :return: nil

    **Examples:**

        box.schema.func.create('calculate') |br|
        box.schema.func.create('calculate', {if_not_exists = false}) |br|
        box.schema.func.create('calculate', {setuid = false}) |br|
        box.schema.func.create('calculate', {language = 'LUA'})

.. function:: box.schema.func.drop(func-name)

    Drop a function tuple.
    For explanation of how Tarantool maintains function data, see
    section :ref:`Functions and the _func space <authentication-funcs>`.

    :param string func-name: the name of the function

    **Example:**

        box.schema.func.drop('calculate')

.. function:: box.schema.func.exists(func-name)

    Return true if a function tuple exists; return false if a function tuple does not exist.

    :param string func-name: the name of the function
    :rtype: bool

    **Example:**

        box.schema.func.exists('calculate')
