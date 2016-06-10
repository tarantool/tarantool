.. _box_index:

-------------------------------------------------------------------------------
                            Package `box.index`
-------------------------------------------------------------------------------

The ``box.index`` package provides read-only access for index definitions and
index keys. Indexes are contained in :samp:`box.space.{space-name}.index` array within
each space object. They provide an API for ordered iteration over tuples. This
API is a direct binding to corresponding methods of index objects of type
``box.index`` in the storage engine.

.. module:: box.index

.. class:: index_object

    .. data:: unique

        True if the index is unique, false if the index is not unique.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`.

        :rtype: boolean

    .. data:: type

        Index type, 'TREE' or 'HASH' or 'BITSET' or 'RTREE'.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`.

    .. data:: parts

        An array describing index key fields.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`.

        :rtype: table

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester.index.primary
            ---
            - unique: true
              parts:
              - type: NUM
                fieldno: 1
              id: 0
              space_id: 513
              name: primary
              type: TREE
            ...

.. _index-pairs:

    .. method:: pairs(bitset-value | search-value, iterator-type)

        This method provides iteration support within an index.
        The :codeitalic:`bitset-value` or :codeitalic:`search-value` parameter
        specifies what must match within the index.
        The :codeitalic:`iterator-type`
        parameter specifies the rule for matching and ordering. Different index types support
        different iterators. For example, a TREE index maintains a
        strict order of keys and can return all tuples in ascending or descending
        order, starting from the specified key. Other index types, however, do not
        support ordering.

        To understand consistency of tuples returned by an iterator, it's essential
        to know the principles of the Tarantool transaction processing subsystem. An
        iterator in Tarantool does not own a consistent read view. Instead, each
        procedure is granted exclusive access to all tuples and spaces until
        there is a "context switch": which may happen due to
        :ref:`the-implicit-yield-rules <the-implicit-yield-rules>`,
        or by an
        explicit call to :func:`fiber.yield`. When the execution flow returns
        to the yielded procedure, the data set could have changed significantly.
        Iteration, resumed after a yield point, does not preserve the read view,
        but continues with the new content of the database.
        The tutorial :ref:`Indexed pattern search <tutorial-indexed-pattern-search>`
        shows one way that iterators and yields can be used together.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`{bitset-value} | {search-value...}` = what to search for
        * :samp:`{iterator-type}` = as defined in tables below.

        :return: this method returns an iterator closure, i.e. a function which can
                be used to get the next value on each invocation
        :rtype:  function, tuple

        Possible errors: Selected iteration type is not supported for the index type,
        or search value is not supported for the iteration type.

        Complexity Factors: Index size, Index type, Number of tuples accessed.

        A search-value can be a number (for example ``1234``), a string
        (for example ``'abcd'``),
        or a table of numbers and strings (for example ``{1234, 'abcd'}``).
        Each part of a search-value will be compared to each part of an index key.

        .. container:: table

            **Iterator types for TREE indexes**

            Note: Formally the logic for TREE index searches is: |br|
            comparison-operator is = or >= or > or <= or < depending on iterator-type |br|
            for i = 1 to number-of-parts-of-search-value |br|
            |nbsp|  if (search-value-part[i] is ``nil`` and <comparison-operator> is "=") |br|
            |nbsp|  or (search-value-part[i] <comparison-operator> index-key-part[i] is true) |br|
            |nbsp|  then comparison-result[i] is true |br|
            if all comparison-results are true, then search-value "matches" index key. |br|
            Notice how, according to this logic, regardless what the index-key-part contains,
            the comparison-result for equality is always true when a search-value-part is ``nil``
            or is missing. This behavior of searches with nil is subject to change.

            Note re storage engine: phia does not allow search-value-parts to be ``nil`` or missing.

            .. rst-class:: left-align-column-1
            .. rst-class:: left-align-column-2
            .. rst-class:: left-align-column-3

            +---------------+-----------+---------------------------------------------+
            | Type          | Arguments | Description                                 |
            +===============+===========+=============================================+
            | box.index.EQ  | search    | The comparison operator is '==' (equal to). |
            | or 'EQ'       | value     | If an index key is equal to a search value, |
            |               |           | it matches.                                 |
            |               |           | Tuples are returned in ascending order by   |
            |               |           | index key. This is the default.             |
            +---------------+-----------+---------------------------------------------+
            | box.index.REQ | search    | Matching is the same as for                 |
            | or 'REQ'      | value     | ``box.index.EQ``.                           |
            |               |           | Tuples are returned in descending order by  |
            |               |           | index key.                                  |
            |               |           | Note re storage engine: phia does not     |
            |               |           | REQ.                                        |
            +---------------+-----------+---------------------------------------------+
            | box.index.GT  | search    | The comparison operator is '>' (greater     |
            | or 'GT'       | value     | than).                                      |
            |               |           | If an index key is greater than a search    |
            |               |           | value, it matches.                          |
            |               |           | Tuples are returned in ascending order by   |
            |               |           | index key.                                  |
            +---------------+-----------+---------------------------------------------+
            | box.index.GE  | search    | The comparison operator is '>=' (greater    |
            | or 'GE'       | value     | than or equal to).                          |
            |               |           | If an index key is greater than or equal to |
            |               |           | a search value, it matches.                 |
            |               |           | Tuples are returned in ascending order by   |
            |               |           | index key.                                  |
            +---------------+-----------+---------------------------------------------+
            | box.index.ALL | search    | Same as box.index.GE.                       |
            | or 'ALL'      | value     |                                             |
            |               |           |                                             |
            +---------------+-----------+---------------------------------------------+
            | box.index.LT  | search    | The comparison operator is '<' (less than). |
            | or 'LT'       | value     | If an index key is less than a search       |
            |               |           | value, it matches.                          |
            |               |           | Tuples are returned in descending order by  |
            |               |           | index key.                                  |
            +---------------+-----------+---------------------------------------------+
            | box.index.LE  | search    | The comparison operator is '<=' (less than  |
            | or 'LE'       | value     | or equal to).                               |
            |               |           | If an index key is less than or equal to a  |
            |               |           | search value, it matches.                   |
            |               |           | Tuples are returned in descending order by  |
            |               |           | index key.                                  |
            +---------------+-----------+---------------------------------------------+


            **Iterator types for HASH indexes**

            .. rst-class:: left-align-column-1
            .. rst-class:: left-align-column-2
            .. rst-class:: left-align-column-3

            +---------------+-----------+------------------------------------------------+
            | Type          | Arguments | Description                                    |
            +===============+===========+================================================+
            | box.index.ALL | none      | All index keys match.                          |
            |               |           | Tuples are returned in ascending order by      |
            |               |           | hash of index key, which will appear to be     |
            |               |           | random.                                        |
            +---------------+-----------+------------------------------------------------+
            | box.index.EQ  | search    | The comparison operator is '==' (equal to).    |
            | or 'EQ'       | value     | If an index key is equal to a search value,    |
            |               |           | it matches.                                    |
            |               |           | The number of returned tuples will be 0 or 1.  |
            |               |           | This is the default.                           |
            +---------------+-----------+------------------------------------------------+
            | box.index.GT  | search    | The comparison operator is '>' (greater than). |
            | or 'GT'       | value     | If a hash of an index key is greater than a    |
            |               |           | hash of a search value, it matches.            |
            |               |           | Tuples are returned in ascending order by hash |
            |               |           | of index key, which will appear to be random.  |
            |               |           | Provided that the space is not being updated,  |
            |               |           | one can retrieve all the tuples in a space,    |
            |               |           | N tuples at a time, by using                   |
            |               |           | {iterator='GT', limit=N}                       |
            |               |           | in each search, and using the last returned    |
            |               |           | value from the previous result as the start    |
            |               |           | search value for the next search.              |
            +---------------+-----------+------------------------------------------------+

            **Iterator types for BITSET indexes**

            .. rst-class:: left-align-column-1
            .. rst-class:: left-align-column-2
            .. rst-class:: left-align-column-3

            +----------------------------+-----------+----------------------------------------------+
            | Type                       | Arguments | Description                                  |
            +============================+===========+==============================================+
            | box.index.ALL              | none      | All index keys match.                        |
            | or 'ALL'                   |           | Tuples are returned in their order within    |
            |                            |           | the space.                                   |
            +----------------------------+-----------+----------------------------------------------+
            | box.index.EQ               | bitset    | If an index key is equal to a bitset value,  |
            | or 'EQ'                    | value     | it matches.                                  |
            |                            |           | Tuples are returned in their order within    |
            |                            |           | the space. This is the default.              |
            +----------------------------+-----------+----------------------------------------------+
            | box.index.BITS_ALL_SET     | bitset    | If all of the bits which are 1 in the bitset |
            |                            | value     | value are 1 in the index key, it matches.    |
            |                            |           | Tuples are returned in their order within    |
            |                            |           | the space.                                   |
            +----------------------------+-----------+----------------------------------------------+
            | box.index.BITS_ANY_SET     | bitset    | If any of the bits which are 1 in the bitset |
            |                            | value     | value are 1 in the index key, it matches.    |
            |                            |           | Tuples are returned in their order within    |
            |                            |           | the space.                                   |
            +----------------------------+-----------+----------------------------------------------+
            | box.index.BITS_ALL_NOT_SET | bitset    | If all of the bits which are 1 in the bitset |
            |                            | value     | value are 0 in the index key, it matches.    |
            |                            |           | Tuples are returned in their order within    |
            |                            |           | the space.                                   |
            +----------------------------+-----------+----------------------------------------------+

            .. _rtree-iterator:

            **Iterator types for RTREE indexes**

            .. rst-class:: left-align-column-1
            .. rst-class:: left-align-column-2
            .. rst-class:: left-align-column-3

            +--------------------+-----------+---------------------------------------------------------+
            | Type               | Arguments | Description                                             |
            +====================+===========+=========================================================+
            | box.index.ALL      | none      | All keys match.                                         |
            | or 'ALL'           |           | Tuples are returned in their order within the space.    |
            +--------------------+-----------+---------------------------------------------------------+
            | box.index.EQ       | search    | If all points of the rectangle-or-box defined by the    |
            | or 'EQ'            | value     | search value are the same as the rectangle-or-box       |
            |                    |           | defined by the index key, it matches.                   |
            |                    |           | Tuples are returned in their order within the space.    |
            |                    |           | "Rectangle-or-box" means "rectangle-or-box as           |
            |                    |           | explained in section RTREE_". This is the default.      |
            +--------------------+-----------+---------------------------------------------------------+
            | box.index.GT       | search    | If all points of the rectangle-or-box defined by the    |
            | or 'GT'            | value     | search value are within the rectangle-or-box            |
            |                    |           | defined by the index key, it matches.                   |
            |                    |           | Tuples are returned in their order within the space.    |
            +--------------------+-----------+---------------------------------------------------------+
            | box.index.GE       | search    | If all points of the rectangle-or-box defined by the    |
            | or 'GE'            | value     | search value are within, or at the side of, the         |
            |                    |           | rectangle-or-box defined by the index key, it matches.  |
            |                    |           | Tuples are returned in their order within the space.    |
            +--------------------+-----------+---------------------------------------------------------+
            | box.index.LT       | search    | If all points of the rectangle-or-box defined by the    |
            | or 'LT'            | value     | index key are within the rectangle-or-box               |
            |                    |           | defined by the search key, it matches.                  |
            |                    |           | Tuples are returned in their order within the space.    |
            +--------------------+-----------+---------------------------------------------------------+
            | box.index.LE       | search    | If all points of the rectangle-or-box defined by the    |
            | or 'LE'            | value     | index key are within, or at the side of, the            |
            |                    |           | rectangle-or-box defined by the search key, it matches. |
            |                    |           | Tuples are returned in their order within the space.    |
            +--------------------+-----------+---------------------------------------------------------+
            | box.index.OVERLAPS | search    | If some points of the rectangle-or-box defined by the   |
            | or 'OVERLAPS'      | values    | search value are within the rectangle-or-box            |
            |                    |           | defined by the index key, it matches.                   |
            |                    |           | Tuples are returned in their order within the space.    |
            +--------------------+-----------+---------------------------------------------------------+
            | box.index.NEIGHBOR | search    | If some points of the rectangle-or-box defined by the   |
            | or 'NEIGHBOR'      | value     | defined by the key are within, or at the side of,       |
            |                    |           | defined by the index key, it matches.                   |
            |                    |           | Tuples are returned in order: nearest neighbor first.   |
            +--------------------+-----------+---------------------------------------------------------+

        **First Example of index pairs():**

        Default 'TREE' Index and ``pairs()`` function:

        .. code-block:: tarantoolsession

            tarantool> s = box.schema.space.create('space17')
            ---
            ...
            tarantool> s:create_index('primary', {
                     >   parts = {1, 'STR', 2, 'STR'}
                     > })
            ---
            ...
            tarantool> s:insert{'C', 'C'}
            ---
            - ['C', 'C']
            ...
            tarantool> s:insert{'B', 'A'}
            ---
            - ['B', 'A']
            ...
            tarantool> s:insert{'C', '!'}
            ---
            - ['C', '!']
            ...
            tarantool> s:insert{'A', 'C'}
            ---
            - ['A', 'C']
            ...
            tarantool> function example()
                     >   for _, tuple in
                     >     s.index.primary:pairs(nil, {
                     >         iterator = box.index.ALL}) do
                     >       print(tuple)
                     >   end
                     > end
            ---
            ...
            tarantool> example()
            ['A', 'C']
            ['B', 'A']
            ['C', '!']
            ['C', 'C']
            ---
            ...
            tarantool> s:drop()
            ---
            ...

        **Second Example of index pairs():**

        This Lua code finds all the tuples whose primary key values begin with 'XY'.
        The assumptions include that there is a one-part primary-key
        TREE index on the first field, which must be a string. The iterator loop ensures
        that the search will return tuples where the first value
        is greater than or equal to 'XY'. The conditional statement
        within the loop ensures that the looping will stop when the
        first two letters are not 'XY'. |br|
        :codenormal:`for _,tuple in box.space.t.index.primary:pairs("XY",{iterator = "GE"}) do` |br|
        |nbsp| |nbsp| :codenormal:`if (string.sub(tuple[1], 1, 2) ~= "XY") then break end` |br|
        |nbsp| |nbsp| :codenormal:`print(tuple)` |br|
        |nbsp| |nbsp| :codenormal:`end` |br|

        **Third Example of index pairs():**

        This Lua code finds all the tuples whose primary key values are
        greater than or equal to 1000, and less than or equal to 1999
        (this type of request is sometimes called a "range search" or a "between search").
        The assumptions include that there is a one-part primary-key
        TREE index on the first field, which must be a number. The iterator loop ensures
        that the search will return tuples where the first value
        is greater than or equal to 1000. The conditional statement
        within the loop ensures that the looping will stop when the
        first value is greater than 1999. |br|
        :codenormal:`for _,tuple in box.space.t2.index.primary:pairs(1000,{iterator = "GE"}) do` |br|
        |nbsp| |nbsp| :codenormal:`if (tuple[1] > 1999) then break end` |br|
        |nbsp| |nbsp| :codenormal:`print(tuple)` |br|
        |nbsp| |nbsp| :codenormal:`end` |br|

    .. _index_object_select:

    .. method:: select(key, options)

        This is an alternative to :func:`box.space...select() <space_object.select>`
        which goes via a particular index and can make use of additional
        parameters that specify the iterator type, and the limit (that is, the
        maximum number of tuples to return) and the offset (that is, which
        tuple to start with in the list).

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`field-value(s)` = values to be matched against the index key;
        * :samp:`option(s)` any or all of
            * :samp:`iterator = {iterator-type}`,
            * :samp:`limit = {maximum-number-of-tuples}`,
            * :samp:`offset = {start-tuple-number}`.

        :return: the tuple or tuples that match the field values.
        :rtype:  tuple set as a Lua table

        **Example:**

        .. code-block:: tarantoolsession

            -- Create a space named tester.
            tarantool> sp = box.schema.space.create('tester')
            -- Create a unique index 'primary'
            -- which won't be needed for this example.
            tarantool> sp:create_index('primary', {parts = {1, 'NUM' }})
            -- Create a non-unique index 'secondary'
            -- with an index on the second field.
            tarantool> sp:create_index('secondary', {
                     >   type = 'tree',
                     >   unique = false,
                     >   parts = {2, 'STR'}
                     > })
            -- Insert three tuples, values in field[2]
            -- equal to 'X', 'Y', and 'Z'.
            tarantool> sp:insert{1, 'X', 'Row with field[2]=X'}
            tarantool> sp:insert{2, 'Y', 'Row with field[2]=Y'}
            tarantool> sp:insert{3, 'Z', 'Row with field[2]=Z'}
            -- Select all tuples where the secondary index
            -- keys are greater than 'X'.`
            tarantool> sp.index.secondary:select({'X'}, {
                     >   iterator = 'GT',
                     >   limit = 1000
                     > })

        The result will be a table of tuple and will look like this:

        .. code-block:: yaml

            ---
            - - [2, 'Y', 'Row with field[2]=Y']
              - [3, 'Z', 'Row with field[2]=Z']
            ...

        .. NOTE::

            :samp:`index.{index-name}` is optional. If it is omitted, then the assumed
            index is the first (primary-key) index. Therefore, for the example
            above, ``box.space.tester:select({1}, {iterator = 'GT'})`` would have
            returned the same two rows, via the 'primary' index.

        .. NOTE::

            :samp:`iterator = {iterator-type}` is optional. If it is omitted, then
            ``iterator = 'EQ'`` is assumed.

        .. NOTE::

            :samp:`{field-value} [, {field-value ...}]` is optional. If it is omitted,
            then every key in the index is considered to be a match, regardless of
            iterator type. Therefore, for the example above,
            ``box.space.tester:select{}`` will select every tuple in the tester
            space via the first (primary-key) index.

        .. NOTE::

            :samp:`box.space.{space-name}.index.{index-name}:select(...)[1]``. can be
            replaced by :samp:`box.space.{space-name}.index.{index-name}:get(...)`.
            That is, ``get`` can be used as a convenient shorthand to get the first
            tuple in the tuple set that would be returned by ``select``. However,
            if there is more than one tuple in the tuple set, then ``get`` returns
            an error.


        **Example with BITSET index:**

        The following script shows creation and search with a BITSET index.
        Notice: BITSET cannot be unique, so first a primary-key index is created.
        Notice: bit values are entered as hexadecimal literals for easier reading.

        .. code-block:: tarantoolsession

            tarantool> s = box.schema.space.create('space_with_bitset')
            tarantool> s:create_index('primary_index', {
                     >   parts = {1, 'STR'},
                     >   unique = true,
                     >   type = 'TREE'
                     > })
            tarantool> s:create_index('bitset_index', {
                     >   parts = {2, 'NUM'},
                     >   unique = false,
                     >   type = 'BITSET'
                     > })
            tarantool> s:insert{'Tuple with bit value = 01', 0x01}
            tarantool> s:insert{'Tuple with bit value = 10', 0x02}
            tarantool> s:insert{'Tuple with bit value = 11', 0x03}
            tarantool> s.index.bitset_index:select(0x02, {
                     >   iterator = box.index.EQ
                     > })
            ---
            - - ['Tuple with bit value = 10', 2]
            ...
            tarantool> s.index.bitset_index:select(0x02, {
                     >   iterator = box.index.BITS_ANY_SET
                     > })
            ---
            - - ['Tuple with bit value = 10', 2]
              - ['Tuple with bit value = 11', 3]
            ...
            tarantool> s.index.bitset_index:select(0x02, {
                     >   iterator = box.index.BITS_ALL_SET
                     > })
            ---
            - - ['Tuple with bit value = 10', 2]
              - ['Tuple with bit value = 11', 3]
            ...
            tarantool> s.index.bitset_index:select(0x02, {
                     >   iterator = box.index.BITS_ALL_NOT_SET
                     > })
            ---
            - - ['Tuple with bit value = 01', 1]
            ...

    .. _index_min:

    .. method:: min([key-value])

        Find the minimum value in the specified index.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`key-value`.

        :return: the tuple for the first key in the index. If optional
                ``key-value`` is supplied, returns the first key which
                is greater than or equal to ``key-value``.
        :rtype:  tuple

        Possible errors: index is not of type 'TREE'.

        Complexity Factors: Index size, Index type.

        Note re storage engine: phia does not support ``min()``.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester.index.primary:min()
            ---
            - ['Alpha!', 55, 'This is the first tuple!']
            ...

    .. _index_max:

    .. method:: max([key-value])

        Find the maximum value in the specified index.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`key-value`.

        :return: the tuple for the last key in the index. If optional ``key-value``
                is supplied, returns the last key which is less than or equal to
                ``key-value``.
        :rtype:  tuple

        Possible errors: index is not of type 'TREE'.

        Complexity Factors: Index size, Index type.

        Note re storage engine: phia does not support ``max()``.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester.index.primary:max()
            ---
            - ['Gamma!', 55, 'This is the third tuple!']
            ...

    .. _index_random:

    .. method:: random(random-value)

        Find a random value in the specified index. This method is useful when it's
        important to get insight into data distribution in an index without having
        to iterate over the entire data set.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`random-value` (type = number) = an arbitrary non-negative integer.

        :return: the tuple for the random key in the index.
        :rtype:  tuple

        Complexity Factors: Index size, Index type.

        Note re storage engine: phia does not support ``random()``.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester.index.secondary:random(1)
            ---
            - ['Beta!', 66, 'This is the second tuple!']
            ...

    .. _index_count:

    .. method:: count([key], [iterator])

        Iterate over an index, counting the number of
        tuples which match the key-value.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`{key-value}` (type = Lua table or scalar) =
          the value which must match the key(s) in the specified index. The type
          may be a list of field-values, or a tuple containing only the
          field-values;  :codeitalic:`iterator` = comparison method.

        :return: the number of matching index keys.
        :rtype:  number

        Note re storage engine: phia does not support :codenormal:`count(...)`.
        One possible workaround is to say :codenormal:`#select(...)`.


        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester.index.primary:count(999)
            ---
            - 0
            ...
            tarantool> box.space.tester.index.primary:count('Alpha!', { iterator = 'LE' })
            ---
            - 1
            ...

    .. method:: update(key, {{operator, field_no, value}, ...})

        Update a tuple.

        Same as :func:`box.space...update() <space_object.update>`,
        but key is searched in this index instead of primary key.
        This index ought to be unique.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`{key}` (type = Lua table or scalar) = key to be matched against
          the index key;
        * :samp:`{operator, field_no, value}` (type = Lua table) = update
          operations (see: :func:`box.space...update() <space_object.update>`).

        :return: the updated tuple.
        :rtype:  tuple

    .. method:: delete(key)

        Delete a tuple identified by a key.

        Same as :func:`box.space...delete() <space_object.delete>`, but key is
        searched in this index instead of in the primary-key index. This index
        ought to be unique.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`key` (type = Lua table or scalar) = key to be matched against
          the index key.

        :return: the deleted tuple.
        :rtype:  tuple

    .. _index_alter:

    .. method:: alter({options})

        Alter an index.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`{options}` = options list, same as the options list for
          :func:`create_index <space_object.create_index>`.

        :return: nil

        Possible errors: Index does not exist, or
        the first index cannot be changed to {unique = false}, or
        the alter function is only applicable for the memtx storage engine.

        Note re storage engine: phia does not support ``alter()``.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.space55.index.primary:alter({type = 'HASH'})
            ---
            ...

    .. method:: drop()

        Drop an index. Dropping a primary-key index has
        a side effect: all tuples are deleted.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`.

        :return: nil.

        Possible errors: Index does not exist, or a primary-key index cannot
        be dropped while a secondary-key index exists.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.space55.index.primary:drop()
            ---
            ...

    .. method:: rename(index-name)

        Rename an index.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`;
        * :samp:`{index-name}` (type = string) = new name for index.

        :return: nil

        Possible errors: index_object does not exist.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.space55.index.primary:rename('secondary')
            ---
            ...

        Complexity Factors: Index size, Index type, Number of tuples accessed.

    .. method:: bsize()

        Return the total number of bytes taken by the index.

        Parameters:

        * :samp:`{index_object}` = an :ref:`object reference <object-reference>`.

        :return: number of bytes
        :rtype: number

