-------------------------------------------------------------------------------
                             Package `box.space`
-------------------------------------------------------------------------------

.. module:: box.space

The ``box.space`` package has the data-manipulation functions ``select``,
``insert``, ``replace``, ``update``, ``delete``, ``get``, ``put``. It also has
members, such as id, and whether or not a space is enabled. Package source code
is available in file
`src/box/lua/schema.lua <https://github.com/tarantool/tarantool/blob/master/src/box/lua/schema.lua>`_.

A list of all ``box.space`` functions follows, then comes a list of all
``box.space`` members.

.. class:: space_object

    .. function:: create_index(index-name [, {options} ])

        Create an index. It is mandatory to create an index for a tuple set
        before trying to insert tuples into it, or select tuples from it. The
        first created index, which will be used as the primary-key index, must be
        unique.

        :param string index-name: name of index, which should not be a number and
                                should not contain special characters;
        :param table options:

        :return: index object
        :rtype:  index_object

        .. container:: table

            **Options for box.space...create_index**

            +---------------+--------------------+-----------------------------+---------------------+
            | Name          | Effect             | Type                        | Default             |
            +===============+====================+=============================+=====================+
            | type          | type of index      | string                      | 'TREE'              |
            |               |                    | ('HASH',     'TREE',        |                     |
            |               |                    | 'BITSET',   'RTREE')        |                     |
            |               |                    |                             |                     |
            |               |                    |                             |                     |
            |               |                    |                             |                     |
            +---------------+--------------------+-----------------------------+---------------------+
            | id            | unique identifier  | number                      | last index's id, +1 |
            +---------------+--------------------+-----------------------------+---------------------+
            | unique        | index is unique    | boolean                     | true                |
            +---------------+--------------------+-----------------------------+---------------------+
            | if_not_exists | no error if        | boolean                     | false               |
            |               | duplicate name     |                             |                     |
            +---------------+--------------------+-----------------------------+---------------------+
            | parts         | field-numbers  +   | ``{field_no, 'NUM'|'STR'}`` | ``{1, 'NUM'}``      |
            |               | types              |                             |                     |
            +---------------+--------------------+-----------------------------+---------------------+

        Possible errors: too many parts. A type option other than TREE, or a
        unique option other than unique, or a parts option with more than one
        field component, is only applicable for the memtx storage engine.

        | :codenormal:`tarantool>` :codebold:`s = box.space.space55`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:create_index('primary', {unique = true, parts = {1, 'NUM', 2, 'STR'}})`
        | :codenormal:`---`
        | :codenormal:`...`

    .. function:: insert(tuple)

        Insert a tuple into a space.

        :param space_object space-object:
        :param lua-table,box.tuple tuple: tuple to be inserted.
        :return: the inserted tuple
        :rtype:  tuple

        Possible errors: If a tuple with the same unique-key value already exists,
        returns :errcode:`ER_TUPLE_FOUND`.

        Example: :codebold:`box.space.tester:insert{5000,'tuple number five thousand'}`

    .. function:: select(key)

        Search for a tuple or a set of tuples in the given space.

        :param space_object space-object:
        :param lua-table,scalar key: key to be matched against the index
                                     key, which may be multi-part.

        :return: the tuples whose primary-key fields are equal to the passed
                 field-values. If the number of passed field-values is less
                 than the number of fields in the primary key, then only the
                 passed field-values are compared, so ``select{1,2}`` will match
                 a tuple whose primary key is ``{1,2,3}``.
        :rtype:  tuple

        Possible errors: No such space; wrong type.

        Complexity Factors: Index size, Index type.

        | :codebold:`EXAMPLE`
        |
        | :codenormal:`tarantool>` :codebold:`s = box.schema.space.create('tmp', {temporary=true})`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:create_index('primary',{parts = {1,'NUM', 2, 'STR'}})`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:insert{1,'A'}`
        | :codenormal:`---`
        | :codenormal:`- [1, 'A']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:insert{1,'B'}`
        | :codenormal:`---`
        | :codenormal:`- [1, 'B']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:insert{1,'C'}`
        | :codenormal:`---`
        | :codenormal:`- [1, 'C']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:insert{2,'D'}`
        | :codenormal:`---`
        | :codenormal:`- [2, 'D']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`-- must equal both primary-key fields`
        | :codenormal:`tarantool>` :codebold:`s:select{1,'B'}`
        | :codenormal:`---`
        | :codenormal:`- - [1, 'B']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`-- must equal only one primary-key field`
        | :codenormal:`tarantool>` :codebold:`s:select{1}`
        | :codenormal:`---`
        | :codenormal:`- - [1, 'A']`
        | |nbsp| |nbsp| :codenormal:`- [1, 'B']`
        | |nbsp| |nbsp| :codenormal:`- [1, 'C']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`-- must equal 0 fields, so returns all tuples`
        | :codenormal:`tarantool>` :codebold:`s:select{}`
        | :codenormal:`---`
        | :codenormal:`- - [1, 'A']`
        | |nbsp| |nbsp| :codenormal:`- [1, 'B']`
        | |nbsp| |nbsp| :codenormal:`- [1, 'C']`
        | |nbsp| |nbsp| :codenormal:`- [2, 'D']`
        | :codenormal:`...`

        For examples of complex ``select`` requests, where one can specify which index to
        search and what condition to use (for example "greater than" instead of
        "equal to") and how many tuples to return, see the later section
        :ref:`box.space.space-name[.index.index-name]:select <index_select>`.

    .. function:: get(key)

        Search for a tuple in the given space.

        :param space_object space-object:
        :param lua-table,scalar key: key to be matched against the index
                                        key, which may be multi-part.
        :return: the selected tuple.
        :rtype:  tuple

        Possible errors: If space-name does not exist.

        Complexity Factors: Index size, Index type,
        Number of indexes accessed, WAL settings.

        The :codenormal:`box.space...select` function returns a set
        of tuples as a Lua table; the :codenormal:`box.space...get`
        function returns a single tuple. And it is possible to get
        the first tuple in a tuple set by appending "[1]".
        Therefore :codenormal:`box.space.tester:get{1}` has the same
        effect as :codenormal:`box.space.tester:select{1}[1]`, and
        may serve as a convenient shorthand. 

        Example: :codenormal:`tarantool>` :codebold:`box.space.tester:get{1}`

    .. function:: drop()

        Drop a space.

        :param space_object space-object:

        :return: nil

        Possible errors: If space-name does not exist.

        Complexity Factors: Index size, Index type,
        Number of indexes accessed, WAL settings.

        Example: :codenormal:`tarantool>` :codebold:`box.space.space_that_does_not_exist:drop()`

    .. function:: rename(space-name)

        Rename a space.

        :param space_object space-object:
        :param string space-name: new name for space.

        :return: nil

        Possible errors: ``space-name`` does not exist.

        | :codebold:`EXAMPLE`
        |
        | :codenormal:`tarantool>` :codebold:`box.space.space55:rename('space56')`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.space56:rename('space55')`
        | :codenormal:`---`
        | :codenormal:`...`

    .. function:: replace(tuple)
                  put(tuple)

        Insert a tuple into a space. If a tuple with the same primary key already
        exists, ``box.space...:replace()`` replaces the existing tuple with a new
        one. The syntax variants ``box.space...:replace()`` and
        ``box.space...:put()`` have the same effect; the latter is sometimes used
        to show that the effect is the converse of ``box.space...:get()``.

        :param space_object space-object:
        :param lua-table,box.tuple tuple: tuple to be inserted.

        :return: the inserted tuple.
        :rtype:  tuple

        Possible errors: If a different tuple with the same unique-key
        value already exists, returns :errcode:`ER_TUPLE_FOUND`. (This
        would only happen if there was a secondary index. By default
        secondary indexes are unique)

        Complexity Factors: Index size, Index type,
        Number of indexes accessed, WAL settings.

        Example: :codenormal:`tarantool>` :codebold:`box.space.tester:replace{5000, 'New value'}`

    .. function:: update(key, {{operator, field_no, value}, ...})

        Update a tuple.

        The ``update`` function supports operations on fields — assignment,
        arithmetic (if the field is unsigned numeric), cutting and pasting
        fragments of a field, deleting or inserting a field. Multiple
        operations can be combined in a single update request, and in this
        case they are performed atomically and sequentially. Each operation
        requires specification of a field number. When multiple operations
        are present, the field number for each operation is assumed to be
        relative to the most recent state of the tuple, that is, as if all
        previous operations in a multi-operation update have already been
        applied. In other words, it is always safe to merge multiple :codenormal:`update`
        invocations into a single invocation, with no change in semantics.

        Possible operators are:

            * '+' for addition (values must be numeric)
            * '-' for subtraction (values must be numeric)
            * '&' for bitwise AND (values must be unsigned numeric)
            * '|' for bitwise OR (values must be unsigned numeric)
            * '^' for bitwise :abbr:`XOR(exclusive OR)` (values must be unsigned numeric)
            * ':' for string splice
            * '!' for insertion
            * '#' for deletion
            * '=' for assignment

        For “!” and “=” operations the field number can be -1, meaning the last field in the tuple.

        :param space_object space-object:
        :param lua-value key: primary-key field values, must be passed as a Lua
                              table if key is multi-part
        :param table {operator, field_no, value}: a group of arguments for each
                operation, indicating what the operation is, what field the
                operation will apply to, and what value will be applied. The
                field number can be negative, meaning the position from the
                end of tuple (#tuple + negative field number + 1).

        :return: the updated tuple.
        :rtype:  tuple

        Possible errors: it is illegal to modify a primary-key field.

        Complexity Factors: Index size, Index type, number of indexes accessed, WAL
        settings.

        Thus in the instruction :codenormal:`s:update(44, {{'+',1,55},{'=',3,'x'}})`
        the primary-key value is 44, the operators are '+' and '=' meaning
        "add a value to a field and then assign a value to a field", the first
        affected field is field 1 and the value which will be added to it is
        55, the second affected field is field 3 and the value which will be
        assigned to it is 'x'.

        | :codebold:`EXAMPLE`
        |
        | :codenormal:`-- Assume that the initial state of the database is ...`
        | :codenormal:`--   tester has one tuple set and one primary key whose type is 'NUM'.`
        | :codenormal:`--   There is one tuple, with field[1] = 999 and field[2] = 'A'.`
        |
        | :codenormal:`-- In the following update ...`
        | :codenormal:`--   The first argument is tester, that is, the affected space is tester`
        | :codenormal:`--   The second argument is 999, that is, the affected tuple is identified by`
        | :codenormal:`--     primary key value = 999`
        | :codenormal:`--   The third argument is '=', that is, there is one operation, assignment`
        | :codenormal:`--     to a field`
        | :codenormal:`--   The fourth argument is 2, that is, the affected field is field[2]`
        | :codenormal:`--   The fifth argument is 'B', that is, field[2] contents change to 'B'`
        | :codenormal:`--   Therefore, after the following update, field[1] = 999 and field[2] = 'B'.`
        | :codenormal:`box.space.tester:update(999, {{'=', 2, 'B'}})`
        |
        | :codenormal:`-- In the following update, the arguments are the same, except that ...`
        | :codenormal:`--   the key is passed as a Lua table (inside braces). This is unnecessary`
        | :codenormal:`--   when the primary key has only one field, but would be necessary if the`
        | :codenormal:`--   primary key had more than one field.`
        | :codenormal:`--   Therefore, after the following update, field[1] = 999 and field[2] = 'B'`
        | :codenormal:`--     (no change).`
        | :codenormal:`box.space.tester:update({999}, {{'=', 2, 'B'}})`
        |
        | :codenormal:`-- In the following update, the arguments are the same, except that ...`
        | :codenormal:`--    The fourth argument is 3, that is, the affected field is field[3].`
        | :codenormal:`--    It is okay that, until now, field[3] has not existed. It gets added.`
        | :codenormal:`--    Therefore, after the following update, field[1] = 999, field[2] = 'B',`
        | :codenormal:`--      field[3] = 1.`
        | :codenormal:`box.space.tester:update({999}, {{'=', 3, 1}})`
        |
        | :codenormal:`-- In the following update, the arguments are the same, except that ...`
        | :codenormal:`--    The third argument is '+', that is, the operation is addition rather`
        | :codenormal:`--      than assignment.`
        | :codenormal:`--    Since field[3] previously contained 1, this means we're adding 1 to 1.`
        | :codenormal:`--    Therefore, after the following update, field[1] = 999, field[2] = 'B',`
        | :codenormal:`--      field[3] = 2.`
        | :codenormal:`box.space.tester:update({999}, {{'+', 3, 1}})`
        |
        | :codenormal:`-- In the following update ...`
        | :codenormal:`--    The idea is to modify two fields at once.`
        | :codenormal:`--    The formats are '|' and '=', that is, there are two operations, OR and`
        | :codenormal:`--      assignment.`
        | :codenormal:`--    The fourth and fifth arguments mean that field[3] gets ORed with 1.`
        | :codenormal:`--    The seventh and eighth arguments mean that field[2] gets assigned 'C'.`
        | :codenormal:`--    Therefore, after the following update, field[1] = 999, field[2] = 'C',`
        | :codenormal:`--      field[3] = 3.`
        | :codenormal:`box.space.tester:update({999}, {{'|', 3, 1}, {'=', 2, 'C'}})`
        |
        | :codenormal:`-- In the following update ...`
        | :codenormal:`--    The idea is to delete field[2], then subtract 3 from field[3], but ...`
        | :codenormal:`--    after the delete, there is a renumbering, so field[3] becomes field[2]`
        | :codenormal:`--    before we subtract 3 from it, and that's why the seventh argument is 2 not 3.`
        | :codenormal:`--    Therefore, after the following update, field[1] = 999, field[2] = 0.`
        | :codenormal:`box.space.tester:update({999}, {{'#', 2, 1}, {'-', 2, 3}})`
        |
        | :codenormal:`-- In the following update ...`
        | :codenormal:`--    We're making a long string so that splice will work in the next example.`
        | :codenormal:`--    Therefore, after the following update, field[1] = 999, field[2] = 'XYZ'.`
        | :codenormal:`box.space.tester:update({999}, {{'=', 2, 'XYZ'}})`
        |
        | :codenormal:`-- In the following update ...`
        | :codenormal:`--    The third argument is ':', that is, this is the example of splice.`
        | :codenormal:`--    The fourth argument is 2 because the change will occur in field[2].`
        | :codenormal:`--    The fifth argument is 2 because deletion will begin with the second byte.`
        | :codenormal:`--    The sixth argument is 1 because the number of bytes to delete is 1.`
        | :codenormal:`--    The seventh argument is '!!' because '!!' is to be added at this position.`
        | :codenormal:`--    Therefore, after the following update, field[1] = 999, field[2] = 'X!!Z'.`
        | :codenormal:`box.space.tester:update({999}, {{':', 2, 2, 1, '!!'}})`


    .. function:: delete(key)

        Delete a tuple identified by a primary key.

        :param space_object space-object:
        :param lua-table,scalar key: key to be matched against the index
                                        key, which may be multi-part.

        :return: the deleted tuple
        :rtype:  tuple

        Complexity Factors: Index size, Index type

        | :codebold:`Example:`
        |
        | :codenormal:`tarantool>` :codebold:`box.space.tester:delete(0)`
        | :codenormal:`---`
        | :codenormal:`- [0, 'My first tuple']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.tester:delete(0)`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.tester:delete('a')`
        | :codenormal:`---`
        | :codenormal:`- error: 'Supplied key type of part 0 does not match index part type:`
        |   :codenormal:`expected NUM'`
        | :codenormal:`...`

