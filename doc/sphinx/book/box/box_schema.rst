-------------------------------------------------------------------------------
                             Package `box.schema`
-------------------------------------------------------------------------------

.. module:: box.schema

The ``box.schema`` package has one data-definition function: ``space.create()``.

.. function:: box.schema.space.create(space-name [, {options} ])

    Create a space.

    :param string space-name: name of space, which should not be a number
                                and should not contain special characters
    :param table options:

    :return: space object
    :rtype: userdata

    .. container:: table

        **Options for box.schema.space.create**

        +---------------+--------------------+---------+---------------------+
        | Name          | Effect             | Type    | Default             |
        +===============+====================+=========+=====================+
        | temporary     | space is temporary | boolean | false               |
        +---------------+--------------------+---------+---------------------+
        | id            | unique identifier  | number  | last space's id, +1 |
        +---------------+--------------------+---------+---------------------+
        | field_count   | fixed field count  | number  | 0 i.e. not fixed    |
        +---------------+--------------------+---------+---------------------+
        | if_not_exists | no error if        | boolean | false               |
        |               | duplicate name     |         |                     |
        +---------------+--------------------+---------+---------------------+
        | engine        | storage package    | string  | 'memtx'             |
        +---------------+--------------------+---------+---------------------+
        | user          | user name          | string  | current user's name |
        +---------------+--------------------+---------+---------------------+
        | format        | field names+types  | table   | (blank)             |
        +---------------+--------------------+---------+---------------------+

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
