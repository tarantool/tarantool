-------------------------------------------------------------------------------
                            Package `box.index`
-------------------------------------------------------------------------------

The ``box.index`` package provides read-only access for index definitions and
index keys. Indexes are contained in ``box.space.space-name.index`` array within
each space object. They provide an API for ordered iteration over tuples. This
API is a direct binding to corresponding methods of index objects of type
``box.index`` in the storage engine.

.. module:: box.index

.. class:: index_object

    .. data:: unique

        true if the index is unique.

        :rtype: boolean

    .. data:: type

        Index type, 'TREE' or 'HASH' or 'BITSET' or 'RTREE'.

        :rtype: string

    .. data:: parts

        An array describing index key fields.

        :rtype: table

        .. code-block:: lua

            tarantool> box.space.tester.index.primary
            ---
            unique: true
            parts:
            0:
                type: NUM
                fieldno: 1
            id: 0
            space_id: 513
            name: primary
            type: TREE
            ...

    .. function:: pairs(bitset-value | field-value..., iterator-type)

        This method provides iteration support within an index. Parameter type is
        used to identify the semantics of iteration. Different index types support
        different iterators. The remaining arguments of the function are varying
        and depend on the iteration type. For example, a TREE index maintains a
        strict order of keys and can return all tuples in ascending or descending
        order, starting from the specified key. Other index types, however, do not
        support ordering.

        To understand consistency of tuples returned by an iterator, it's essential
        to know the principles of the Tarantool transaction processing subsystem. An
        iterator in Tarantool does not own a consistent read view. Instead, each
        procedure is granted exclusive access to all tuples and spaces until it
        encounters a "context switch": by causing a write to disk, network, or by an
        explicit call to :func:`fiber.yield`. When the execution flow returns
        to the yielded procedure, the data set could have changed significantly.
        Iteration, resumed after a yield point, does not preserve the read view,
        but continues with the new content of the database.

        :param type: iteration strategy as defined in tables below
        :return: this method returns an iterator closure, i.e. a function which can
                be used to get the next value on each invocation
        :rtype:  function, tuple

        :except: Selected iteration type is not supported in the subject index type,
                or supplied parameters do not match iteration type.

        Complexity Factors: Index size, Index type, Number of tuples accessed.

        .. container:: table

            **TREE iterator types**

            +---------------+-----------+---------------------------------------------+
            | Type          | Arguments | Description                                 |
            +===============+===========+=============================================+
            | box.index.ALL | none      | Iterate over all tuples in an index. Tuples |
            | or 'ALL'      |           | are returned in ascending order of the key. |
            +---------------+-----------+---------------------------------------------+
            | box.index.EQ  | field     | Equality iterator: iterate over all tuples  |
            | or 'EQ'       | values    | where field values = key values. Parts of a |
            |               |           | multi-part key need to be separated by      |
            |               |           | commas.                                     |
            |               |           |                                             |
            |               |           | If the number of field values is less than  |
            |               |           | the number of parts of a multi-part key,    |
            |               |           | the missing field values are considered to  |
            |               |           | be matching.                                |
            |               |           |                                             |
            |               |           | If there are multiple matches, then tuples  |
            |               |           | are returned in ascending order by key.     |
            +---------------+-----------+---------------------------------------------+
            | box.index.GT  | field     | Keys match if key values are greater than   |
            | or 'GT'       | values    | field values. If the number of field values |
            |               |           | is less than the number of parts of a       |
            |               |           | multi-part key, the missing field values    |
            |               |           | are considered to be matching. If the field |
            |               |           | value is ``nil``, iteration starts from the |
            |               |           | smallest key in the index. Tuples are       |
            |               |           | returned in ascending order by key.         |
            +---------------+-----------+---------------------------------------------+
            | box.index.REQ | field     | Reverse equality iterator. Matching is      |
            | or 'REQ'      | values    | determined in the same way as for           |
            |               |           | ``box.index.EQ``, but, if there are multiple|
            |               |           | matches, then tuples are returned in        |
            |               |           | descending order by key,                    |
            +---------------+-----------+---------------------------------------------+
            | box.index.GE  | field     | Keys match if key values are greater than   |
            | or 'GE'       | values    | or equal to field values. Tuples are        |
            |               |           | returned in ascending order by key. If the  |
            |               |           | field value is ``nil``, iteration starts    |
            |               |           | from the first key in the index.            |
            +---------------+-----------+---------------------------------------------+
            | box.index.LT  | field     | Keys match if key values are less than      |
            | or 'LT'       | values    | field values. Tuples are returned in        |
            |               |           | descending order by key. If the field value |
            |               |           | is ``nil``, iteration starts from the last  |
            |               |           | key in the index.                           |
            +---------------+-----------+---------------------------------------------+
            | box.index.LE  | field     | Keys match if key values are less than or   |
            | or 'LE'       | values    | equal to field values. Tuples are returned  |
            |               |           | in descending order by key. If the field    |
            |               |           | value is ``nil``, iteration starts from     |
            |               |           | the last key in the index.                  |
            +---------------+-----------+---------------------------------------------+

            **HASH iterator types**

            +---------------+-----------+---------------------------------------------+
            | Type          | Arguments | Description                                 |
            +===============+===========+=============================================+
            | box.index.ALL | none      | Iterate over all tuples in an index. Tuples |
            | or 'ALL'      |           | are returned in ascending order of the key. |
            +---------------+-----------+---------------------------------------------+
            | box.index.EQ  | field     | Equality iterator: iterate over all tuples  |
            | or 'EQ'       | values    | matching the key. Parts of a multi-part     |
            |               |           | key need to be separated by commas.         |
            |               |           |                                             |
            |               |           | A HASH index only supports exact match:     |
            |               |           | all parts of a key participating in the     |
            |               |           | index must be provided.                     |
            |               |           |                                             |
            |               |           | HASH indexes are always unique.             |
            +---------------+-----------+---------------------------------------------+
            | box.index.GT  | field     | Keys match if hashed key values are greater |
            | or 'GT'       | values    | than hashed field values. If the number of  |
            |               |           | field values is less than the number of     |
            |               |           | parts of a multi-part key, the result is an |
            |               |           | error. Tuples are returned in ascending     |
            |               |           | order by hashed key, so the order will      |
            |               |           | appear to be random. Provided that the      |
            |               |           | space is not being updated, the 'GT'        |
            |               |           | iterator can be used to retrieve all        |
            |               |           | tuples piece by piece, by supplying the     |
            |               |           | last returned value from the previous       |
            |               |           | range as the start field value for an       |
            |               |           | iterator over the next range.               |
            +---------------+-----------+---------------------------------------------+

            **BITSET iterator types**

            +----------------------------+-----------+---------------------------------------------+
            | Type                       | Arguments | Description                                 |
            +============================+===========+=============================================+
            | box.index.ALL              | none      | Iterate over all tuples in an index. Tuples |
            | or 'ALL'                   |           | are returned in ascending order of the      |
            |                            |           | key's bitset, and so will appear to be      |
            |                            |           | unordered.                                  |
            +----------------------------+-----------+---------------------------------------------+
            | box.index.EQ               | field     | Equality iterator: iterate over all tuples  |
            | or 'EQ'                    | values    | matching the field values. If there are     |
            |                            |           | multiple field values, they need to be      |
            |                            |           | separated by commas.                        |
            |                            |           |                                             |
            |                            |           | BITSET indexes are always unique.           |
            +----------------------------+-----------+---------------------------------------------+
            | box.index.BITS_ALL_SET     | field     | Keys match if all of the bits specified in  |
            |                            | values    | 'bit mask' are set.                         |
            +----------------------------+-----------+---------------------------------------------+
            | box.index.BITS_ANY_SET     | field     | Keys match if any of the bits specified in  |
            |                            | values    | 'bit mask' is set.                          |
            +----------------------------+-----------+---------------------------------------------+
            | box.index.BITS_ALL_NOT_SET | field     | Keys match if none of the bits specified in |
            |                            | values    | 'bit mask' is set.                          |
            +----------------------------+-----------+---------------------------------------------+

            .. _rtree-iterator:

            **RTREE iterator types**

            +--------------------+-----------+---------------------------------------------+
            | Type               | Arguments | Description                                 |
            +====================+===========+=============================================+
            | box.index.ALL      | none      | All keys match. Tuples are returned in      |
            | or 'ALL'           |           | ascending order of the primary key.         |
            +--------------------+-----------+---------------------------------------------+
            | box.index.EQ       | field     | Keys match if the rectangle defined by the  |
            | or 'EQ'            | values    | field values is the same as the rectangle   |
            |                    |           | defined by the key -- where "key" means     |
            |                    |           | "the key in the RTREE index" and            |
            |                    |           | "rectangle" means "rectangle as explained   |
            |                    |           | in section RTREE_.                          |
            +--------------------+-----------+---------------------------------------------+
            | box.index.GT       | field     | Keys match if all points of the rectangle   |
            | or 'GT'            | values    | defined by the field values are within the  |
            |                    |           | rectangle defined by the key.               |
            +--------------------+-----------+---------------------------------------------+
            | box.index.GE       | field     | Keys match if all points of the rectangle   |
            | or 'GE'            | values    | defined by the field values are within, or  |
            |                    |           | at the side of, the rectangle defined by    |
            |                    |           | the key.                                    |
            +--------------------+-----------+---------------------------------------------+
            | box.index.LT       | field     | Keys match if all points of the rectangle   |
            | or 'LT'            | values    | defined by the key are within the rectangle |
            |                    |           | defined by the field values.                |
            +--------------------+-----------+---------------------------------------------+
            | box.index.LE       | field     | Keys match if all points of the rectangle   |
            | or 'LE'            | values    | defined by the key are within, or at the    |
            |                    |           | side of, the rectangle defined by the field |
            |                    |           | values.                                     |
            +--------------------+-----------+---------------------------------------------+
            | box.index.OVERLAPS | field     | Keys match if all points of the rectangle   |
            | or 'OVERLAPS'      | values    | defined by the key are within, or at the    |
            |                    |           | side of, the rectangle defined by the field |
            |                    |           | values.                                     |
            +--------------------+-----------+---------------------------------------------+
            | box.index.NEIGHBOR | field     | Keys match if all points of the rectangle   |
            | or 'NEIGHBOR'      | values    | defined by the key are within, or at the    |
            |                    |           | side of, the rectangle defined by the field |
            |                    |           | values.                                     |
            +--------------------+-----------+---------------------------------------------+

        .. code-block:: lua

            tarantool> s = box.schema.space.create('space17')
            ---
            ...
            tarantool> s:create_index('primary', {parts = {1, 'STR', 2, 'STR'}})
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
            tarantool> console = require('console'); console.delimiter('!')
            ---
            ...
            tarantool> function example()
                     >   for _, tuple in
                     >   s.index.primary:pairs(nil, {iterator = box.index.ALL}) do
                     >     print(tuple)
                     >   end
                     > end!
            ---
            ...
            tarantool> console.delimiter('')!
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

    .. function:: select(key, options)

        This is is an alternative to box.space...select() which goes via a
        particular index and can make use of additional parameters that specify the
        iterator type, and the limit (that is, the maximum number of tuples to
        return) and the offset (that is, which tuple to start with in the list).

        :param lua-table or scalar key: values to be matched against the index key.
        :param lua-table options: table with any or all of iterator=iterator-type
                                    limit=maximum-number-of-tuples,
                                    offset=start-tuple-number.

        :return: the tuple or tuples that match the field values.
        :rtype:  tuple set as a Lua table

        .. code-block:: lua

            -- Create a space named tester.
            -- Create a unique index 'primary', which won't be needed for this example.
            -- Create a non-unique index 'secondary' with an index on the second field.
            -- Insert three tuples, values in field[2] equal to 'X', 'Y', and 'Z'.
            -- Select all tuples where the secondary index keys are greater than 'X'.
            box.schema.space.create('tester')
            box.space.tester:create_index('primary', {parts = {1, 'NUM' }})
            box.space.tester:create_index('secondary', {type = 'tree', unique = false, parts = {2, 'STR'}})
            box.space.tester:insert{1,'X','Row with field[2]=X'}
            box.space.tester:insert{2,'Y','Row with field[2]=Y'}
            box.space.tester:insert{3,'Z','Row with field[2]=Z'}
            box.space.tester.index.secondary:select({'X'}, {iterator = 'GT', limit = 1000})

        The result will be a table of tuple and will look like this:

        .. code-block:: yaml

            ---
            - - [2, 'Y', 'Row with field[2]=Y']
              - [3, 'Z', 'Row with field[2]=Z']
            ...

        .. NOTE::

            [.index.index-name] is optional. If it is omitted, then the assumed
            index is the first (primary-key) index. Therefore, for the example
            above, ``box.space.tester:select({1}, {iterator = 'GT'})`` would have
            returned the same two rows, via the 'primary' index.

        .. NOTE::

            ``iterator = iterator type`` is optional. If it is omitted, then
            ``iterator = 'EQ'`` is assumed.

        .. NOTE::

            ``field-value [, field-value ...]`` is optional. If it is omitted,
            then every key in the index is considered to be a match, regardless of
            iterator type. Therefore, for the example above,
            ``box.space.tester:select{}`` will select every tuple in the tester
            space via the first (primary-key) index.

        .. NOTE::

            ``box.space.space-name.index.index-name:select(...)[1]``. can be
            replaced by ``box.space.space-name.index.index-name:get(...)``.
            That is, get can be used as a convenient shorthand to get the first
            tuple in the tuple set that would be returned by select. However,
            if there is more than one tuple in the tuple set, then get returns
            an error.

    .. function:: min([key-value])

        Find the minimum value in the specified index.

        :return: the tuple for the first key in the index. If optional
                ``key-value`` is supplied, returns the first key which
                is greater than or equal to ``key-value``.
        :rtype:  tuple
        :except: index is not of type 'TREE'.

        Complexity Factors: Index size, Index type.

        .. code-block:: lua

            tarantool> box.space.tester.index.primary:min()
            ---
            - ['Alpha!', 55, 'This is the first tuple!']
            ...

    .. function:: max([key-value])

        Find the maximum value in the specified index.

        :return: the tuple for the last key in the index. If optional ``key-value``
                is supplied, returns the last key which is less than or equal to
                ``key-value``.
        :rtype:  tuple
        :except: index is not of type 'TREE'.

        Complexity Factors: Index size, Index type.

        .. code-block:: lua

            tarantool> box.space.tester.index.primary:max()
            ---
            - ['Gamma!', 55, 'This is the third tuple!']
            ...


    .. function:: random(random-value)

        Find a random value in the specified index. This method is useful when it's
        important to get insight into data distribution in an index without having
        to iterate over the entire data set.

        :param integer random-value: an arbitrary non-negative integer.
        :return: the tuple for the random key in the index.
        :rtype:  tuple

        Complexity Factors: Index size, Index type.

        .. code-block:: lua

            tarantool> box.space.tester.index.secondary:random(1)
            ---
            - ['Beta!', 66, 'This is the second tuple!']
            ...

    .. function:: count(key-value, options)

        Iterate over an index, counting the number of
        tuples which equal the provided search criteria.

        :param lua-value key-value: the value which must match the key(s) in the
                                    specified index. The type may be a list of
                                    field-values, or a tuple containing only
                                    the field-values.

        :return: the number of matching index keys. The ``index`` function
                is only applicable for the memtx storage engine.
        :rtype:  number

        .. code-block:: lua

            tarantool> box.space.tester.index.primary:count(999)
            ---
            - 0
            ...
            tarantool> box.space.tester.index.primary:count('Alpha!', { iterator = 'LE' })
            ---
            - 1
            ...

    .. function:: alter({options})

        Alter an index.

        :param table options: options list for create_index().
        :return: nil

        :except: If index-name doesn't exist.
        :except: The first index cannot be changed to {unique = false}.
        :except: The alter function is only applicable for the memtx storage engine.

        .. code-block:: lua

            tarantool> box.space.space55.index.primary:alter({type = 'HASH'})
            ---
            ...

    .. function:: drop()

        Drop an index. Dropping a primary-key index has
        a side effect: all tuples are deleted.

        :return: nil.
        :except: If index-name doesn't exist.

        .. code-block:: lua

            tarantool> box.space.space55.index.primary:drop()
            ---
            ...

    .. function:: rename(index-name)

        Rename an index.

        :param string index-name: new name for index.
        :return: nil
        :except: If index-name doesn't exist.

        .. code-block:: lua

            tarantool> box.space.space55.index.primary:rename('secondary')
            ---
            ...

        Complexity Factors: Index size, Index type, Number of tuples accessed.