.. _space_object_id:

    space-object.id

        Ordinal space number. Spaces can be referenced by either name or
        number. Thus, if space 'tester' has id = 800, then
        ``box.space.tester:insert{0}`` and ``box.space[800]:insert{0}``
        are equivalent requests.

        :rtype: number

.. _space_object_enabled:

    space-object.enabled

        Whether or not this space is enabled.
        The value is :codenormal:`false` if there is no index.

        :rtype: boolean

.. _space_object_field_count:

    space-object.field_count

        The required field count for all tuples in this space. The field_count
        can be set initially with
        ``box.schema.space.create... field_count = new-field-count-value ...``.
        The default value is 0, which means there is no required field count.

        :rtype: number

.. _space_object_index:

    space-object.index[]

        A container for all defined indexes. An index is a Lua object of type
        :mod:`box.index` with methods to search tuples and iterate over them in
        predefined order.

        :rtype: table

        | :codebold:`Example`
        |
        | :codenormal:`tarantool>` :codebold:`box.space.tester.id`
        | :codenormal:`---`
        | :codenormal:`- 512`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.tester.field_count`
        | :codenormal:`---`
        | :codenormal:`- 0`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.tester.index.primary.type`
        | :codenormal:`---`
        | :codenormal:`- TREE`
        | :codenormal:`...`

