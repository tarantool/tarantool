-------------------------------------------------------------------------------
                        Appendix C. Lua tutorial
-------------------------------------------------------------------------------

=====================================================================
       Insert one million tuples with a Lua stored procedure
=====================================================================

This is an exercise assignment: “Insert one million tuples. Each tuple should
have a constantly-increasing numeric primary-key field and a random alphabetic
10-character string field.”

The purpose of the exercise is to show what Lua functions look like inside
Tarantool. It will be necessary to employ the Lua math library, the Lua string
library, the Tarantool box library, the Tarantool box.tuple library, loops, and
concatenations. It should be easy to follow even for a person who has not used
either Lua or Tarantool before. The only requirement is a knowledge of how other
programming languages work and a memory of the first two chapters of this manual.
But for better understanding, follow the comments and the links, which point to
the Lua manual or to elsewhere in this Tarantool manual. To further enhance
learning, type the statements in with the tarantool client while reading along.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                        Configure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We are going to use the "tarantool_sandbox" that was created in section
:ref:`first database`. So there is a single space, and a numeric primary key,
and a running tarantool server which also serves as a client.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                        Delimiter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In earlier versions of Tarantool, multi-line functions had to be
enclosed within "delimiters". They are no longer necessary, and
so they will not be used in this tutorial. However, they are still
supported. Users who wish to use delimiters, or users of
older versions of Tarantool, should check the syntax description for
:ref:`declaring a delimiter <setting delimiter>` before proceeding.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
           Create a function that returns a string
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We will start by making a function that returns a fixed string, “Hello world”.

.. code-block:: lua

    function string_function()
      return "hello world"
    end

The word "``function``" is a Lua keyword -- we're about to go into Lua. The
function name is string_function. The function has one executable statement,
``return "hello world"``. The string "hello world" is enclosed in double quotes
here, although Lua doesn't care -- one could use single quotes instead. The
word "``end``" means “this is the end of the Lua function declaration.”
To confirm that the function works, we can say

.. code-block:: lua

    string_function()

Sending ``function-name()`` means “invoke the Lua function.” The effect is
that the string which the function returns will end up on the screen.

For more about Lua strings see Lua manual `chapter 2.4 "Strings"`_ . For more
about functions see Lua manual `chapter 5 "Functions"`_.

.. _chapter 2.4 "Strings": http://www.lua.org/pil/2.4.html
.. _chapter 5 "Functions": http://www.lua.org/pil/5.html

The screen now looks like this:

.. code-block:: tarantoolsession

    tarantool> function string_funciton()
             >   return "hello world"
             > end
    ---
    ...
    tarantool> string_function()
    ---
    - hello world
    ...
    tarantool> 

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 Create a function that calls another function and sets a variable
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that ``string_function`` exists, we can invoke it from another
function.

.. code-block:: lua

    function main_function()
      local string_value
      string_value = string_function()
      return string_value
    end

We begin by declaring a variable "``string_value``". The word "``local``"
means that string_value appears only in ``main_function``. If we didn't use
"``local``" then ``string_value`` would be visible everywhere - even by other
users using other clients connected to this server! Sometimes that's a very
desirable feature for inter-client communication, but not this time.

Then we assign a value to ``string_value``, namely, the result of
``string_function()``. Soon we will invoke ``main_function()`` to check that it
got the value.

For more about Lua variables see Lua manual `chapter 4.2 "Local Variables and Blocks"`_ .

.. _chapter 4.2 "Local Variables and Blocks": http://www.lua.org/pil/4.2.html

The screen now looks like this:

.. code-block:: tarantoolsession

    tarantool> function main_function()
             >   local string_value
             >   string_value = string_function()
             >   return string_value
             > end
    ---
    ...
    tarantool> main_function()
    ---
    - hello world
    ...
    tarantool> 

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   Modify the function so it returns a one-letter random string
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that it's a bit clearer how to make a variable, we can change
``string_function()`` so that, instead of returning a fixed literal
'Hello world", it returns a random letter between 'A' and 'Z'.

.. code-block:: lua

    function string_function()
      local random_number
      local random_string
      random_number = math.random(65, 90)
      random_string = string.char(random_number)
      return random_string
    end

It is not necessary to destroy the old ``string_function()`` contents, they're
simply overwritten. The first assignment invokes a random-number function
in Lua's math library; the parameters mean “the number must be an integer
between 65 and 90.” The second assignment invokes an integer-to-character
function in Lua's string library; the parameter is the code point of the
character. Luckily the ASCII value of 'A' is 65 and the ASCII value of 'Z'
is 90 so the result will always be a letter between A and Z.

For more about Lua math-library functions see Lua users "`Math Library Tutorial`_".
For more about Lua string-library functions see Lua users "`String Library Tutorial`_" .