===========================================================
                         Example
===========================================================

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

The function uses Tarantool box functions box.space...select,
box.space...replace, fiber.time, uuid.str(). The function uses
Lua functions os.date() and string.sub().

.. code-block:: lua

    console = require('console'); console.delimiter('!')
    function example()
      local a, b, c, table_of_selected_tuples, replaced_tuple, time_field
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
    end!
    console.delimiter('')!

... And here is what happens when one invokes the function:

.. code-block:: lua

    tarantool> box.space.tester:delete(1000)
    ---
    - 1000: {'264ee2da03634f24972be76c43808254', '1391037015.6809'}
    ...
    tarantool> example(1000)
    ---
    - 2014-01-29 16:11:51.1582
    ...
    tarantool> example(1000)
    ---
    - error: 'This tuple already has 3 fields'
    ...

.. _RTREE:

===========================================================
             Package `box.index` for RTREE
===========================================================

The :mod:`box.index` package may be used for spatial searches if the index type
is RTREE. There are operations for searching ``rectangles``. Rectangles are
described according to their X-axis (horizontal axis) and Y-axis (vertical axis)
coordinates in a grid of arbitrary size. Here is a picture of four rectangles on
a grid with 11 horizontal points and 11 vertical points:

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

.. code-block:: lua

    s = box.schema.create_space('rectangles')
    i = s:create_index('primary',{type='HASH',parts={1,'NUM'}})
    r = s:create_index('spatial',{type='RTREE',unique=false,parts={2,'ARRAY'}})

