.. _dbms-plugins:

-------------------------------------------------------------------------------
                        Appendix D. Modules
-------------------------------------------------------------------------------

A module is an optional library which enhances Tarantool functionality.

For examples of creating one's own module with Lua or C, see
`gist.github.com/rtsisyk/aa95cf9ed9bbb538ff80`_.

The discussion here in the user guide is about incorporating and using two
modules that have already been created: the "SQL DBMS rocks" for
MySQL and PostgreSQL.

===========================================================
                  SQL DBMS Modules
===========================================================

To call another DBMS from Tarantool, the essential requirements are: another
DBMS, and Tarantool. The module which connects Tarantool to another DBMS may
be called a "connector". Within the module there is a shared library which
may be called a "driver".

Tarantool supplies DBMS connector modules with the package manager for Lua,
LuaRocks. So the connector modules may be called "rocks".

The Tarantool rocks allow for connecting to an SQL server and executing SQL
statements the same way that a MySQL or PostgreSQL client does. The SQL
statements are visible as Lua methods. Thus Tarantool can serve as a "MySQL Lua
Connector" or "PostgreSQL Lua Connector", which would be useful even if that was
all Tarantool could do. But of course Tarantool is also a DBMS, so the plugin
also is useful for any operations, such as database copying and accelerating,
which work best when the application can work on both SQL and Tarantool inside
the same Lua routine.
The methods for connect/select/insert/etc. are similar to the ones in the
:ref:`net.box <package_net_box>` package.

From a user's point of view the MySQL and PostgreSQL rocks are
very similar, so the following sections -- "MySQL Example" and
"PostgreSQL Example" -- contain some redundancy.


===========================================================
                  MySQL Example
===========================================================

This example assumes that MySQL 5.5 or MySQL 5.6 has been installed.
Recent MariaDB versions should also work.
The package that matters most is the MySQL client
developer package, typically named something like libmysqlclient-dev.
The file that matters most from this package is
libmysqlclient.so or a similar name.
One can use :code:`find` or :code:`whereis` to see what
directories these files are installed in.

It will be necessary to install Tarantool's MySQL driver shared library,
load it, and use it to connect to a MySQL server.
After that, one can pass any MySQL statement to the server and
receive results, including multiple result sets.

HOW TO INSTALL:

Check the instructions for
:ref:`Downloading and installing a binary package <downloading-and-installing-a-binary-package>`
that apply for the environment where tarantool was installed.
In addition to installing :code:`tarantool`, install :code:`tarantool-dev`.
For example, on Ubuntu, add the line |br|
:codebold:`sudo apt-get install tarantool-dev`

Now, for the MySQL driver shared library, there are two ways to install:
with luarocks or with github.

With luarocks ... Begin by installing luarocks and making sure that
tarantool is among the upstream servers, as in the instructions on
`rocks.tarantool.org`_, the Tarantool luarocks page. Now execute this: |br|
:codenormal:`luarocks install mysql [MYSQL_LIBDIR=` :codeitalic:`name` :codenormal:`] [MYSQL_INCDIR=` :codeitalic:`name` :codenormal:`] [--local]` |br|
For example: |br|
:codebold:`luarocks install mysql MYSQL_LIBDIR=/usr/local/mysql/lib`

With github ... go the site `github.com/tarantool/mysql`_.
Follow the instructions there, saying

  | :codebold:`git clone https://github.com/tarantool/mysql.git`
  | :codebold:`cd mysql && cmake . -DCMAKE_BUILD_TYPE=RelWithDebugInfo`
  | :codebold:`make`
  | :codebold:`make install`

At this point it is a good idea to check that the installation
produced a file named :code:`driver.so`, and to check that this file
is on a directory that is searched by the :code:`require` request.

HOW TO CONNECT:

Begin by making a :code:`require` request for the mysql driver.
For example, :codebold:`mysql = require('mysql')`.
We will assume that the name is :code:`mysql` in further examples.