.. _space_object_len:

    space-object:len()

        :return: Number of tuples in the space.

        | :codebold:`Example`
        |
        | :codenormal:`tarantool>` :codebold:`box.space.tester:len()`
        | :codenormal:`---`
        | :codenormal:`- 2`
        | :codenormal:`...`

.. _space_object_truncate:

    space-object:truncate()

        Deletes all tuples.

        Complexity Factors: Index size, Index type, Number of tuples accessed.

        :return: nil

        | :codebold:`Example`
        |
        | :codenormal:`tarantool>` :codebold:`box.space.tester:truncate()`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.tester:len()`
        | :codenormal:`---`
        | :codenormal:`- 0`
        | :codenormal:`...`


.. _space_object_inc:

    space-object:inc{field-value [, field-value ...]}

        Increments a counter in a tuple whose primary key matches the
        field-value(s). The field following the primary-key fields
        will be the counter. If there is no tuple matching the
        ``field-value(s)``, a new one is inserted with initial counter
        value set to 1.

        :param space_object space-object:
        :param lua-table,scalar field-value(s): values which must match the primary key

        :return: the new counter value
        :rtype:  number

        Complexity Factors: Index size, Index type, WAL settings.


        | :codebold:`Example`
        |
        | :codenormal:`tarantool>` :codebold:`s = box.schema.space.create('forty_second_space')`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:create_index('primary', {unique = true, parts = {1, 'NUM', 2, 'STR'}})`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.forty_second_space:inc{1,'a'}`
        | :codenormal:`---`
        | :codenormal:`- 1`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.forty_second_space:inc{1,'a'}`
        | :codenormal:`---`
        | :codenormal:`- 2`
        | :codenormal:`...`