Field#1 doesn't matter, we just make it because we need a primary-key index.
(RTREE indexes cannot be unique and therefore cannot be primary-key indexes.)
The second field must be an "array", which means its values must represent
{x,y} points or {x1,y1,x2,y2} rectangles. Now let us populate the table by
inserting two tuples, containing the coordinates of Rectangle#2 and Rectangle#4.

.. code-block:: lua

    s:insert{1, {3,5,9,10}}
    s:insert{2, {10,11}}

And now, following the description of `RTREE iterator types`_, we can search the
rectangles with these requests:

.. _RTREE iterator types: rtree-iterator_

.. code-block:: lua

    r:select({10,11,10,11},{iterator='EQ'})   -- Request#1 (returns 1 tuple)
    r:select({4,7,5,9},{iterator='GT'})       -- Request#2 (returns 1 tuple)
    r:select({1,2,3,4},{iterator='NEIGHBOR'}) -- Request#3 (returns 2 tuples)

Request#1 returns 1 tuple because the point {10,11} is the same as the rectangle
{10,11,10,11} ("Rectangle#4" in the picture). Request#2 returns 1 tuple because
the rectangle {4,7,5,9}, which was "Rectangle#3" in the picture, is entirely
within{3,5,9,10} which was Rectangle#2. Request#3 returns 2 tuples, because the
NEIGHBOR iterator always returns all tuples, and the first returned tuple will
be {3,5,9,10} ("Rectangle#2" in the picture) because it is the closest neighbor
of {1,2,3,4} ("Rectangle#1" in the picture).

More examples of spatial searching are online in the file `R tree index quick
start and usage`_.

.. _R tree index quick start and usage: https://github.com/tarantool/tarantool/wiki/R-tree-index-quick-start-and-usage