Now, say |br|
:codenormal:`connection_name = mysql.connect(` :codeitalic:`connection options` :codenormal:`)` |br|
The connection-options parameter is a table.
The possible options are: |br|
:codenormal:`host =` :codeitalic:`host-name` -- string, default value = 'localhost' |br|
:codenormal:`port =` :codeitalic:`port-number` -- number, default value = 3306 |br|
:codenormal:`user =` :codeitalic:`user-name` -- string, default value = operating-system user name |br|
:codenormal:`password =` :codeitalic:`password` or :codenormal:`pass =` :codeitalic:`password` -- string, default value = blank |br|
:codenormal:`db =` :codeitalic:`database-name` -- string, default value = blank |br|
The names are similar to the names that MySQL's mysql client uses, for details
see the MySQL manual at `dev.mysql.com/doc/refman/5.6/en/connecting.html`_.
To connect with a Unix socket rather than with TCP, specify :codenormal:`host = 'unix/'`
and :codenormal:`port =` :codeitalic:`socket-name`. |br|

Example, using a table literal enclosed in {braces}: |br|
:codebold:`conn = mysql.connect({host='127.0.0.1', port=3306, user='p', password='p', db='test'})` |br|

Example, using a table literal enclosed in {braces}: |br|
:codebold:`conn = mysql.connect({host='unix/',port='/var/run/mysqld/mysqld.sock'})`