=================================================================
              Example showing use of the box functions
=================================================================

This example will work with the sandbox configuration described in the preface.
That is, there is a space named tester with a numeric primary key. The example
function will:

* select a tuple whose key value is 1000;
* return an error if the tuple already exists and already has 3 fields;
* Insert or replace the tuple with:
    * field[1] = 1000
    * field[2] = a uuid
    * field[3] = number of seconds since 1970-01-01;
* Get field[3] from what was replaced;
* Format the value from field[3] as yyyy-mm-dd hh:mm:ss.ffff;
* Return the formatted value.

The function uses Tarantool box functions
:func:`box.space...select <space_object.select>`,
:func:`box.space...replace <space_object.replace>`, :func:`fiber.time`,
:func:`uuid.str`. The function uses
Lua functions `os.date()`_ and `string.sub()`_.

.. _os.date(): http://www.lua.org/pil/22.1.html
.. _string.sub(): http://www.lua.org/pil/20.html

.. code-block:: lua

    function example()
      local a, b, c, table_of_selected_tuples, d
      local replaced_tuple, time_field
      local formatted_time_field
      local fiber = require('fiber')
      table_of_selected_tuples = box.space.tester:select{1000}
      if table_of_selected_tuples ~= nil then
        if table_of_selected_tuples[1] ~= nil then
          if #table_of_selected_tuples[1] == 3 then
            box.error({code=1, reason='This tuple already has 3 fields'})
          end
        end
      end
      replaced_tuple = box.space.tester:replace
        {1000,  require('uuid').str(), tostring(fiber.time())}
      time_field = tonumber(replaced_tuple[3])
      formatted_time_field = os.date("%Y-%m-%d %H:%M:%S", time_field)
      c = time_field % 1
      d = string.sub(c, 3, 6)
      formatted_time_field = formatted_time_field .. '.' .. d
      return formatted_time_field
    end

