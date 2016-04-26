.. _database-chapter:

*******************************************************************************
                              Database
*******************************************************************************

This chapter describes how Tarantool stores values
and what operations with data it supports.

===================
Document data model
===================

If you tried out the
:ref:`Starting Tarantool and making your first database <first database>`
exercise from the last chapter, then your database looks like this:

.. code-block:: none

   +--------------------------------------------+
   |                                            |
   | SPACE 'tester'                             |
   | +----------------------------------------+ |
   | |                                        | |
   | | TUPLE SET 'tester'                     | |
   | | +-----------------------------------+  | |
   | | | Tuple: [ 1 ]                      |  | |
   | | | Tuple: [ 2, 'Music' ]             |  | |
   | | | Tuple: [ 3, 'length', 93 ]        |  | |
   | | +-----------------------------------+  | |
   | |                                        | |
   | | INDEX 'primary'                        | |
   | | +-----------------------------------+  | |
   | | | Key: 1                            |  | |
   | | | Key: 2                            |  | |
   | | | Key: 3                            |  | |
   | | +-----------------------------------+  | |
   | |                                        | |
   | +----------------------------------------+ |
   +--------------------------------------------+

-----
Space
-----

A *space* -- 'tester' in the example -- is a container.

When Tarantool is being used to store data, there
is always at least one space. There can be many spaces.
Each space has a unique name specified by the user.
Each space has a unique numeric identifier which can
be specified by the user but usually is assigned
automatically by Tarantool. Spaces always
contain one tuple set and one or more indexes.

---------
Tuple Set
---------

A *tuple set* -- 'tester' in the example -- is a group of tuples.

There is always one tuple set in a space.
The identifier of a tuple set is the same
as the space name -- 'tester' in the example.

A tuple fills the same role as a “row” or a “record”,
and the components of a tuple (which we call “fields”)
fill the same role as a “row column” or “record field”,
except that: the fields of a tuple can be composite
structures, such as arrays or maps and don't need to have names.
That's why there was no need to pre-define the tuple set
when creating the space, and that's why each tuple can
have a different number of elements.
Tuples are stored as `MsgPack`_ arrays.

.. _MsgPack: https://en.wikipedia.org/wiki/MessagePack

Any given tuple may have any number of fields and
the fields may have a variety of types. The identifier
of a field is the field's number, base 1. For example
“1” can be used in some contexts to refer to the first field of a tuple.

When Tarantool returns a tuple value, it surrounds
strings with single quotes, separates fields with commas,
and encloses the tuple inside square brackets.
For example: ``[ 3, 'length', 93 ]``.

.. _box.index:

-----
Index
-----

An *index* -- 'primary' in the example -- is a group of key values and pointers.

In order for a tuple set to be useful, there must always
be at least one index in a space. There can be many.
As with spaces, the user can and should specify the index name,
and let Tarantool come up with a unique numeric identifier
(the "index id").
In our example there is one index and its name is “primary”.

An index may be *multi-part*, that is, the user can declare
that an index key value is taken from two or more fields
in the tuple, in any order. An index may be *unique*, that is,
the user can declare that it would be illegal to have the
same key value twice. An index may have *one of four types*:
HASH which is fastest and uses the least memory but must
be unique, TREE which allows partial-key searching and ordered
results, BITSET which can be good for searches that contain
'=' and multiple ANDed conditions, and RTREE for spatial coordinates.
The first index is called the “*primary key*” index and it must be unique;
all other indexes are called “secondary” indexes.

An index definition may include identifiers of tuple fields
and their expected types. The allowed types for indexed fields are NUM
(unsigned integer between 0 and
18,446,744,073,709,551,615), or STR (string, any sequence of octets), or ARRAY
(a series of numbers for use with :ref:`RTREE indexes <RTREE>`.
Take our example, which has the request:

.. code-block:: tarantoolsession

    tarantool> i = s:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})

The effect is that, for all tuples in tester,
field number 1 must exist and must contain an unsigned integer.