Example, creating a function which sets each option in a separate line:
    | :codenormal:`# Connection function. Usage: conn = mysql_connect()`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`function mysql_connect ()`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`p = {}`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`p.host = 'widgets.com'`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`p.db = 'test'`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`conn = mysql.connect(p)`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`return conn`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`end!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console.delimiter('')!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`conn = mysql_connect()`
    | :codenormal:`---`
    | :codenormal:`...`

We will assume that the name is 'conn' in further examples.

HOW TO PING:

To ensure that a connection is working, the request is:

  | :codeitalic:`connection-name` :codenormal:`:` :codenormal:`ping()`

Example: |br|
  | :codenormal:`tarantool>` :codebold:`conn:ping()`
  | :codenormal:`---`
  | :codenormal:`- true`
  | :codenormal:`...`

HOW TO EXECUTE A STATEMENT: |br|

For all MySQL statements, the request is: |br|
:codeitalic:`connection-name` :codenormal:`:` :codenormal:`execute(` :codeitalic:`sql-statement` [, :codeitalic:`parameters` :codenormal:`])` |br|
where :code:`sql-statement` is a string, and the optional :code:`parameters`
are extra values that can be plugged in to replace any question marks ("?"s) in the SQL statement. |br|

For example: |br|
  | :codenormal:`tarantool>` :codebold:`conn:execute('select table_name from information_schema.tables')`
  | :codenormal:`---`
  | :codenormal:`- - table_name: ALL_PLUGINS`
  | |nbsp| |nbsp| :codenormal:`- table_name: APPLICABLE_ROLES`
  | |nbsp| |nbsp| :codenormal:`- table_name: CHARACTER_SETS`
  | |nbsp| :codenormal:`...`
  | :codenormal:`- 78`
  | :codenormal:`...`

HOW TO CLOSE:

To end a session that began with :code:`mysql.connect`, the request is: |br|
:codeitalic:`connection-name` :codenormal:`:` :codenormal:`close()` |br|
For example: |br|
:codebold:`conn:close()`

For further information, including examples of rarely-used requests,
see the README.md file at `github.com/tarantool/mysql`_.

LONG EXAMPLE:

The example was run on an Ubuntu 12.04 ("precise") machine where tarantool
had been installed in a /usr subdirectory, and a copy of MySQL had been installed on ~/mysql-5.5. The
mysqld server is already running on the local host 127.0.0.1.

    | :codebold:`export TMDIR=~/mysql-5.5`
    | :codenormal:`# Check that the include subdirectory exists by looking for .../include/mysql.h.`
    | :codenormal:`# (If this fails, there's a chance that it's in .../include/mysql/mysql.h instead.)`
    | :codenormal:`$` :codebold:`[ -f $TMDIR/include/mysql.h ] && echo "OK" || echo "Error"`
    | :codenormal:`OK`
    |
    | :codenormal:`# Check that the library subdirectory exists and has the necessary .so file.`
    | :codenormal:`$` :codebold:`[ -f $TMDIR/lib/libmysqlclient.so ] && echo "OK" || echo "Error"`
    | :codenormal:`OK`
    |
    | :codenormal:`# Check that the mysql client can connect using some factory defaults:`
    | :codenormal:`# port = 3306, user = 'root', user password = '', database = 'test'.`
    | :codenormal:`# These can be changed, provided one uses the changed values in`
    | :codenormal:`# all places.`
    | :codenormal:`$` :codebold:`$TMDIR/bin/mysql --port=3306 -h 127.0.0.1 --user=root --password= --database=test`
    | :codenormal:`Welcome to the MySQL monitor.  Commands end with ; or \\g.`
    | :codenormal:`Your MySQL connection id is 25`
    | :codenormal:`Server version: 5.5.35 MySQL Community Server (GPL)`
    | :codenormal:`...`
    | :codenormal:`Type 'help;' or '\\h' for help. Type '\\c' to clear the current input statement.`
    |
    | :codenormal:`# Insert a row in database test, and quit.`
    | :codenormal:`mysql>` :codebold:`CREATE TABLE IF NOT EXISTS test (s1 INT, s2 VARCHAR(50));`
    | :codenormal:`Query OK, 0 rows affected (0.13 sec)`
    | :codenormal:`mysql>` :codebold:`INSERT INTO test.test VALUES (1,'MySQL row');`
    | :codenormal:`Query OK, 1 row affected (0.02 sec)`
    | :codenormal:`mysql>` :codebold:`QUIT`
    | :codenormal:`Bye`
    |
    | :codenormal:`# Install luarocks`
    | :codenormal:`$` :codebold:`sudo apt-get -y install luarocks | grep "Setting up"`
    | :codenormal:`Setting up luarocks (2.0.8-2) ...`
    |
    | :codenormal:`# Set up the Tarantool rock list in ~/.luarocks,`
    | :codenormal:`# following instructions at rocks.tarantool.org`
    | :codenormal:`$` :codebold:`mkdir ~/.luarocks`
    | :codenormal:`$` :codebold:`echo "rocks_servers = {[[http://rocks.tarantool.org/]]}" >> ~/.luarocks/config.lua`
    |
    | :codenormal:`# Ensure that the next "install" will get files from Tarantool master repository`
    | :codenormal:`# The resultant display is normal for Ubuntu 12.04 precise`
    | :codenormal:`$` :codebold:`cat /etc/apt/sources.list.d/tarantool.list`
    | :codenormal:`deb http://tarantool.org/dist/master/ubuntu/ precise main`
    | :codenormal:`deb-src http://tarantool.org/dist/master/ubuntu/ precise main`
    |
    | :codenormal:`# Install tarantool-dev. The displayed line should show version = 1.6`
    | :codenormal:`$` :codebold:`sudo apt-get -y install tarantool-dev | grep "Setting up"`
    | :codenormal:`Setting up tarantool-dev (1.6.6.222.g48b98bb~precise-1) ...`
    | :codenormal:`$`
    |
    | :codenormal:`# Use luarocks to install locally, that is, relative to $HOME`
    | :codenormal:`$` :codebold:`luarocks install mysql MYSQL_LIBDIR=/usr/local/mysql/lib --local`
    | :codenormal:`Installing http://rocks.tarantool.org/mysql-scm-1.rockspec...`
    | :codenormal:`... (more information about building the Tarantool/MySQL driver appears here) ...`
    | :codenormal:`mysql scm-1 is now built and installed in ~/.luarocks/`
    |
    | :codenormal:`# Ensure driver.so now has been created in a place tarantool will look at`
    | :codenormal:`$` :codebold:`find ~/.luarocks -name "driver.so"`
    | :codenormal:`~/.luarocks/lib/lua/5.1/mysql/driver.so`
    |
    | :codenormal:`# Change directory to a directory which can be used for temporary tests.`
    | :codenormal:`# For this example we assume that the name of this directory is`
    | :codenormal:`# /home/pgulutzan/tarantool_sandbox. (Change "/home/pgulutzan" to whatever`
    | :codenormal:`# is the user's actual home directory for the machine that's used for this test.)`
    | :codebold:`cd /home/pgulutzan/tarantool_sandbox`
    |
    | :codenormal:`# Start the Tarantool server. Do not use a Lua initialization file.`
    |
    | :codenormal:`$` :codebold:`tarantool`
    | :codenormal:`tarantool: version 1.6.6-222-g48b98bb`
    | :codenormal:`type 'help' for interactive help`
    | :codenormal:`tarantool>` :codebold:`box.cfg{}`
    | :codenormal:`...`
    | :codenormal:`# Request the mysql package`
    | :codenormal:`tarantool>` :codebold:`mysql = require('mysql')`
    | :codenormal:`# ... Make sure that tarantool does not reply "error" for the call to "require()".`
    |
    | :codenormal:`# Create a Lua function that will connect to the MySQL server,`
    | :codenormal:`# (using some factory default values for the port and user and password),`
    | :codenormal:`# retrieve one row, and display the row.`
    | :codenormal:`# For explanations of the statement types used here, read the`
    | :codenormal:`# Lua tutorial earlier in the Tarantool user manual.`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`function mysql_select ()`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local conn = mysql.connect(`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`{host='127.0.0.1', port=3306, user='root', db='test'})`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local test = conn:execute('SELECT * FROM test WHERE s1 = 1')`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local row = ''`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`for i, card in pairs(test) do`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`row = row .. card.s2 .. ' '`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`end`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`conn:close()`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`return row`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`end!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console.delimiter('')!`
    | :codenormal:`tarantool>`
    |
    | :codenormal:`# Execute the Lua function.`
    | :codenormal:`tarantool>` :codebold:`mysql_select()`
    | :codenormal:`---`
    | :codenormal:`- 'MySQL row '`
    | :codenormal:`...`
    | :codenormal:`# Observe the result. It contains "MySQL row".`
    | :codenormal:`# So this is the row that was inserted into the MySQL database.`
    | :codenormal:`# And now it's been selected with the Tarantool client.`