.. _space_object_dec:

    space-object:dec{field-value [, field-value ...]}

        Decrements a counter in a tuple whose primary key matches the
        ``field-value(s)``. The field following the primary-key fields
        will be the counter. If there is no tuple matching the
        ``field-value(s)``, a new one is not inserted. If the counter value drops
        to zero, the tuple is deleted.

        :param space_object space-object:
        :param lua-table,scalar key: key to be matched against the index
                                        key, which may be multi-part.
        :return: the new counter value
        :rtype:  number

        Complexity Factors: Index size, Index type, WAL settings.

        | :codebold:`Example`
        |
        | :codenormal:`tarantool>` :codebold:`s = box.schema.space.create('space19')`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:create_index('primary', {unique = true, parts = {1, 'NUM', 2, 'STR'}})`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.space19:insert{1,'a',1000}`
        | :codenormal:`---`
        | :codenormal:`- [1, 'a', 1000]`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.space19:dec{1,'a'}`
        | :codenormal:`---`
        | :codenormal:`- 999`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.space19:dec{1,'a'}`
        | :codenormal:`---`
        | :codenormal:`- 998`
        | :codenormal:`...`

.. _space_object_auto_increment:

    space-object:auto_increment{field-value [, field-value ...]}

        Insert a new tuple using an auto-increment primary key. The space specified
        by space-name must have a NUM primary key index of type TREE. The
        primary-key field will be incremented before the insert.
        This is only applicable for the memtx storage engine.

        :param space_object space-object:
        :param lua-table,box.tuple tuple: tuple's fields, other than the primary-key field.

        :return: the inserted tuple.
        :rtype:  tuple

        Complexity Factors: Index size, Index type,
        Number of indexes accessed, WAL settings.

        Possible errors: index has wrong type or primary-key indexed field is not a number.

        | :codebold:`Example`
        |
        | :codenormal:`tarantool>` :codebold:`box.space.tester:auto_increment{'Fld#1', 'Fld#2'}`
        | :codenormal:`---`
        | :codenormal:`- [1, 'Fld#1', 'Fld#2']`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.space.tester:auto_increment{'Fld#3'}`
        | :codenormal:`---`
        | :codenormal:`- [2, 'Fld#3']`
        | :codenormal:`...`

.. _space_object_pairs:

    space-object:pairs()

    .. function:: pairs()

        A helper function to prepare for iterating over all tuples in a space.

        :return: function which can be used in a for/end loop. Within the loop, a value is returned for each iteration.
        :rtype:  function, tuple

        | :codebold:`Example`
        |
        | :codenormal:`tarantool>` :codebold:`s = box.schema.space.create('space33')`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`-- index 'X' has default parts {1,'NUM'}`
        | :codenormal:`tarantool>` :codebold:`s:create_index('X', {})`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`s:insert{0,'Hello my '}; s:insert{1,'Lua world'}`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`tmp = ''; for k, v in s:pairs() do tmp = tmp .. v[2] end`
        | :codenormal:`---`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`tmp`
        | :codenormal:`---`
        | :codenormal:`- Hello my Lua world`
        | :codenormal:`...`

.. data::     _schema

    ``_schema`` is a system tuple set. Its single tuple contains these fields:
    ``'version', major-version-number, minor-version-number``.

    EXAMPLE: The following function will display all fields in all tuples of ``_schema``.

    | :codenormal:`console = require('console'); console.delimiter('!')`
    | :codenormal:`function example()`
    | |nbsp| |nbsp| :codenormal:`local ta = {}, i, line`
    | |nbsp| |nbsp| :codenormal:`for k, v in box.space._schema:pairs() do`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`i = 1`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`line = ''`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`while i <= #v do line = line .. v[i] .. ' ' i = i + 1 end`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`table.insert(ta, line)`
    | |nbsp| |nbsp| :codenormal:`end`
    | |nbsp| |nbsp| :codenormal:`return ta`
    | :codenormal:`end!`
    | :codenormal:`console.delimiter('')!`

    Here is what ``example()`` returns in a typical installation:

    | :codenormal:`tarantool>` :codebold:`example()`
    | :codenormal:`---`
    | :codenormal:`- - 'cluster 1ec4e1f8-8f1b-4304-bb22-6c47ce0cf9c6 '`
    | :codenormal:`- 'max_id 520 '`
    | :codenormal:`- 'version 1 6 '`
    | :codenormal:`...`

