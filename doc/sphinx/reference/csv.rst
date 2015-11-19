-------------------------------------------------------------------------------
                                Package `csv`
-------------------------------------------------------------------------------


The csv package handles records formatted according to Comma-Separated-Values (CSV) rules.

The default formatting rules are:

* Lua `escape sequences`_ such as \\n or \\10 are legal within strings but not
  within files,
* Commas designate end-of-field,
* Line feeds, or line feeds plus carriage returns, designate end-of-record,
* Leading or trailing spaces are ignored,
* Quote marks may enclose fields or parts of fields,
* When enclosed by quote marks, commas and line feeds and spaces are treated as
  ordinary characters, and a pair of quote marks "" is treated as a single
  quote mark.

.. _csv-options:

The possible options which can be passed to csv functions are:

* :samp:`delimiter = {string}` -- single-byte character to designate
  end-of-field, default = comma
* :samp:`quote_char = {string}` -- single-byte character to designate
  encloser of string, default = quote mark
* :samp:`chunk-size = {number}` -- number of characters to read at once
  (usually for file-IO efficiency), default = 4096
* :samp:`skip_head_lines = {number}` -- number of lines to skip at the
  start (usually for a header), default 0.

.. module:: csv

.. function:: load(readable[, {options}])

    Get CSV-formatted input from :code:`readable` and return a table as output.
    Usually :code:`readable` is either a string or a file opened for reading.
    Usually :code:`{options}` is not specified.

    :param object readable: a string, or any object which has a read() method,  formatted according to the CSV rules
    :param table options: see :ref:`above <csv-options>`
    :return: loaded_value
    :rtype:  table

    **EXAMPLE**

    | :codenormal:`tarantool>` :codebold:`csv = require('csv')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`#readable string has 3 fields, field#2 has comma and space so use quote marks`
    | :codenormal:`tarantool>` :codebold:`csv.load('a,"b,c ",d')`
    | :codenormal:`---`
    | :codenormal:`- - - a`
    | :codenormal:`\     - 'b,c '`
    | :codenormal:`\     - d`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`#readable string contains 2-byte character = Cyrillic Letter Palochka`
    | :codenormal:`tarantool>` :codebold:`#(This displays a palochka if and only if character set = UTF-8.)`
    | :codenormal:`tarantool>` :codebold:`csv.load('a\\211\\128b')`
    | :codenormal:`---`
    | :codenormal:`- - - aӀb`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`#readable string contains a line feed so there are two records`
    | :codenormal:`tarantool>` :codebold:`csv.load('a,b,c\\10d,e,f')`
    | :codenormal:`---`
    | :codenormal:`- - - a`
    | :codenormal:`\     - b`
    | :codenormal:`\     - c`
    | :codenormal:`\   - - d`
    | :codenormal:`\     - e`
    | :codenormal:`\     - f`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`#semicolon instead of comma for the delimiter`
    | :codenormal:`tarantool>` :codebold:`tarantool> csv.load('a,b;c,d',{delimiter=';'})`
    | :codenormal:`---`
    | :codenormal:`- - - a,b`
    | :codenormal:`\     c,d`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`#readable file ./file.csv contains two CSV records`
    | :codenormal:`tarantool>` :codebold:`#    a,"b,c ",d`
    | :codenormal:`tarantool>` :codebold:`#    a\\211\\128b`
    | :codenormal:`tarantool>` :codebold:`#Explanation of fio is in section` :ref:`fio <fio-section>`
    | :codenormal:`tarantool>` :codebold:`fio = require('fio')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`f = fio.open('./file.csv', {'O_RDONLY'})`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`csv.load(f, {chunk_size = 4096})`
    | :codenormal:`---`
    | :codenormal:`- - - a`
    | :codenormal:`\     - 'b,c '`
    | :codenormal:`\     - d`
    | :codenormal:`\   - - a\\211\\128b`
    | :codenormal:`\   - -`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`f:close()`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`

.. function:: dump(csv-table[, options, writable])

    Get table input from :code:`csv-table` and return a CSV-formatted string as output.
    Or, get table input from :code:`csv-table` and put the output in :code:`writable`.
    Usually :code:`{options}` is not specified.
    Usually :code:`writable`, if specified, is a file opened for writing.
    :code:`csv.dump()` is the reverse of :code:`csv.load()`.

    :param table csv-table: a table which can be formatted according to the CSV rules.
    :param table options: optional. see :ref:`above <csv-options>`
    :param object writable: any object which has a write() method
    :return: dumped_value
    :rtype:  string, which is written to :code:`writable` if specified

    **EXAMPLE**

    | :codenormal:`tarantool>` :codebold:`#csv-table has 3 fields, field#2 has "," so result has quote marks`
    | :codenormal:`tarantool>` :codebold:`csv = require('csv')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`csv.dump({'a','b,c ','d'})`
    | :codenormal:`---`
    | :codenormal:`- 'a,"b,c ",d`
    |
    | :codenormal:`'`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`#Round Trip: from string to table and back to string`
    | :codenormal:`tarantool>` :codebold:`csv_table = csv.load('a,b,c')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`csv.dump(csv_table)`
    | :codenormal:`---`
    | :codenormal:`- 'a,b,c`
    |
    | :codenormal:`'`
    | :codenormal:`...`


.. function:: iterate(input, {options})

    Form a Lua iterator function for going through CSV records
    one field at a time.

    :param table csv-table: a table which can be formatted according to the CSV rules.
    :param table options: see :ref:`above <csv-options>`
    :return: Lua iterator function
    :rtype:  iterator function

    **EXAMPLE**

    | :codenormal:`csv.iterate()` is the low level of :codenormal:`csv.load()` and :codenormal:`csv.dump()`.
    | To illustrate that, here is a function which is the same as
    | the :codenormal:`csv.load()` function, as seen in `the Tarantool source code`_.
    | :codebold:`console=require('console'); console.delimiter('!')`
    | :codebold:`load = function(readable, opts)`
    | :codebold:`opts = opts or {}`
    | :codebold:`local result = {}`
    | :codebold:`for i, tup in csv.iterate(readable, opts) do`
    | :codebold:`result[i] = tup`
    | :codebold:`end`
    | :codebold:`return result`
    | :codebold:`end!`
    | :codebold:`console.delimiter('')!`
    | :codebold:`#Now, executing "load('a,b,c')" will return the same result as`
    | :codebold:`#"csv.load('a,b,c')", because it is the same code.`



.. _escape sequences: http://www.lua.org/pil/2.4.html
.. _the Tarantool source code: https://github.com/tarantool/tarantool/blob/master/src/lua/csv.lua

