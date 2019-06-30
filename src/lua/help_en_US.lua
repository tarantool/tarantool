--- help (en_US) 2.x

return {
    help = [[
To get help, see the Tarantool manual at https://tarantool.io/en/doc/
To start the interactive Tarantool tutorial, type 'tutorial()' here.

Available backslash commands:

  \set language <language>   -- set language (lua or sql)
  \set output <format>       -- set output format (lua[,line|block] or yaml)
  \set delimiter <delimiter> -- set expression delimiter
  \help                      -- show this screen
  \quit                      -- quit interactive console
]];
    tutorial = {
    [[
Tutorial -- Screen #1 -- Hello, Moon
====================================

Welcome to the Tarantool tutorial.
It will introduce you to Tarantool’s Lua application server
and database server, which is what’s running what you’re seeing.
This is INTERACTIVE -- you’re expected to enter requests
based on the suggestions or examples in the screen’s text.

The first request is:

5.1, "Olá", "Lua"
------------------

This isn’t your grandfather’s "Hello World" ...
the decimal literal 5.1 and the two strings inside
single quotes ("Hello Moon" in Portuguese) will just
be repeated, without need for a print() statement.

Take that one-line request and enter it below after the
"tarantool>" prompt, then type Enter.
You’ll see the response:
---
- 5.1
- Olá
- Lua
...
Then you’ll get a chance to repeat -- perhaps entering
something else such as "Longer String",-1,-3,0.
When you’re ready to go to the next screen, enter <tutorial("next")>.
]];

[[
Tutorial -- Screen #2 -- Variables
==================================

Improve on "5.1, "Olá", "Lua""
by using variables rather than literals,
and put the strings inside braces, which means
they’re elements of a TABLE.
More in the Lua manual: http://www.lua.org/pil/2.html

You don’t need to declare variables in advance
because Lua figures out the data type from what
you assign to it. Assignment is done with the "=" operator.

If the data type of variable t is table, then the
elements can be referenced as t[1], t[2], and so on.
More in the Lua manual: http://www.lua.org/pil/2.5.html

Request #2 is:

n = 5.1
t = {"Olá", "Lua"}
n, t[1], t[2]
------------------

Take all the three lines and enter them below after the
"tarantool>" prompt, then type Enter.
Or try different values in a different order.
When you’re ready to go to the next screen, enter <tutorial("next")>.
Or, to go to the previous screen, enter <tutorial("prev")>.
]];

[[
Tutorial -- Screen #3 -- Loops
==============================

Add action by using loops rather than static displays.
There are several syntaxes for loops in Lua,
but we’ll just use one:
for variable-name = start-value, end-value, 1 do loop-body end
which is good enough if you want to assign a
start-value to a variable, do what’s in the loop body,
add 1 to the variable, and repeat until it equals end-value.
More in the Lua manual: http://www.lua.org/pil/4.3.4.html.

Request #3 is:

result_table = {}
n = 5.1
for i=1,2,1 do result_table[i] = n * i end
result_table
------------------------------------------

Take all four lines and enter them below after the
"tarantool>" prompt, then type Enter.
For adventure, change the loop to "for i=1,3,1"
(don’t worry, it won’t crash).
When you’re ready to go to the next screen, enter <tutorial("next")>.
]];

[[
Tutorial -- Screen #4 -- Operators
==================================

Among the many operators that Lua supports, you most often see:
For arithmetic: * (multiply), + (add), - (subtract), / (divide).
For strings: .. (concatenate)
More in the Lua manual: http://www.lua.org/pil/3.1.html

Request #4 is:

n = 5.1
t = {"Olá", "Lua"}
for i=1,2,1 do n = n * 2 t[1] = t[1] .. t[2] end
n,t[1],t[2]
------------------------------------------------

Before you type that in and see Tarantool display the result,
try to predict whether the display will be
(a) 20.4 OláLuaLua Lua
(b) 10.2 Olá Lua Lua Lua
(c) 5.1 Olá Lua

The answer will appear when you type in the request.
When you’re ready to go to the next screen, enter <tutorial("next")>.
]];

[[
Tutorial -- Screen #5 -- Conditions
===================================

A condition involves a comparison operator such as "==",
">", ">=", "<", "<=". Conditions are used in statements
of the form if ... then.
More in the Lua manual: http://www.lua.org/pil/4.3.1.html

Request #5 is:

x = 17
if x * 2 > 34 then result = x else result = "no" end
result
----------------------------------------------------

Before you type in those three lines and see Tarantool display
the result, try to predict whether the display will be
(a) 17
(b) 34
(c) no
The answer will appear when you type in the request.
When you’re ready to go to the next screen, enter <tutorial("next")>.
]];

[[
Tutorial -- Screen #6 -- Delimiters
===================================

This is just to prepare for later exercises
which will go over many lines. There is a
Tarantool instruction that means <don’t execute
every time I type Enter; wait until I type a
special string called the "delimiter".>
More in the Tarantool manual:
https://tarantool.io/en/doc/<version>/reference/reference_lua/console/#console-delimiter

Request #6 is:

console = require("console"); console.delimiter("!")
----------------------------------------------------

It’s not an exercise -- just do it.
Cancelling the delimiter could be done with
console.delimiter("")!
but you’ll see "!" in following exercises.

You'll need a custom delimiter only in the trial console at
https://tarantool.io/en/try-dev/.
Tarantool console in production is smarter.
It can tell when a multi-line request has not ended (for example,
if it sees that a function declaration does not have an end keyword,
as we'll be writing on the next screen).

When you’re ready to go to the next screen, enter <tutorial("next")!>.
Yes, <tutorial("next")!> now has to end with an exclamation mark too!
]];

[[
Tutorial -- Screen #7 -- Simple functions
=========================================

A function, or a stored procedure that returns a value,
is a named set of Lua requests whose simplest form is
function function_name () body end
More in the Lua manual: http://www.lua.org/pil/5.html

Request #7 is:

n = 0
function func ()
for i=1,100,1 do n = n + i end
return n
end!
func()!
------------------------------

This defines a function which sums all the numbers
between 1 and 100, and returns the final result.
The request "func()!" invokes the function.
]];

[[
Tutorial -- Screen #8 -- Improved functions
===========================================

Improve the simple function by avoiding globals.
The variable n could be passed as a parameter
and the variable i could be declared as local.
More in the Lua manual: http://www.lua.org/pil/4.2.html

Request #8 is:

function func (n)
local i
for i=1,100,1 do n = n + i end
return n
end!
func(0)!
------------------------------
]];

[[
Tutorial -- Screen #9 -- Comments
=================================

There are several ways to add comments, but
one will do: (minus sign) (minus sign) comment-text.
More in the Lua manual: http://www.lua.org/pil/1.3.html

Request #9 is:

-- func is a function which returns a sum.
-- n is a parameter. i is a local variable.
-- "!" is a delimiter (introduced in Screen #6)
-- func is a function (introduced in Screen #7)
-- n is a parameter (introduced in Screen #8)
-- "n = n + 1" is an operator usage (introduced in Screen #4)
-- "for ... do ... end" is a loop (introduced in Screen #3)
function func(n) -- n is a parameter
local i -- i is a local variable
for i=1,100,1 do n = n + i end
return n
end!
-- invoke the function
func(0)!
-------------------------------------------

Obviously it will work, so just type <tutorial("next")!> now.
]];

[[
Tutorial -- Screen #10 -- Modules
=================================

Many developers have gone to the trouble of making modules,
i.e. distributable packages of functions that have a general
utility. In the Lua world, modules are called "rocks".
More in the Luarocks list: http://luarocks.org/

Most modules have to be "required", with the syntax
variable_name = require("module-name")
which should look familiar because earlier you said
console = require("console")

At this point, if you just say the variable_name,
you’ll see a list of the module’s members and
functions. If then you use a "." operator as in
variable_name.function_name()
you’ll invoke a module’s function.
(At a different level you’ll have to use a ":"
operator, as you’ll see in later examples.)

Request #10 is:

fiber = require("fiber")!
fiber!
fiber.status()!
-------------------------

First you’ll see a list of functions, one of which is "status".
Then you’ll see the fiber's current status (the fiber is running now).
More on fibers on the next screen, so type <tutorial("next")!> now.
]];

[[
Tutorial -- Screen #11 -- The fiber module
==========================================

Make a function that will run like a daemon in the
background until you cancel it. For this you need
a fiber. Tarantool is a "cooperative multitasking"
application server, which means that multiple
tasks each get a slice, but they have to yield
occasionally so that other tasks get a chance.
That’s what a properly designed fiber will do.
More in the Tarantool manual:
https://tarantool.io/en/doc/<version>/reference/reference_lua/fiber/

Request #11 is:

fiber = require("fiber")!
gvar = 0!
function function_x()
for i=0,600,1 do
gvar = gvar + 1
fiber.sleep(1)
end
end!
fid = fiber.create(function_x)!
gvar!
-------------------------------

The fiber.sleep(1) function will go to sleep for
one second, which is one way of yielding.
So the "for i=0,600,1" loop will go on for about 600 seconds (10 minutes).
During waking moments, gvar will go up by 1 -- and
gvar is deliberately a global variable. So it’s
possible to monitor it: slowly type "gvar!" a few
times and notice how the value mysteriously increases.
]];

[[
Tutorial -- Screen #12 -- The socket module
===========================================

Connect to the Internet and send a message to Tarantool's web-site.

Request #12 is:

function socket_get ()
local socket, sock, result
socket = require("socket")
sock = socket.tcp_connect("tarantool.io", 80)
sock:send("GET / HTTP/1.0\r\nHost: tarantool.io\r\n\r\n")
result = sock:read(17)
sock:close()
return result
end!
socket_get()!
--------------------------------

Briefly these requests are opening a socket
and sending a "GET" request to tarantool.io’s server.
The response will be short, for example
"- "HTTP/1.1 302 OK\r\n""
but it shows you’ve gotten in touch with a distant server.
More in the Tarantool manual:
https://tarantool.io/en/doc/<version>/reference/reference_lua/socket/
]];

[[
Tutorial -- Screen #13 -- The box module
========================================

So far you’ve seen Tarantool in action as a
Lua application server. Henceforth you’ll see
it as a DBMS (database management system) server
-- with Lua stored procedures.

In serious situations you’d have to ask the
database administrator to create database objects
and grant read/write access to you, but here
you’re the "admin" user -- you have administrative
powers -- so you can start manipulating data immediately.
More in the Tarantool manual:
https://tarantool.io/en/doc/<version>/book/box/box_space/#box-space-replace

Request #13 is:

box.schema.space.create("tutor")!
box.space.tutor:create_index("primary",{})!
box.space.tutor:replace{1,"First tuple"}!
box.space.tutor:replace{2,"Second tuple"}!
box.space.tutor:replace{3,"Third tuple"}!
box.space.tutor:replace{4,"Fourth tuple"}!
box.space.tutor:replace{5,"Fifth tuple"}!
box.space.tutor!
-------------------------------------------

Please ignore all the requests except the last one.
You’ll see a description of a space named tutor.
To understand the description, you just have to know that:
** fields are numbered item-storage areas
(vaguely like columns in an SQL DBMS)
** tuples are collections of fields, as are Lua tables
(vaguely like rows in an SQL DBMS)
** spaces are where Tarantool stores sets of tuples
(vaguely like tables in an SQL DBMS)
** indexes are objects that make lookups of tuples faster
(vaguely like indexes in an SQL DBMS)
Much of the description doesn’t matter right now; it’s
enough if you see that module box gets a space which is
named tutor, and it has one index on the first field.
]];

[[
Tutorial -- Screen #14 -- box.select()
======================================

The most common data-manipulation function is box.select().

One of the syntaxes is:
box.space.tutor.index.primary:select({1}, {iterator = "GE"})
and it returns a set of tuples via the index of the tutor
space.
Now that you know that, and considering that you already
know how to make functions and loops in Lua, it’s simple
to figure out how to search and display the first five
tuples in the database.

Request #14 is:

-- This function will select and display 5 tuples in space=tutor
function database_display (space_name)
local i
local result = ""
t = box.space[space_name].index.primary:select({1}, {iterator = "GE"})
for i=1,5,1 do
result = result .. t[i][1] .. " " .. t[i][2] .. "\n"
end
return result
end!
database_display("tutor")!
--------------------------

So select() is returning a set of tuples into a Lua table
named t, and the loop is going to print each element of
the table. That is, when you call database_display()! you’ll
see a display of what’s in the tuples.
]];

[[
Tutorial -- Screen #15 -- box.replace()
=======================================

Pick any of the tuples that were displayed on the last screen.
Recall that the first field is the indexed field.
That’s all you need to replace the rest of the fields with
new values. The syntax of box.replace(), pared down, is:
box.space.tutor:replace{primary-key-field, other-fields}
More in the Tarantool manual:
https://tarantool.io/en/doc/<version>/book/box/box_space/#box-space-replace
Tarantool by default keeps database changes in memory,
but box.replace() will cause a write to a log, and log
information can later be consolidated with another box
function (box.snapshot).

Request #15 is:

box.space.tutor:replace{1, "My First Piece Of Data"}!
-----------------------------------------------------

If there is already a "tuple" (our equivalent of a record)
whose number is equal to 1, it will be replaced with your
new data. Otherwise it will be created for the first time.
The display will be the formal description of the new tuple.
]];

[[
Tutorial -- Screen #16 -- Create your own space
===============================================

You’ve now selected and replaced tuples from the
tutor space, and you could select and replace many
tuples because you know how to make variables and
functions and loops that do selecting or replacing.
But you’ve been confined to a space and an index
that Tarantool started with.
Suppose that you want to create your own.
More in the Tarantool manual:
https://tarantool.io/en/doc/<version>/book/getting_started/using_docker/#creating-a-database

Request #16 is:

box.schema.space.create("test", {engine="memtx"})!
--------------------------------------------------

The new space’s name will be "test" and the engine
will be "memtx" -- the engine which keeps all tuples
in memory, and writes changes to a log file to ensure
that data can’t be lost. Although "memtx" is the
default engine anyway, specifying it does no harm.
]];

[[
Tutorial -- Screen #17 -- Create your own index
===============================================

Having a space isn’t enough -- you must have at
least one index. Indexes make access faster.
Indexes can be declared to be "unique", which
is important because some combination of the
fields must be unique, for identification purposes.
More in the Tarantool manual:
https://tarantool.io/en/doc/<version>/book/box/data_model/#index

Request #17 is:

box.space.test:create_index("primary",{unique = true, parts = {1, "NUM"}})!
box.space.test:create_index("secondary",{parts = {2, "STR"}})!
--------------------------------------------------------------

This means the first index will be named primary,
will be unique, will be on the first field of each
tuple, and will be numeric. The second index will
be named secondary, doesn’t have to be unique, will
be on the second field of each tuple, and will be
in order by string value.
]];

[[
Tutorial -- Screen #18 -- Insert multiple tuples
================================================

In a loop, put some tuples in your new space.
Because of the index definitions, the first field
must be a number, the second field must be a string,
and the later fields can be anything.
Use a function in the Lua string library to make
values for the second field.
More in the Lua manual: http://www.lua.org/pil/20.html

Request #18 is:

for i=65,70,1 do
box.space.test:replace{i, string.char(i)}
end!
-----------------------------------------

Tip: to select the tuples later, use the function
that you created earlier: database_display("test")!
]];

[[
Tutorial -- Screen #19 -- Become another user
=============================================

Remember, you’re currently "admin" -- administrator.
Now switch to being "guest", a much less powerful user.

Request #19 is:

box.session.su("guest") -- switch user to "guest"!
box.space.test:replace{100,""} -- try to add a tuple!
-----------------------------------------------------

The result will be an error message telling you that
you don’t have the privilege to do that any more.
That’s good news. It shows that Tarantool prevents
unauthorized users from working with databases.
But you can say box.session.su("admin")! to become
a powerful user again, because for this tutorial
the "admin" user isn’t protected by a password.
]];

[[
Tutorial -- Screen #20 -- The bigger Tutorials
==============================================

You can continue to type in whatever Lua instructions,
module requires, and database-manipulations you want,
here on this screen. But to really get into Tarantool,
you should download it so that you can be your own
administrator and create your own permanent databases. The
Tarantool manual has three significant Lua tutorials:

Insert one million tuples with a Lua stored procedure,
Sum a JSON field for all tuples, and
Indexed pattern search.

See https://tarantool.io/en/doc/<version>/tutorials/lua_tutorials/

Request #20 is:

((Whatever you want. Enjoy!))

When you’re finished, don’t type <tutorial("next")!>, just wander off
and have a nice day.
]];
    }; --[[ tutorial ]]--
}