.. data::     _space

    ``_space`` is a system tuple set. Its tuples contain these fields:
    ``id, uid, space-name, engine, field_count, temporary, format``.
    These fields are established by :ref:`box.schema.space.create <box.schema.space.create>`.

    EXAMPLE. The following function will display all simple fields
    in all tuples of ``_space``.

    | :codenormal:`console = require('console'); console.delimiter('!')`
    | :codenormal:`function example()`
    | |nbsp| |nbsp| :codenormal:`local ta = {}, i, line`
    | |nbsp| |nbsp| :codenormal:`for k, v in box.space._space:pairs() do`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`i = 1`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`line = ''`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`while i <= #v do`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`if type(v[i]) ~= 'table' then`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`line = line .. v[i] .. ' '`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`end`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`i = i + 1`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`end`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`table.insert(ta, line)`
    | |nbsp| |nbsp| |nbsp| |nbsp|:codenormal:`end`
    | |nbsp| |nbsp| :codenormal:`return ta`
    | :codenormal:`end!`
    | :codenormal:`console.delimiter('')!`

    Here is what ``example()`` returns in a typical installation:

    | :codenormal:`tarantool>` :codebold:`example()`
    | :codenormal:`---`
    | :codenormal:`- - '272 1 _schema memtx 0  '`
    | :codenormal:`- '280 1 _space memtx 0  '`
    | :codenormal:`- '288 1 _index memtx 0  '`
    | :codenormal:`- '296 1 _func memtx 0  '`
    | :codenormal:`- '304 1 _user memtx 0  '`
    | :codenormal:`- '312 1 _priv memtx 0  '`
    | :codenormal:`- '320 1 _cluster memtx 0  '`
    | :codenormal:`- '512 1 tester memtx 0  '`
    | :codenormal:`- '513 1 origin sophia 0  '`
    | :codenormal:`- '514 1 archive memtx 0  '`
    | :codenormal:`...`