.. _Math Library Tutorial: http://lua-users.org/wiki/MathLibraryTutorial
.. _String Library Tutorial: http://lua-users.org/wiki/StringLibraryTutorial

Once again the ``string_function()`` can be invoked from main_function() which
can be invoked with ``main_function()``.

The screen now looks like this:

.. code-block:: tarantoolsession

    tarantool> function string_function()
             >   local random_number
             >   local random_string
             >   random_number = math.random(65, 90)
             >   random_string = string.char(random_number)
             >   return random_string
             > end
    ---
    ...
    tarantool> main_function()
    ---
    - C
    ...
    tarantool> 

... Well, actually it won't always look like this because ``math.random()``
produces random numbers. But for the illustration purposes it won't matter
what the random string values are.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   Modify the function so it returns a ten-letter random string
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that it's clear how to produce one-letter random strings, we can reach our
goal of producing a ten-letter string by concatenating ten one-letter strings,
in a loop.

.. code-block:: lua

    function string_function()
      local random_number
      local random_string
      random_string = ""
      for x = 1,10,1 do
        random_number = math.random(65, 90)
        random_string = random_string .. string.char(random_number)
      end
      return random_string
    end

The words "for x = 1,10,1" mean “start with x equals 1, loop until x equals 10,
increment x by 1 for each iteration.” The symbol ".." means "concatenate", that
is, add the string on the right of the ".." sign to the string on the left of
the ".." sign. Since we start by saying that random_string is "" (a blank
string), the end result is that random_string has 10 random letters. Once
again the ``string_function()`` can be invoked from ``main_function()`` which
can be invoked with ``main_function()``.

For more about Lua loops see Lua manual `chapter 4.3.4 "Numeric for"`_.

.. _chapter 4.3.4 "Numeric for": http://www.lua.org/pil/4.3.4.html

The screen now looks like this:

.. code-block:: tarantoolsession

    tarantool> function string_function()
             >   local random_number
             >   local random_string
             >   random_string = ""
             >   for x = 1,10,1 do
             >     random_number = math.random(65, 90)
             >     random_string = random_string .. string.char(random_number)
             >   end
             >   return random_string
             > end
    ---
    ...
    tarantool> main_function()
    ---
    - 'ZUDJBHKEFM'
    ...
    tarantool> 


~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
           Make a tuple out of a number and a string
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that it's clear how to make a 10-letter random string, it's possible to
make a tuple that contains a number and a 10-letter random string, by invoking
a function in Tarantool's library of Lua functions.

.. code-block:: lua

    function main_function()
      local string_value, t
      string_value = string_function()
      t = box.tuple.new({1, string_value})
      return t
    end

Once this is done, t will be the value of a new tuple which has two fields.
The first field is numeric: 1. The second field is a random string. Once again
the ``string_function()`` can be invoked from ``main_function()`` which can be
invoked with  ``main_function()``.

For more about Tarantool tuples see Tarantool manual section :mod:`Package box.tuple <box.tuple>`.

The screen now looks like this:

.. code-block:: tarantoolsession

    tarantool> function main_function()
             > local string_value, t
             > string_value = string_function()
             > t = box.tuple.new({1, string_value})
             > return t
             > end
    ---
    ...
    tarantool> main_function()
    ---
    - [1, 'PNPZPCOOKA']
    ...
    tarantool> 

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     Modify main_function to insert a tuple into the database
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that it's clear how to make a tuple that contains a number and a 10-letter
random string, the only trick remaining is putting that tuple into tester.
Remember that tester is the first space that was defined in the sandbox, so
it's like a database table.

.. code-block:: lua

    function main_function()
      local string_value, t
      string_value = string_function()
      t = box.tuple.new({1,string_value})
      box.space.tester:replace(t)
    end

The new line here is ``box.space.tester:replace(t)``. The name contains
'tester' because the insertion is going to be to tester. The second parameter
is the tuple value. To be perfectly correct we could have said
``box.space.tester:insert(t)`` here, rather than ``box.space.tester:replace(t)``,
but "replace" means “insert even if there is already a tuple whose primary-key
value is a duplicate”, and that makes it easier to re-run the exercise even if
the sandbox database isn't empty. Once this is done, tester will contain a tuple
with two fields. The first field will be 1. The second field will be a random
10-letter string. Once again the ``string_function(``) can be invoked from
``main_function()`` which can be invoked with ``main_function()``. But
``main_function()`` won't tell the whole story, because it does not return t, it
only puts t into the database. To confirm that something got inserted, we'll use
a SELECT request.

.. code-block:: lua

    main_function()
    box.space.tester:select{1}