... And here is what happens when one invokes the function:

.. code-block:: tarantoolsession

    tarantool> box.space.tester:delete(1000)
    ---
    - [1000, '264ee2da03634f24972be76c43808254', '1391037015.6809']
    ...
    tarantool> example(1000)
    ---
    - 2014-01-29 16:11:51.1582
    ...
    tarantool> example(1000)
    ---
    - error: 'This tuple already has 3 fields'
    ...

=================================================================
              Example showing a user-defined iterator
=================================================================

Here is an example that shows how to build one's own iterator.
The paged_iter function is an "iterator function", which will only be
understood by programmers who have read the Lua
manual section
`Iterators and Closures <https://www.lua.org/pil/7.1.html>`_.
It does paginated retrievals, that is, it returns 10
tuples at a time from a table named "t", whose
primary key was defined with :codenormal:`create_index('primary',{parts={1,'STR'}})`. |br|
|nbsp| |nbsp| :codenormal:`function paged_iter(search_key, tuples_per_page)` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`local iterator_string = "GE"` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`return function ()` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`local page = box.space.t.index[0]:select(search_key,` |br|
|nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{iterator = iterator_string, limit=tuples_per_page})` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`if #page == 0 then return nil end` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`search_key = page[#page][1]` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`iterator_string = "GT"` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`return page` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`end` |br|
|nbsp| |nbsp| :codenormal:`end`