.. _boxspaceexample:

    EXAMPLE. The following requests will create a space using
    :code:`box.schema.space.create` with a :code:`format` clause.
    Then it retrieves the _space tuple for the new space.
    This illustrates the typical use of the :code:`format` clause,
    it shows the recommended names and data types for the fields.

    | :codenormal:`tarantool>` :codebold:`box.schema.space.create('TM',{format={[1]={["name"]="field#1"},[2]={["type"]="num"}}})`
    | :codenormal:`---`
    | :codenormal:`- index: []`
    | |nbsp| |nbsp| :codenormal:`on_replace: 'function: 0x41c67338'`
    | |nbsp| |nbsp| :codenormal:`temporary: false`
    | |nbsp| |nbsp| :codenormal:`id: 522`
    | |nbsp| |nbsp| :codenormal:`engine: memtx`
    | |nbsp| |nbsp| :codenormal:`enabled: false`
    | |nbsp| |nbsp| :codenormal:`name: TM`
    | |nbsp| |nbsp| :codenormal:`field_count: 0`
    | :codenormal:`- created`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space._space:select(522)`
    | :codenormal:`---`
    | :codenormal:`- - [522, 1, 'TM', 'memtx', 0, '', [{'name': 'field#1'}, {'type': 'num'}]]`
    | :codenormal:`...`

.. data::     _index

    ``_index`` is a system tuple set. Its tuples contain these fields:
    ``space-id index-id index-name index-type index-is-unique
    index-field-count [tuple-field-no, tuple-field-type ...]``.

    The following function will display all fields in all tuples of _index.

    | :codenormal:`console = require('console'); console.delimiter('!')`
    | :codenormal:`function example()`
    | |nbsp| |nbsp| :codenormal:`local ta = {}, i, line`
    | |nbsp| |nbsp| :codenormal:`for k, v in box.space._index:pairs() do`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`i = 1`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`line = ''`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`while i <= #v do line = line .. v[i] .. ' ' i = i + 1 end`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`table.insert(ta, line)`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`end`
    | |nbsp| |nbsp| :codenormal:`return ta`
    | :codenormal:`end!`
    | :codenormal:`console.delimiter('')!`

    Here is what ``example()`` returns in a typical installation:


    | :codenormal:`tarantool>` :codebold:`example()`
    | :codenormal:`---`
    | :codenormal:`- - '272 0 primary tree 1 1 0 str '`
    | :codenormal:`- '280 0 primary tree 1 1 0 num '`
    | :codenormal:`- '280 1 owner tree 0 1 1 num '`
    | :codenormal:`- '280 2 name tree 1 1 2 str '`
    | :codenormal:`- '288 0 primary tree 1 2 0 num 1 num '`
    | :codenormal:`- '288 2 name tree 1 2 0 num 2 str '`
    | :codenormal:`- '296 0 primary tree 1 1 0 num '`
    | :codenormal:`- '296 1 owner tree 0 1 1 num '`
    | :codenormal:`- '296 2 name tree 1 1 2 str '`
    | :codenormal:`- '304 0 primary tree 1 1 0 num '`
    | :codenormal:`- '304 1 owner tree 0 1 1 num '`
    | :codenormal:`- '304 2 name tree 1 1 2 str '`
    | :codenormal:`- '312 0 primary tree 1 3 1 num 2 str 3 num '`
    | :codenormal:`- '312 1 owner tree 0 1 0 num '`
    | :codenormal:`- '312 2 object tree 0 2 2 str 3 num '`
    | :codenormal:`- '320 0 primary tree 1 1 0 num '`
    | :codenormal:`- '320 1 uuid tree 1 1 1 str '`
    | :codenormal:`- '512 0 primary tree 1 1 0 num '`
    | :codenormal:`- '513 0 first tree 1 1 0 NUM '`
    | :codenormal:`- '514 0 first tree 1 1 0 STR '`
    | :codenormal:`...`

