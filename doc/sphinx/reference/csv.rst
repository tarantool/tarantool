-------------------------------------------------------------------------------
                                Package `csv`
-------------------------------------------------------------------------------

.. module:: csv

The csv package handles records formatted according to Comma-Separated-Values
(CSV) rules.

The default formatting rules are:

* Lua `escape sequences`_ such as \\n or \\10 are legal within strings but not
  within files,
* Commas designate end-of-field,
* Line feeds, or line feeds plus carriage returns, designate end-of-record,
* Leading or trailing spaces are ignored,
* Quote marks may enclose fields or parts of fields,
* When enclosed by quote marks, commas and line feeds and spaces are treated
  as ordinary characters, and a pair of quote marks "" is treated as a single
  quote mark.

.. _csv-options:

The possible options which can be passed to csv functions are:

* :samp:`delimiter = {string}` -- single-byte character to designate end-of-field, default = comma
* :samp:`quote_char = {string}` -- single-byte character to designate encloser of string, default = quote mark
* :samp:`chunk-size = {number}` -- number of characters to read at once (usually for file-IO efficiency), default = 4096
* :samp:`skip_head_lines = {number}` -- number of lines to skip at the start (usually for a header), default 0

.. function:: load(readable[, {options}])

    Get CSV-formatted input from ``readable`` and return a table as output.
    Usually ``readable`` is either a string or a file opened for reading.
    Usually :samp:`{options}` is not specified.

    :param object readable: a string, or any object which has a read() method,
                            formatted according to the CSV rules
    :param table options: see :ref:`above <csv-options>`
    :return: loaded_value
    :rtype:  table

    **Example:**

    Readable string has 3 fields, field#2 has comma and space so use quote marks:

    .. code-block:: tarantoolsession

        tarantool> csv = require('csv')
        ---
        ...
        tarantool> csv.load('a,"b,c ",d')
        ---
        - - - a
            - 'b,c '
            - d
        ...

    Readable string contains 2-byte character = Cyrillic Letter Palochka:
    (This displays a palochka if and only if character set = UTF-8.)

    .. code-block:: tarantoolsession

        tarantool> csv.load('a\\211\\128b')
        ---
        - - - a\211\128b
        ...

    Semicolon instead of comma for the delimiter:

    .. code-block:: tarantoolsession

        tarantool> csv.load('a,b;c,d', {delimiter = ';'})
        ---
        - - - a,b
            - c,d
        ...

    Readable file :file:`./file.csv` contains two CSV records.
    Explanation of fio is in section` :ref:`fio <fio-section>`.
    Source CSV file and example respectively:

    .. code-block:: tarantoolsession

        tarantool> -- input in file.csv is:
        tarantool> -- a,"b,c ",d
        tarantool> -- a\\211\\128b
        tarantool> fio = require('fio')
        ---
        ...
        tarantool> f = fio.open('./file.csv', {'O_RDONLY'})
        ---
        ...
        tarantool> csv.load(f, {chunk_size = 4096})
        ---
        - - - a
            - 'b,c '
            - d
          - - a\\211\\128b
        ...
        tarantool> f:close(nn)
        ---
        - true
        ...

.. function:: dump(csv-table[, options, writable])

    Get table input from ``csv-table`` and return a CSV-formatted string as output.
    Or, get table input from ``csv-table`` and put the output in ``writable``.
    Usually :samp:`{options}` is not specified.
    Usually ``writable``, if specified, is a file opened for writing.
    :func:`csv.dump()` is the reverse of :func:`csv.load()`.

    :param table csv-table: a table which can be formatted according to the CSV rules.
    :param table options: optional. see :ref:`above <csv-options>`
    :param object writable: any object which has a write() method
    :return: dumped_value
    :rtype:  string, which is written to ``writable`` if specified

    **Example:**

    CSV-table has 3 fields, field#2 has "," so result has quote marks

    .. code-block:: tarantoolsession

        tarantool> csv = require('csv')
        ---
        ...
        tarantool> csv.dump({'a','b,c ','d'})
        ---
        - 'a,"b,c ",d

        '
        ...

    Round Trip: from string to table and back to string

    .. code-block:: tarantoolsession

        tarantool> csv_table = csv.load('a,b,c')
        ---
        ...
        tarantool> csv.dump(csv_table)
        ---
        - 'a,b,c

        '
        ...


.. function:: iterate(input, {options})

    Form a Lua iterator function for going through CSV records
    one field at a time.

    :param table csv-table: a table which can be formatted according to the CSV rules.
    :param table options: see :ref:`above <csv-options>`
    :return: Lua iterator function
    :rtype:  iterator function

    **Example:**

    func:`csv.iterate()` is the low level of func:`csv.load()` and :func:`csv.dump()`.
    To illustrate that, here is a function which is the same as the :func:`csv.load()`
    function, as seen in `the Tarantool source code`_.

    .. code-block:: tarantoolsession

        tarantool> load = function(readable, opts)
                 >   opts = opts or {}
                 >   local result = {}
                 >   for i, tup in csv.iterate(readable, opts) do
                 >     result[i] = tup
                 >   end
                 >   return result
                 > end
        ---
        ...
        tarantool> load('a,b,c')
        ---
        - - - a
            - b
            - c
        ...

.. _escape sequences: http://www.lua.org/pil/2.4.html
.. _the Tarantool source code: https://github.com/tarantool/tarantool/blob/master/src/lua/csv.lua