Space definitions and index definitions are stored permanently
in system spaces. It is possible to add, drop, or alter the
definitions at runtime, with some restrictions.
The syntax details for defining spaces and indexes
are in section :ref:`The box library <box-library>`.

----------
Data types
----------

Tarantool can work with numbers, strings, booleans, tables, and userdata.

+--------------+-------------+----------------------------+------------------------+
|  General type|Specific type|What Lua type()|would return|Example                 |
+==============+=============+============================+========================+
|   scalar     |  number     |   "`number`_"              |      12345             |
+--------------+-------------+----------------------------+------------------------+
|   scalar     |  string     |   "`string`_"              |       'A B C'          |
+--------------+-------------+----------------------------+------------------------+
|   scalar     |  boolean    |   "`boolean`_"             |       true             |
+--------------+-------------+----------------------------+------------------------+
|   scalar     |  nil        |   "`nil`_"                 |       nil              |
+--------------+-------------+----------------------------+------------------------+
|   compound   |  Lua table  |   "`table`_"               |       table: 0x410f8b10|
+--------------+-------------+----------------------------+------------------------+
|   compound   |  tuple      |   "`Userdata`_"            |       12345: {'A B C'} |
+--------------+-------------+----------------------------+------------------------+

.. _number: http://www.lua.org/pil/2.3.html
.. _string: http://www.lua.org/pil/2.4.html
.. _boolean: http://www.lua.org/pil/2.2.html
.. _nil: http://www.lua.org/pil/2.1.html
.. _table: http://www.lua.org/pil/2.5.html
.. _userdata: http://www.lua.org/pil/28.1.html

In Lua a *number* is double-precision floating-point,
but Tarantool allows both integer and floating-point values.
Tarantool will try to store a number as floating-point if
the value contains a decimal point or is very large (greater than 100 quadrillion = 1e14),
otherwise Tarantool will store it as an integer.
To ensure that even very large numbers will be treated as
integers, use the :func:`tonumber64 <tonumber64>`
function, or the LL (Long Long) suffix, or the ULL
(Unsigned Long Long) suffix. Here are examples of numbers
using regular notation, exponential notation, the ULL suffix,
and the tonumber64 function:
-55,  -2.7e+20, 100000000000000ULL, tonumber64('18446744073709551615').

For database storage Tarantool uses MsgPack rules.
Storage is variable-length, so the smallest number
requires only one byte but the largest number requires nine bytes.
When a field has a 'NUM' index, all values must be unsigned
integers between 0 and 18,446,744,073,709,551,615.

A *string* is a variable-length sequence of bytes,
usually represented with alphanumeric characters inside single quotes.

A *boolean* is either ``true`` or ``false``.

A *nil* type has only one possible value, also called *nil*, but often displayed
as *null*. Nils may be compared to values of any types with == (is-equal) or
~= (is-not-equal), but other operations will not work. Nils may not be used in
Lua tables; the workaround is to use :data:`yaml.NULL` or :data:`json.NULL` or
:data:`msgpack.NULL`.

A *tuple* is returned in YAML format like ``- [120, 'a', 'b', 'c']``.
A few functions may return tables with multiple tuples.
A scalar may be converted to a tuple with only one field.
A Lua table may contain all of a tuple's fields, but not nil.

For more tuple examples see :ref:`box.tuple <box-tuple>`.

----------
Operations
----------

The basic operations are: the five data-change operations
(insert, update, upsert, delete, replace), and the data-retrieval
operation (select). There are also minor operations like
“ping” which can only be used with the binary protocol.
Also, there are :func:`index iterator <index_object.pairs>` operations, which can only
be used with Lua code. (Index iterators are for traversing
indexes one key at a time, taking advantage of features
that are specific to an index type, for example evaluating
Boolean expressions when traversing BITSET indexes, or
going in descending order when traversing TREE indexes.)

Six examples of basic operations:

.. code-block:: tarantoolsession

    -- Add a new tuple to tuple set tester.
    -- The first field, field[1], will be 999 (type is NUM).
    -- The second field, field[2], will be 'Taranto' (type is STR).
    tarantool> box.space.tester:insert{999, 'Taranto'}

    -- Update the tuple, changing field field[2].
    -- The clause "{999}", which has the value to look up in
    -- the index of the tuple's primary-key field, is mandatory
    -- because update() requests must always have a clause that
    -- specifies the primary key, which in this case is field[1].
    -- The clause "{{'=', 2, 'Tarantino'}}" specifies that assignment
    -- will happen to field[2] with the new value.
    tarantool> box.space.tester:update({999}, {{'=', 2, 'Tarantino'}})

    -- Upsert the tuple, changing field field[2] again.
    -- The syntax of upsert is the same as the syntax of update,
    -- but the return value will be different.
    tarantool> box.space.tester:upsert({999}, {{'=', 2, 'Tarantism'}})

    -- Replace the tuple, adding a new field.
    -- This is also possible with the update() request but
    -- the update() request is usually more complicated.
    tarantool> box.space.tester:replace{999, 'Tarantella', 'Tarantula'}

    -- Retrieve the tuple.
    -- The clause "{999}" is still mandatory, although it does not have to
    -- mention the primary key.
    tarantool> box.space.tester:select{999}

    -- Delete the tuple.
    -- Once again the clause to identify the primary-key field is mandatory.
    tarantool> box.space.tester:delete{999}

How does Tarantool do a basic operation? Let's take this example:

.. code-block:: tarantoolsession

    tarantool> box.space.tester:update({3}, {{'=', 2, 'size'}, {'=', 3, 0}})

which, for those who know SQL, is equivalent to a statement like

.. code-block:: SQL

   UPDATE tester SET "field[2]" = 'size', "field[3]" = 0 WHERE "field[[1]" = 3

**STEP #1**: if this is happening on a remote client,
then the client parses the statement and changes
it to a binary-protocol instruction which has already
been checked, and which the server can understand without
needing to parse everything again. The client ships a packet to the server.

**STEP #2**: the server's “transaction processor” thread uses
the primary-key index on field[1] to find the location
of the tuple in memory. It determines that the tuple can
be updated (not much can go wrong when you're merely
changing an unindexed field value to something shorter).

**STEP #3**: the transaction processor thread sends a message
to the write-ahead logging (WAL) thread.

At this point a *yield* takes place. To know the significance
of that -- and it's quite significant -- you have to know a few
facts and a few new words.

**FACT #1**: there is only one transaction processor thread.
Some people are used to the idea that there can be multiple
threads operating on the database, with (say) thread #1
reading row #x while thread#2 writes row#y. With Tarantool
no such thing ever happens. Only the transaction processor
thread can access the database, and there is only one
transaction processor thread for each instance of the server.

**FACT #2**: the transaction processor thread can handle many *fibers*.
A fiber is a set of computer instructions that may contain
"yield" signals. The transaction processor thread will execute
all computer instructions until a yield, then switch to execute
the instructions of a different fiber. Thus (say) the thread reads
row#x for the sake of fiber#1, then writes row#y for the sake of fiber#2.

.. _yields_must_happen:

**FACT #3**: yields must happen, otherwise the transaction processor
thread would stick permanently on the same fiber.
There are implicit yields: every data-change operation
or network-access causes an implicit yield, and every
statement that goes through the tarantool client causes
an implicit yield. And there are explicit yields:
in a Lua function one can and should add “yield” statements
to prevent hogging. This is called *cooperative multitasking*.

Since all data-change operations end with an implicit yield
and an implicit commit, and since no data-change operation
can change more than one tuple, there is no need for any locking.
Consider, for example, a Lua function that does three Tarantool operations:

.. code-block:: lua

   s:select{999}             -- this does not yield and does not commit
   s:update({...},{{...}})   -- this yields and commits
   s:select{999}             -- this does not yield and does not commit