Programmers who use paged_iter do not need to know
why it works, they only need to know that, if they
call it within a loop, they will get 10 tuples
at a time until there are no more tuples. In this
example the tuples are merely printed, a page at a time.
But it should be simple to change the functionality,
for example by yielding after each retrieval, or
by breaking when the tuples fail to match some
additional criteria. |br|
|nbsp| |nbsp| :codenormal:`for page in paged_iter("X", 10) do` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`print("New Page. Number Of Tuples = " .. #page)` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`for i=1,#page,1 do print(page[i]) end` |br|
|nbsp| |nbsp| :codenormal:`end`

.. _RTREE:

=============================================================================
             Package `box.index` with index type = RTREE for spatial searches
=============================================================================

The :mod:`box.index` package may be used for spatial searches if the index type
is RTREE. There are operations for searching *rectangles* (geometric objects
with 4 corners and 4 sides) and *boxes* (geometric objects with more than 4
corners and more than 4 sides, sometimes called hyperrectangles). This manual
uses the term *rectangle-or-box* for the whole class of objects that includes both
rectangles and boxes. Only rectangles will be illustrated.

Rectangles are described according to their X-axis (horizontal axis) and Y-axis
(vertical axis) coordinates in a grid of arbitrary size. Here is a picture of
four rectangles on a grid with 11 horizontal points and 11 vertical points:

::

               X AXIS
               1   2   3   4   5   6   7   8   9   10  11
            1
            2  #-------+                                           <-Rectangle#1
    Y AXIS  3  |       |
            4  +-------#
            5          #-----------------------+                   <-Rectangle#2
            6          |                       |
            7          |   #---+               |                   <-Rectangle#3
            8          |   |   |               |
            9          |   +---#               |
            10         +-----------------------#
            11                                     #               <-Rectangle#4

The rectangles are defined according to this scheme: {X-axis coordinate of top
left, Y-axis coordinate of top left, X-axis coordinate of bottom right, Y-axis
coordinate of bottom right} -- or more succinctly: {x1,y1,x2,y2}. So in the
picture ... Rectangle#1 starts at position 1 on the X axis and position 2 on
the Y axis, and ends at position 3 on the X axis and position 4 on the Y axis,
so its coordinates are {1,2,3,4}. Rectangle#2's coordinates are {3,5,9,10}.
Rectangle#3's coordinates are {4,7,5,9}. And finally Rectangle#4's coordinates
are {10,11,10,11}. Rectangle#4 is actually a "point" since it has zero width
and zero height, so it could have been described with only two digits: {10,11}.

Some relationships between the rectangles are: "Rectangle#1's nearest neighbor
is Rectangle#2", and "Rectangle#3 is entirely inside Rectangle#2".