For more about Tarantool insert and replace calls, see Tarantool manual section
:mod:`Package box.space <box.space>`.

The screen now looks like this:

.. code-block:: tarantoolsession

    tarantool> function main_function()
             >   local string_value, t
             >   string_value = string_function()
             >   t = box.tuple.new({1,string_value})
             >   box.space.tester:replace(t)
             > end
    ---
    ...
    tarantool> main_function()
    ---
    ...
    tarantool> box.space.tester:select{1}
    ---
    - - [1, 'EUJYVEECIL']
    ...
    tarantool> 

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 Modify main_function to insert a million tuples into the database
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that it's clear how to insert one tuple into the database, it's no big deal
to figure out how to scale up: instead of inserting with a literal value = 1
for the primary key, insert with a variable value = between 1 and 1 million, in
a loop. Since we already saw how to loop, that's a simple thing. The only extra
wrinkle that we add here is a timing function.

.. code-block:: lua

    function main_function()
      local string_value, t
      for i = 1,1000000,1 do
        string_value = string_function()
        t = box.tuple.new({i,string_value})
        box.space.tester:replace(t)
      end
    end
    start_time = os.clock()
    main_function()
    end_time = os.clock()
    'insert done in ' .. end_time - start_time .. ' seconds'

The Lua ``os.clock()`` function will return the number of seconds since the
start. Therefore, by getting start_time = number of seconds just before the
inserting, and then getting end_time = number of seconds just after the
inserting, we can calculate (end_time - start_time) = elapsed time in seconds.
We will display that value by putting it in a request without any assignments,
which causes Tarantool to send the value to the client, which prints it. (Lua's
answer to the C ``printf()`` function, which is ``print()``, will also work.)

For more on Lua ``os.clock()`` see Lua manual `chapter 22.1 "Date and Time"`_.
For more on Lua print() see Lua manual `chapter 5 "Functions"`_.

.. _chapter 22.1 "Date and Time": http://www.lua.org/pil/22.1.html
.. _chapter 5 "Functions": http://www.lua.org/pil/5.html

Since this is the grand finale, we will redo the final versions of all the
necessary requests: the request that
created ``string_function()``, the request that created ``main_function()``,
and the request that invokes ``main_function()``.

.. code-block:: lua

    function string_function()
      local random_number
      local random_string
      random_string = ""
      for x = 1,10,1 do
        random_number = math.random(65, 90)
        random_string = random_string .. string.char(random_number)
      end
      return random_string
    end

    function main_function()
      local string_value, t
      for i = 1,1000000,1 do
        string_value = string_function()
        t = box.tuple.new({i,string_value})
        box.space.tester:replace(t)
      end
    end
    start_time = os.clock()
    main_function()
    end_time = os.clock()
    'insert done in ' .. end_time - start_time .. ' seconds'

The screen now looks like this:

.. code-block:: tarantoolsession

    tarantool> function string_function()
             >   local random_number
             >   local random_string
             >   random_string = ""
             >   for x = 1,10,1 do
             >     random_number = math.random(65, 90)
             >     random_string = random_string .. string.char(random_number)
             >   end
             >   return random_string
             > end
    ---
    ...
    tarantool> function main_function()
             >   local string_value, t
             >   for i = 1,1000000,1 do
             >     string_value = string_function()
             >     t = box.tuple.new({i,string_value})
             >     box.space.tester:replace(t)
             >   end
             > end
    ---
    ...
    tarantool> start_time = os.clock()
    ---
    ...
    tarantool> main_function()
    ---
    ...
    tarantool> end_time = os.clock()
    ---
    ...
    tarantool> 'insert done in ' .. end_time - start_time .. ' seconds'
    ---
    - insert done in 37.62 seconds
    ...
    tarantool> 

What has been shown is that Lua functions are quite expressive (in fact one can
do more with Tarantool's Lua stored procedures than one can do with stored
procedures in some SQL DBMSs), and that it's straightforward to combine
Lua-library functions and Tarantool-library functions.

What has also been shown is that inserting a million tuples took 37 seconds. The
host computer was a Linux laptop. By changing :confval:`wal_mode <wal_mode>` to 'none' before
running the test, one can reduce the elapsed time to 4 seconds.

=====================================================================
                  Sum a JSON field for all tuples
=====================================================================

This is an exercise assignment: “Assume that inside every tuple there is a
string formatted as JSON. Inside that string there is a JSON numeric field.
For each tuple, find the numeric field's value and add it to a 'sum' variable.
At end, return the 'sum' variable.” The purpose of the exercise is to get
experience in one way to read and process tuples.