The combination “SELECT plus UPDATE” is an atomic transaction:
the function holds a consistent view of the database until the UPDATE ends.
For the combination “UPDATE plus SELECT” the view is not consistent,
because after the UPDATE the transaction processor thread can switch
to another fiber, and delete the tuple that was just updated.
Note re storage engine: phia handles yields differently, see
:ref:`differences between memtx and phia <phia_diff>`.
Note re multi-request transactions: there is a way to delay yields,
see :ref:`Atomic execution <atomic_execution>`.

Since locks don't exist, and disk writes only involve the write-ahead log,
transactions are usually fast. Also the Tarantool server may not be using
up all the threads of a powerful multi-core processor, so advanced users
may be able to start a second Tarantool server on the same processor without ill effects.

Additional examples of requests can be found in the `Tarantool regression test suite`_.
A complete grammar of supported data-manipulation functions will come later in this chapter.

.. _Tarantool regression test suite: https://github.com/tarantool/tarantool/tree/1.6/test/box

Since not all Tarantool operations can be expressed with the data-manipulation
functions, or with Lua, to gain complete access to data manipulation
functionality one must use a :ref:`Perl, PHP, Python or other programming language connector <box-connectors>`.
The client/server protocol is open and documented: an annotated BNF can be found
in the source tree, file `doc/box-protocol.html`_.

.. _doc/box-protocol.html: http://tarantool.org/doc/box-protocol.html

--------------
Saving To Disk
--------------

Tarantool maintains a set of write-ahead log (WAL) files.
There is a separate thread -- the WAL writer -- which catches all
requests that can change a database, such as box.schema.create or
box.space.insert. Ordinarily the WAL writer writes the request,
along with administrative fields and flags, to a WAL file immediately.
This ensures data persistence, because, even if an in-memory database
is lost when the power goes off, Tarantool recovers it automatically
when it starts up again, by reading the WAL files and redoing the requests
(this is called the "recovery process").
Users can change the timing of the WAL writer,
or turn it off, by setting :confval:`wal_mode <wal_mode>`.

Tarantool also maintains a set of snapshot files.
A snapshot file is an on-disk copy of the entire data set for a given moment.
Instead of reading every WAL file since the databases were created,
the recovery process can load the latest snapshot and then read only
the WAL files that were produced after the snapshot was made.
A snapshot can be made even if there is no WAL file.
Some snapshots are automatic, or users can make them at any time
with the :func:`box.snapshot() <box.snapshot()>` request.

Details about the WAL writer and the recovery process
are in the :ref:`Internals <box-internals>` section.

-----------------
Data manipulation
-----------------

The basic *data-manipulation* requests are: ``insert``, ``replace``, ``update``,
``upsert``, ``delete``, ``select``. All of them are part of the ``box`` library.
Most of them may return data. Usually both inputs and outputs are Lua tables.

The Lua syntax for data-manipulation functions can vary. Here are examples of
the variations with ``select`` requests; the same rules exist for the other
data-manipulation functions. Every one of the examples does the same thing:
select a tuple set from a space named tester where the primary-key field value
equals 1. For the examples there is an assumption that the numeric id of 'tester' is 512,
which happens to be the case in our sandbox example only.

.. _object-reference:

First, there are five *object reference variations*:

.. code-block:: tarantoolsession

    -- #1 package . sub-package . name
    tarantool> box.space.tester:select{1}
    -- #2 replace name with a literal in square brackets
    tarantool> box.space['tester']:select{1}
    -- #3 replace name with a numeric id in square brackets
    tarantool> box.space[512]:select{1}
    -- #4 use a variable instead of a literal for the name
    tarantool> variable = 'tester'
    tarantool> box.space[variable]:select{1}
    -- #5 use a variable for the entire object reference
    tarantool> s = box.space.tester
    tarantool> s:select{1}