Now let us create a space and add an RTREE index.

.. code-block:: tarantoolsession

    tarantool> s = box.schema.space.create('rectangles')
    tarantool> i = s:create_index('primary', {
             >   type = 'HASH',
             >   parts = {1, 'NUM'}
             > })
    tarantool> r = s:create_index('rtree', {
             >   type = 'RTREE',
             >   unique = false,
             >   parts = {2, 'ARRAY'}
             > })

Field#1 doesn't matter, we just make it because we need a primary-key index.
(RTREE indexes cannot be unique and therefore cannot be primary-key indexes.)
The second field must be an "array", which means its values must represent
{x,y} points or {x1,y1,x2,y2} rectangles. Now let us populate the table by
inserting two tuples, containing the coordinates of Rectangle#2 and Rectangle#4.

.. code-block:: tarantoolsession

    tarantool> s:insert{1, {3, 5, 9, 10}}
    tarantool> s:insert{2, {10, 11}}

And now, following the description of `RTREE iterator types`_, we can search the
rectangles with these requests:

.. _RTREE iterator types: rtree-iterator_

.. code-block:: tarantoolsession

    tarantool> r:select({10, 11, 10, 11}, {iterator = 'EQ'})
    ---
    - - [2, [10, 11]]
    ...
    tarantool> r:select({4, 7, 5, 9}, {iterator = 'GT'})
    ---
    - - [1, [3, 5, 9, 10]]
    ...
    tarantool> r:select({1, 2, 3, 4}, {iterator = 'NEIGHBOR'})
    ---
    - - [1, [3, 5, 9, 10]]
      - [2, [10, 11]]
    ...