===========================================================
                  PostgreSQL Example
===========================================================

This example assumes that PostgreSQL 8 or PostgreSQL 9 has been installed.
More recent versions should also work.
The package that matters most is the PostgreSQL 
developer package, typically named something like libpq-dev.
On Ubuntu this can be installed with |br|
:codebold:`sudo apt-get install libpq-dev` |br|
However, because not all platforms are alike, for this
example the assumption is that the user must check that the appropriate
PostgreSQL files are present and must explicitly state where they are when
building the Tarantool/PostgreSQL driver.
One can use :code:`find` or :code:`whereis` to see what
directories PostgreSQL files are installed in.

It will be necessary to install Tarantool's PostgreSQL driver shared library,
load it, and use it to connect to a PostgreSQL server.
After that, one can pass any PostgreSQL statement to the server and
receive results.

HOW TO INSTALL:

Check the instructions for
:ref:`Downloading and installing a binary package <downloading-and-installing-a-binary-package>`
that apply for the environment where tarantool was installed.
In addition to installing :code:`tarantool`, install :code:`tarantool-dev`.
For example, on Ubuntu, add the line |br|
:codebold:`sudo apt-get install tarantool-dev`

Now, for the PostgreSQL driver shared library, there are two ways to install:
with luarocks or with github.