Later examples in this manual will usually have the ":samp:`box.space.{tester}:`" form
(#1); however, this is a matter of user preference and all
the variations exist in the wild.

Later descriptions in this manual will use the syntax
":code:`space_object:`" for references to objects
which are spaces as in the above examples, and ":code:`index_object:`"
for references to objects which are indexes (for example
:samp:`box.space.{tester}.index.{primary}:`).

Then, there are six *parameter variations*:

.. code-block:: tarantoolsession

    -- #1
    tarantool> box.space.tester:select{1}
    -- #2
    tarantool> box.space.tester:select({1})
    -- #3
    tarantool> box.space.tester:select(1)
    -- #4
    tarantool> box.space.tester:select({1},{iterator='EQ'})
    -- #5
    tarantool> variable = 1
    tarantool> box.space.tester:select{variable}
    -- #6
    tarantool> variable = {1}
    tarantool> box.space.tester:select(variable)

The primary-key value is enclosed in braces, and if
it was a multi-part primary key then the value would be
multi-part, for example ``...select{1,2,3}``. The braces
can be enclosed inside parentheses — ``...select({...})`` — which
is optional unless it is necessary to pass something
besides the primary-key value, as in the fourth example.
Literal values such as 1 (a scalar value) or {1} (a Lua table
value) may be replaced by variable names, as in examples
``#5`` and ``#6``. Although there are special cases where braces
can be omitted, they are preferable because they signal
"Lua table". Examples and descriptions in this manual
have the "{1}" form; however, this too is a matter of
user preference and all the variations exist in the wild.

All the data-manipulation functions operate on tuple sets but,
since primary keys are unique, the number of tuples in the
tuple set is always 0 or 1. The only exception is ``box.space...select``,
which may accept either a primary-key value or a secondary-key value.

----------------
Index operations
----------------

Index operations are automatic: if a data-manipulation request
changes a tuple, then it also changes the index keys defined
for the tuple.
Therefore the user only needs to know how and why to define.

The simple index-creation operation which has been illustrated before is: |br|
:codenormal:`box.space.`:codeitalic:`space-name`:codenormal:`:create_index('index-name')` |br|
By default, this creates a unique "tree" index on the first field of all
tuples (often called "Field#1"), which is assumed to be numeric.

These variations exist: |br|
(1) A indexed field may be a string rather than a number. |br|
:codenormal:`box.space.`:codeitalic:`space-name`:codenormal:`:create_index('index-name',{parts = {1, 'STR'}})` |br|
For an ordinary index, the two possible data types are 'NUM'
= numeric = any positive integer, or 'STR' ='string' = any
series of bytes. Numbers are ordered
according to their point on the number line -- so 2345 is
greater than 500 -- while strings are ordered according to the
encoding of the first byte then the encoding of the second
byte then the encoding of the third byte and so on -- so
'2345' is less than '500'. |br|
(2) There may be more than one field. |br|
:codenormal:`box.space.`:codeitalic:`space-name`:codenormal:`:create_index('index-name', {parts = {3, 'NUM', 2, 'STR'}})` |br|
For an ordinary index, the maximum number of parts is 255.
The specification of each part consists of a field number and
a type. |br|
(3) The index does not have to be unique. |br|
:codenormal:`box.space.`:codeitalic:`space-name`:codenormal:`:create_index('index-name', {unique=false})` |br|
The first index of a tuple set must be unique, but other indexes ("secondary"
indexes) may be non-unique. |br|
(4) The index does not have to be a tree. |br|
:codenormal:`box.space.`:codeitalic:`space-name`:codenormal:`:create_index('index-name', {type='hash'})` |br|
The two ordinary index types are 'tree' which is the default,
and 'hash' which must be unique and which may be faster or
smaller. The third type is 'bitset' which is not unique and
which works best for combinations of binary values.
The fourth type is 'rtree' which is not unique and
which, instead of 'STR' or 'NUM' values, works with arrays.

The existence of indexes does not affect the syntax of data-change requests, but
does cause select requests to have more variety.

The simple select request which has been illustrated before is: |br|
:samp:`box.space.{space-name}:select(value)` |br|
By default, this looks for a single tuple
via the first index. Since the first index is always unique,
the maximum number of returned tuples will be: one.

These variations exist: |br|
(1) The search can use comparisons other than equality. |br|
:codenormal:`box.space.`:codeitalic:`space-name`:codenormal:`:select('value', {iterator='GT'})` |br|
The comparison operators are LT LE EQ REQ REQ GE GT for
"less than" "less than or equal" " "reversed
equal" "greater than or equal" "greater than" respectively.
Comparisons make sense if and only if the index type
is 'tree'.
This type of search may return more than one tuple;
if so, the tuples will be in descending order by key
when the comparison operator is LT or LE or REQ,
otherwise in ascending order. |br|
Note re storage engines: phia does not allow REQ. |br|
(2) The search can use a secondary index. |br|
:samp:`box.space.{space-name}.index.{index-name}:select('value')` |br|
For a primary-key search, it is optional to specify
an index name. For a secondary-key search, it is mandatory. |br|
(3) The search may be for some or all key parts. |br|
Suppose an index has two parts: {1,'NUM', 2, 'STR'}.
Suppose the space has three tuples: {1, 'A'},{1, 'B'},{2, ''}.
The search can be for all fields, using a table for the value: |br|
:codenormal:`box.space.`:codeitalic:`space-name`:codenormal:`:select({1, 'A'})` |br|
or the search can be for one field, using a table or a scalar: |br|
:samp:`box.space.{space-name}:select(1)` |br|
in the second case, the result will be two tuples:
{1, 'A'} and {1, 'B'}. It's even possible to specify zero
fields, causing all three tuples to be
returned.
Note re storage engines: phia requires that all fields, or none, be specified.

(1) BITSET example: |br|
:codenormal:`box.schema.space.create('bitset_example')` |br|
:codenormal:`box.space.bitset_example:create_index('primary')` |br|
:codenormal:`box.space.bitset_example:create_index('bitset',{unique=false,type='BITSET', parts={2,'NUM'}})` |br|
:codenormal:`box.space.bitset_example:insert{1,1}` |br|
:codenormal:`box.space.bitset_example:insert{2,4}` |br|
:codenormal:`box.space.bitset_example:insert{3,7}` |br|
:codenormal:`box.space.bitset_example:insert{4,3}` |br|
:codenormal:`box.space.bitset_example.index.bitset:select(2, {iterator='BITS_ANY_SET'})` |br|
The result will be: |br|
:codenormal:`---` |br|
:codenormal:`- - [3, 7]` |br|
|nbsp| |nbsp| :codenormal:`- [4, 3]` |br|
:codenormal:`...` |br|
because (7 AND 2) is not equal to 0, and (3 AND 2) is not equal to 0.
Searches on BITSET indexes can be for BITS_ANY_SET, BITS_ALL_SET,
BITS_ALL_NOT_SET, EQ, or ALL.

(2) RTREE example |br|
:codenormal:`box.schema.space.create('rtree_example')` |br|
:codenormal:`box.space.rtree_example:create_index('primary')` |br|
:codenormal:`box.space.rtree_example:create_index('rtree',{unique=false,type='RTREE', parts={2,'ARRAY'}})` |br|
:codenormal:`box.space.rtree_example:insert{1, {3, 5, 9, 10}}` |br|
:codenormal:`box.space.rtree_example:insert{2, {10, 11}}` |br|
:codenormal:`box.space.rtree_example.index.rtree:select({4, 7, 5, 9}, {iterator = 'GT'})` |br|
The result will be:
:codenormal:`---` |br|
:codenormal:`- - [1, [3, 5, 9, 10]]` |br|
:codenormal:`...` |br|
because a rectangle whose corners are at coordinates
4,7,5,9 is entirely within a rectangle whose corners
are at coordinates 3,5,9,10. Searches on RTREE indexes
can be for GT, GE, LT, LE, OVERLAPS, or NEIGHBOR.

.. _box-library:

---------------
The box library
---------------

As well as executing Lua chunks or defining their own functions, users can exploit
the Tarantool server's storage functionality with the ``Lua library``.

=====================================================================
                     Packages of the box library
=====================================================================

The contents of the ``box`` library can be inspected at runtime
with ``box``, with no arguments. The packages inside the box library are:
``box.schema``, ``box.tuple``, ``box.space``, ``box.index``, ``net.box``,
``box.cfg``, ``box.info``, ``box.slab``, ``box.stat``.
Every package contains one or more Lua functions. A few packages contain
members as well as functions. The functions allow data definition (create
alter drop), data manipulation (insert delete update upsert select replace), and
introspection (inspecting contents of spaces, accessing server configuration).


.. container:: table

    **Complexity Factors that may affect data
    manipulation functions in the box library**

    .. rst-class:: left-align-column-1
    .. rst-class:: left-align-column-2

    +-------------------+-----------------------------------------------------+
    | Index size        | The number of index keys is the same as the number  |
    |                   | of tuples in the data set. For a TREE index, if     |
    |                   | there are more keys then the lookup time will be    |
    |                   | greater, although of course the effect is not       |
    |                   | linear. For a HASH index, if there are more keys    |
    |                   | then there is more RAM use, but the number of       |
    |                   | low-level steps tends to remain constant.           |
    +-------------------+-----------------------------------------------------+
    | Index type        | Typically a HASH index is faster than a TREE index  |
    |                   | if the number of tuples in the tuple set is greater |
    |                   | than one.                                           |
    +-------------------+-----------------------------------------------------+
    | Number of indexes | Ordinarily only one index is accessed to retrieve   |
    | accessed          | one tuple. But to update the tuple, there must be N |
    |                   | accesses if the tuple set has N different indexes.  |
    +-------------------+-----------------------------------------------------+
    | Number of tuples  | A few requests, for example select, can retrieve    |
    | accessed          | multiple tuples. This factor is usually less        |
    |                   | important than the others.                          |
    +-------------------+-----------------------------------------------------+
    | WAL settings      | The important setting for the write-ahead log is    |
    |                   | :ref:`wal_mode <wal_mode>`. If the setting causes   |
    |                   | no writing or                                       |
    |                   | delayed writing, this factor is unimportant. If the |
    |                   | setting causes every data-change request to wait    |
    |                   | for writing to finish on a slow device, this factor |
    |                   | is more important than all the others.              |
    +-------------------+-----------------------------------------------------+

In the discussion of each data-manipulation function there will be a note about
which Complexity Factors might affect the function's resource usage.

.. _two-storage-engines:

=====================================================================
            The two storage engines: memtx and phia
=====================================================================

A storage engine is a set of very-low-level routines which actually store and
retrieve tuple values. Tarantool offers a choice of two storage engines: memtx
(the in-memory storage engine) and phia (the on-disk storage engine).
To specify that the engine should be phia, add a clause: ``engine = 'phia'``.
The manual concentrates on memtx because it is the default and has been around
longer. But phia is a working key-value engine and will especially appeal to
users who like to see data go directly to disk, so that recovery time might be
shorter and database size might be larger. For architectural explanations and
benchmarks, see Appendix E: :ref:`phia <phia>`.
On the other hand, phia lacks some functions and
options that are available with memtx. Where that is the case, the relevant
description will contain a note beginning with the words
"Note re storage engine: phia". The end of this chapter has coverage
for all :ref:`the differences between memtx and phia <phia_diff>`.

=====================================================================
                        Library Reference
=====================================================================

.. toctree::
    :maxdepth: 1

    box_schema
    box_space
    box_index
    box_session
    box_tuple
    box_introspection
    admin
    atomic
    authentication
    triggers
    limitations
    phia_diff