Request#1 returns 1 tuple because the point {10,11} is the same as the rectangle
{10,11,10,11} ("Rectangle#4" in the picture). Request#2 returns 1 tuple because
the rectangle {4,7,5,9}, which was "Rectangle#3" in the picture, is entirely
within{3,5,9,10} which was Rectangle#2. Request#3 returns 2 tuples, because the
NEIGHBOR iterator always returns all tuples, and the first returned tuple will
be {3,5,9,10} ("Rectangle#2" in the picture) because it is the closest neighbor
of {1,2,3,4} ("Rectangle#1" in the picture).

Now let us create a space and index for cuboids, which are rectangle-or-boxes that have
6 corners and 6 sides.

.. code-block:: tarantoolsession

    tarantool> s = box.schema.space.create('R')
    tarantool> i = s:create_index('primary', {parts = {1, 'NUM'}})
    tarantool> r = s:create_index('S', {
             >   type = 'RTREE',
             >   unique = false,
             >   dimension = 3,
             >   parts = {2, 'ARRAY'}
             > })

The additional field here is ``dimension=3``. The default dimension is 2, which is
why it didn't need to be specified for the examples of rectangle. The maximum dimension
is 20. Now for insertions and selections there will usually be 6 coordinates. For example:

.. code-block:: tarantoolsession

    tarantool> s:insert{1, {0, 3, 0, 3, 0, 3}}
    tarantool> r:select({1, 2, 1, 2, 1, 2}, {iterator = box.index.GT})

Now let us create a space and index for Manhattan-style spatial objects, which are rectangle-or-boxes that have
a different way to calculate neighbors.

.. code-block:: tarantoolsession

    tarantool> s = box.schema.space.create('R')
    tarantool> i = s:create_index('primary', {parts = {1, 'NUM'}})
    tarantool> r = s:create_index('S', {
             >   type = 'RTREE',
             >   unique = false,
             >   distance = 'manhattan',
             >   parts = {2, 'ARRAY'}
             > })

The additional field here is ``distance='manhattan'``.
The default distance calculator is 'euclid', which is the straightforward as-the-crow-flies method.
The optional distance calculator is 'manhattan', which can be a more appropriate method
if one is following the lines of a grid rather than traveling in a straight line.

.. code-block:: tarantoolsession

    tarantool> s:insert{1, {0, 3, 0, 3}}
    tarantool> r:select({1, 2, 1, 2}, {iterator = box.index.NEIGHBOR})


More examples of spatial searching are online in the file `R tree index quick
start and usage`_.

.. _R tree index quick start and usage: https://github.com/tarantool/tarantool/wiki/R-tree-index-quick-start-and-usage