With luarocks ... Begin by installing luarocks and making sure that
tarantool is among the upstream servers, as in the instructions on
`rocks.tarantool.org`_, the Tarantool luarocks page. Now execute this: |br|
:codenormal:`luarocks install pg [POSTGRESQL_LIBDIR=` :codeitalic:`name` :codenormal:`] [POSTGRESQL_INCDIR=` :codeitalic:`name` :codenormal:`] [--local]` |br|
For example: |br|
:codebold:`luarocks install pg POSTGRESQL_LIBDIR=/usr/local/postgresql/lib`

With github ... go the site `github.com/tarantool/pg`_.
Follow the instructions there, saying

  | :codebold:`git clone https://github.com/tarantool/pg.git`
  | :codebold:`cd pg && cmake . -DCMAKE_BUILD_TYPE=RelWithDebugInfo`
  | :codebold:`make`
  | :codebold:`make install`

At this point it is a good idea to check that the installation
produced a file named :code:`driver.so`, and to check that this file
is on a directory that is searched by the :code:`require` request.

HOW TO CONNECT:

Begin by making a :code:`require` request for the pg driver.
For example, :codebold:`pg = require('pg')`.
We will assume that the name is :code:`pg` in further examples.

Now, say |br|
:codenormal:`connection_name = pg.connect(` :codeitalic:`connection options` :codenormal:`)` |br|
The connection-options parameter is a table.
The possible options are: |br|
:codenormal:`host =` :codeitalic:`host-name` -- string |br|
:codenormal:`port =` :codeitalic:`port-number` -- number |br|
:codenormal:`user =` :codeitalic:`user-name` -- string |br|
:codenormal:`password =` :codeitalic:`password` or :codenormal:`pass =` :codeitalic:`password` -- string |br|
:codenormal:`db =` :codeitalic:`database-name` -- string |br|
The names are similar to the names that PostgreSQL itself uses.
|br|

Example, using a table literal enclosed in {braces}: |br|
:codebold:`conn = pg.connect({host='127.0.0.1', port=3306, user='p', password='p', db='test'})` |br|