.. data::     _user

    ``_user`` is a new system tuple set for
    support of the :ref:`authorization feature <box-authentication>`.

.. data::     _priv

    ``_priv`` is a new system tuple set for
    support of the :ref:`authorization feature <box-authentication>`.

.. data::     _cluster

    ``_cluster`` is a new system tuple set
    for support of the :ref:`replication feature <box-replication>`.

===================================================================
                     Example showing use of the box.space functions
===================================================================

This function will illustrate how to look at all the spaces, and for each
display: approximately how many tuples it contains, and the first field of
its first tuple. The function uses Tarantool ``box.space`` functions ``len()``
and ``pairs()``. The iteration through the spaces is coded as a scan of the
``_space`` system tuple set, which contains metadata. The third field in
``_space`` contains the space name, so the key instruction
"``space_name = v[3]``" means "``space_name`` = the ``space_name`` field in
the tuple of ``_space`` that we've just fetched with ``pairs()``". The function
returns a table.

.. code-block:: lua

    console = require('console'); console.delimiter('!')
    function example()
        local tuple_count, space_name, line
        local ta = {}
        for k, v in box.space._space:pairs() do
            space_name = v[3]
            if box.space[space_name].index[0] ~= nil then
                tuple_count = box.space[space_name]:len()
            else
                tuple_count = 0
            end
            line = space_name .. ' tuple_count =' .. tuple_count
            if tuple_count > 0 then
                for k1, v1 in box.space[space_name]:pairs() do
                    line = line .. '. first field in first tuple = ' .. v1[1]
                    break
                end
            end
            table.insert(ta, line)
        end
        return ta
    end!
    console.delimiter('')!

... And here is what happens when one invokes the function:

    | :codenormal:`tarantool>` :codebold:`example()`
    | :codenormal:`---`
    | :codenormal:`- - _schema tuple_count =3. first field in first tuple = cluster`
    | :codenormal:`- _space tuple_count =15. first field in first tuple = 272`
    | :codenormal:`- _index tuple_count =25. first field in first tuple = 272`
    | :codenormal:`- _func tuple_count =1. first field in first tuple = 1`
    | :codenormal:`- _user tuple_count =4. first field in first tuple = 0`
    | :codenormal:`- _priv tuple_count =6. first field in first tuple = 1`
    | :codenormal:`- _cluster tuple_count =1. first field in first tuple = 1`
    | :codenormal:`- tester tuple_count =2. first field in first tuple = 1`
    | :codenormal:`- origin tuple_count =0`
    | :codenormal:`- archive tuple_count =13. first field in first tuple = test_0@tarantool.org`
    | :codenormal:`- space55 tuple_count =0`
    | :codenormal:`- tmp tuple_count =0`
    | :codenormal:`- forty_second_space tuple_count =1. first field in first tuple = 1`
    | :codenormal:`...`