.. code-block:: lua
    :linenos:

    json = require('json')
    function sum_json_field(field_name)
      local v, t, sum, field_value, is_valid_json, lua_table
      sum = 0
      for v, t in box.space.tester:pairs() do
        is_valid_json, lua_table = pcall(json.decode, t[2])
        if is_valid_json then
          field_value = lua_table[field_name]
          if type(field_value) == "number" then sum = sum + field_value end
        end
      end
      return sum
    end

**LINE 3: WHY "LOCAL".** This line declares all the variables that will be used in
the function. Actually it's not necessary to declare all variables at the start,
and in a long function it would be better to declare variables just before using
them. In fact it's not even necessary to declare variables at all, but an
undeclared variable is "global". That's not desirable for any of the variables
that are declared in line 1, because all of them are for use only within the function.

**LINE 5: WHY "PAIRS()".** Our job is to go through all the rows and there are two
ways to do it: with :func:`box.space.space_object:pairs() <space_object.pairs>` or with
:func:`index.iterator <index_object.pairs>`.
We preferred ``pairs()`` because it is simpler.

**LINE 5: START THE MAIN LOOP.** Everything inside this ":code:`for`" loop will be
repeated as long as there is another index key. A tuple is fetched and can be
referenced with variable :code:`t`.

**LINE 6: WHY "PCALL".** If we simply said ``lua_table = json.decode(t[2]))``, then
the function would abort with an error if it encountered something wrong with the
JSON string - a missing colon, for example. By putting the function inside "``pcall``"
(`protected call`_), we're saying: we want to intercept that sort of error, so if
there's a problem just set ``is_valid_json = false`` and we will know what to do
about it later.

**LINE 6: MEANING.** The function is :func:`json.decode` which means decode a JSON
string, and the parameter is t[2] which is a reference to a JSON string. There's
a bit of hard coding here, we're assuming that the second field in the tuple is
where the JSON string was inserted. For example, we're assuming a tuple looks like

.. _protected call: http://www.lua.org/pil/8.4.html

.. code-block:: json

    field[1]: 444
    field[2]: '{"Hello": "world", "Quantity": 15}'

meaning that the tuple's first field, the primary key field, is a number while
the tuple's second field, the JSON string, is a string. Thus the entire statement
means "decode ``t[2]`` (the tuple's second field) as a JSON string; if there's an
error set ``is_valid_json = false``; if there's no error set ``is_valid_json = true`` and
set ``lua_table =`` a Lua table which has the decoded string".

**LINE 8.** At last we are ready to get the JSON field value from the Lua table that
came from the JSON string. The value in field_name, which is the parameter for the
whole function, must be a name of a JSON field. For example, inside the JSON string
``'{"Hello": "world", "Quantity": 15}'``, there are two JSON fields: "Hello" and
"Quantity". If the whole function is invoked with ``sum_json_field("Quantity")``,
then ``field_value = lua_table[field_name]`` is effectively the same as
``field_value = lua_table["Quantity"]`` or even ``field_value = lua_table.Quantity``.
Those are just three different ways of saying: for the Quantity field in the Lua table,
get the value and put it in variable :code:`field_value`.

**LINE 9: WHY "IF".** Suppose that the JSON string is well formed but the JSON field
is not a number, or is missing. In that case, the function would be aborted when
there was an attempt to add it to the sum. By first checking
``type(field_value) == "number"``, we avoid that abortion. Anyone who knows that
the database is in perfect shape can skip this kind of thing.

And the function is complete. Time to test it. Starting with an empty database,
defined the same way as the sandbox database that was introduced in
:ref:`first database`,

.. code-block:: lua

    -- if tester is left over from some previous test, destroy it
    box.space.tester:drop()
    box.schema.space.create('tester')
    box.space.tester:create_index('primary', {parts = {1, 'NUM'}})

then add some tuples where the first field is a number and the second
field is a string.

.. code-block:: lua

    box.space.tester:insert{444, '{"Item": "widget", "Quantity": 15}'}
    box.space.tester:insert{445, '{"Item": "widget", "Quantity": 7}'}
    box.space.tester:insert{446, '{"Item": "golf club", "Quantity": "sunshine"}'}
    box.space.tester:insert{447, '{"Item": "waffle iron", "Quantit": 3}'}

Since this is a test, there are deliberate errors. The "golf club" and the
"waffle iron" do not have numeric Quantity fields, so must be ignored.
Therefore the real sum of the Quantity field in the JSON strings should be:
15 + 7 = 22.

Invoke the function with ``sum_json_field("Quantity")``.

.. code-block:: tarantoolsession

    tarantool> sum_json_field("Quantity")
    ---
    - 22
    ...

It works. We'll just leave, as exercises for future improvement, the possibility
that the "hard coding" assumptions could be removed, that there might have to be
an overflow check if some field values are huge, and that the function should
contain a "yield" instruction if the count of tuples is huge.