Example, creating a function which sets each option in a separate line:
    | :codenormal:`# Connection function. Usage: conn = pg_connect()`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`function pg_connect ()`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`p = {}`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`p.host = 'widgets.com'`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`p.db = 'test'`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`conn = pg.connect(p)`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`return conn`
    | |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`end!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console.delimiter('')!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`conn = pg_connect()`
    | :codenormal:`---`
    | :codenormal:`...`

We will assume that the name is 'conn' in further examples.

HOW TO PING:

To ensure that a connection is working, the request is:

  | :codeitalic:`connection-name` :codenormal:`:` :codenormal:`ping()`

Example: |br|
  | :codenormal:`tarantool>` :codebold:`conn:ping()`
  | :codenormal:`---`
  | :codenormal:`- true`
  | :codenormal:`...`

HOW TO EXECUTE A STATEMENT: |br|

For all PostgreSQL statements, the request is: |br|
:codeitalic:`connection-name` :codenormal:`:` :codenormal:`execute(` :codeitalic:`sql-statement` [, :codeitalic:`parameters` :codenormal:`])` |br|
where :code:`sql-statement` is a string, and the optional :code:`parameters`
are extra values that can be plugged in to replace any question marks ("?"s) in the SQL statement. |br|

For example: |br|
  | :codenormal:`tarantool>` :codebold:`conn:execute('select tablename from pg_tables')`
  | :codenormal:`---`
  | :codenormal:`- - table_name: ALL_PLUGINS`
  | |nbsp| |nbsp| :codenormal:`- tablename: pg_statistic`
  | |nbsp| |nbsp| :codenormal:`- tablename: pg_type`
  | |nbsp| :codenormal:`...`
  | :codenormal:`...`

HOW TO CLOSE:

To end a session that began with :code:`pg.connect`, the request is: |br|
:codeitalic:`connection-name` :codenormal:`:` :codenormal:`close()` |br|
For example: |br|
:codebold:`conn:close()`

For further information, including examples of rarely-used requests,
see the README.md file at `github.com/tarantool/pg`_.

LONG EXAMPLE:

The example was run on an Ubuntu 12.04 ("precise") machine where tarantool
had been installed in a /usr subdirectory, and a copy of PostgreSQL had been installed on /usr. The
PostgreSQL server is already running on the local host 127.0.0.1.

    | :codenormal:`# Check that the include subdirectory exists`
    | :codenormal:`# by looking for /usr/include/postgresql/libpq-fe-h.`
    | :codenormal:`$` :codebold:`[ -f /usr/include/postgresql/libpq-fe.h ] && echo "OK" || echo "Error"`
    | :codenormal:`OK`
    |
    | :codenormal:`# Check that the library subdirectory exists and has the necessary .so file.`
    | :codenormal:`$` :codebold:`[ -f /usr/lib/x86_64-linux-gnu/libpq.so ] && echo "OK" || echo "Error"`
    | :codenormal:`OK`
    |
    | :codenormal:`# Check that the psql client can connect using some factory defaults:`
    | :codenormal:`# port = 5432, user = 'postgres', user password = 'postgres', database = 'postgres'.`
    | :codenormal:`# These can be changed, provided one changes them in all places.`
    | :codenormal:`# Insert a row in database postgres, and quit.`
    | :codenormal:`$` :codebold:`psql -h 127.0.0.1 -p 5432 -U postgres -d postgres`
    | :codenormal:`Password for user postgres:`
    | :codenormal:`psql (9.3.0, server 9.3.2)`
    | :codenormal:`SSL connection (cipher: DHE-RSA-AES256-SHA, bits: 256)`
    | :codenormal:`Type "help" for help.`
    |
    | :codenormal:`postgres=#` :codebold:`CREATE TABLE test (s1 INT, s2 VARCHAR(50));`
    | :codenormal:`CREATE TABLE`
    | :codenormal:`postgres=#` :codebold:`INSERT INTO test VALUES (1,'PostgreSQL row');`
    | :codenormal:`INSERT 0 1`
    | :codenormal:`postgres=#` :codebold:`\\q`
    | :codenormal:`$`
    |
    | :codenormal:`# Install luarocks`
    | :codenormal:`$` :codebold:`sudo apt-get -y install luarocks | grep "Setting up"`
    | :codenormal:`Setting up luarocks (2.0.8-2) ...`
    |
    | :codenormal:`# Set up the Tarantool rock list in ~/.luarocks,`
    | :codenormal:`# following instructions at rocks.tarantool.org`
    | :codenormal:`$` :codebold:`mkdir ~/.luarocks`
    | :codenormal:`$` :codebold:`echo "rocks_servers = {[[http://rocks.tarantool.org/]]}" >> ~/.luarocks/config.lua`
    |
    | :codenormal:`# Ensure that the next "install" will get files from Tarantool master repository`
    | :codenormal:`# The resultant display is normal for Ubuntu 12.04 precise`
    | :codenormal:`$` :codebold:`cat /etc/apt/sources.list.d/tarantool.list`
    | :codenormal:`deb http://tarantool.org/dist/master/ubuntu/ precise main`
    | :codenormal:`deb-src http://tarantool.org/dist/master/ubuntu/ precise main`
    |
    | :codenormal:`# Install tarantool-dev. The displayed line should show version = 1.6`
    | :codenormal:`$` :codebold:`sudo apt-get -y install tarantool-dev | grep "Setting up"`
    | :codenormal:`Setting up tarantool-dev (1.6.6.222.g48b98bb~precise-1) ...`
    | :codenormal:`$`
    |
    | :codenormal:`# Use luarocks to install locally, that is, relative to $HOME`
    | :codenormal:`$` :codebold:`luarocks install pg POSTGRESQL_LIBDIR=/usr/lib/x86_64-linux-gnu --local`
    | :codenormal:`Installing http://rocks.tarantool.org/pg-scm-1.rockspec...`
    | :codenormal:`... (more information about building the Tarantool/PostgreSQL driver appears here) ...`
    | :codenormal:`pg scm-1 is now built and installed in ~/.luarocks/`
    |
    | :codenormal:`# Ensure driver.so now has been created in a place tarantool will look at`
    | :codenormal:`$` :codebold:`find ~/.luarocks -name "driver.so"`
    | :codenormal:`~/.luarocks/lib/lua/5.1/pg/driver.so`
    |
    | :codenormal:`# Change directory to a directory which can be used for temporary tests.`
    | :codenormal:`# For this example we assume that the name of this directory is`
    | :codenormal:`# /home/pgulutzan/tarantool_sandbox. (Change "/home/pgulutzan" to whatever`
    | :codenormal:`# is the user's actual home directory for the machine that's used for this test.)`
    | :codebold:`cd /home/pgulutzan/tarantool_sandbox`
    |
    | :codenormal:`# Start the Tarantool server. Do not use a Lua initialization file.`
    |
    | :codenormal:`$` :codebold:`tarantool`
    | :codenormal:`tarantool: version 1.6.6-222-g48b98bb`
    | :codenormal:`type 'help' for interactive help`
    | :codenormal:`tarantool>` :codebold:`box.cfg{}`
    | :codenormal:`...`
    | :codenormal:`# Request the pg package`
    | :codenormal:`tarantool>` :codebold:`pg = require('pg')`
    | :codenormal:`# ... Make sure that tarantool does not reply "error" for the call to "require()".`
    |
    | :codenormal:`# Create a Lua function that will connect to the PostgreSQL server,`
    | :codenormal:`# (using some factory default values for the port and user and password),`
    | :codenormal:`# retrieve one row, and display the row.`
    | :codenormal:`# For explanations of the statement types used here, read the`
    | :codenormal:`# Lua tutorial earlier in the Tarantool user manual.`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`function pg_select ()`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local conn = pg.connect(`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`{host='127.0.0.1', port=5432, user='postgres', password='postgres', db='postgres'})`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local test = conn:execute('SELECT * FROM test WHERE s1 = 1')`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local row = ''`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`for i, card in pairs(test) do`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`row = row .. card.s2 .. ' '`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`end`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`conn:close()`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`return row`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`end!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console.delimiter('')!`
    | :codenormal:`tarantool>`
    |
    | :codenormal:`# Execute the Lua function.`
    | :codenormal:`tarantool>` :codebold:`pg_select()`
    | :codenormal:`---`
    | :codenormal:`- 'PostgreSQL row '`
    | :codenormal:`...`
    | :codenormal:`# Observe the result. It contains "PostgreSQL row".`
    | :codenormal:`# So this is the row that was inserted into the PostgreSQL database.`
    | :codenormal:`# And now it's been selected with the Tarantool client.`


.. _gist.github.com/rtsisyk/aa95cf9ed9bbb538ff80: https://gist.github.com/rtsisyk/aa95cf9ed9bbb538ff80
.. _rocks.tarantool.org: http://rocks.tarantool.org/
.. _github.com/tarantool/mysql: https://github.com/tarantool/mysql
.. _dev.mysql.com/doc/refman/5.6/en/connecting.html: https://dev.mysql.com/doc/refman/5.6/en/connecting.html
.. _github.com/tarantool/mysql: https://github.com/tarantool/mysql
.. _github.com/tarantool/pg: https://github.com/tarantool/pg

