.. _dbms-plugins:

-------------------------------------------------------------------------------
                        Appendix D. Plugins
-------------------------------------------------------------------------------

A plugin is an optional library which enhances Tarantool functionality.

The details of creating one's own plugin are described on the `Tarantool Plugin API wiki page`_.

The discussion here in the user guide is about incorporating and using two
plugins that have already been created: the "SQL DBMS plugins" for
MySQL and PostgreSQL.

===========================================================
                  SQL DBMS Plugins
===========================================================

To call another DBMS from Tarantool, the essential requirements are: another
DBMS, and Tarantool.

It will be necessary to build Tarantool from source, as described in
“:ref:`building-from-source`”.

.. _Tarantool Plugin API wiki page: https://github.com/tarantool/tarantool/wiki/Plugin-API

The Tarantool plugins allow for connecting to an SQL server and executing SQL
statements the same way that a MySQL or PostgreSQL client does. The SQL
statements are visible as Lua methods. Thus Tarantool can serve as a "MySQL Lua
Connector" or "PostgreSQL Lua Connector", which would be useful even if that was
all Tarantool could do. But of course Tarantool is also a DBMS, so the plugin
also is useful for any operations, such as database copying and accelerating,
which work best when the application can work on both SQL and Tarantool inside
the same Lua routine.

The connection method is
:samp:`box.net.sql.connect('mysql'|'pg', {host}, {port}, {user}, {password}, {database})`.
The methods for select/insert/etc. are the same as the ones in the
:ref:`net.box <package_net_box>` package.


===========================================================
                  MySQL Example
===========================================================

This example assumes that MySQL 5.5 or MySQL 5.6 has been installed (recent
MariaDB versions should also work).

The example was run on a Linux machine where the base directory had a copy of
the Tarantool source on ~/tarantool, and a copy of MySQL on ~/mysql-5.5. The
mysqld server is already running on the local host 127.0.0.1.

    | :codenormal:`# Check that the include subdirectory exists by looking for .../include/mysql.h.`
    | :codenormal:`# (If this fails, there's a chance that it's in .../include/mysql/mysql.h instead.)`
    | :codenormal:`$` :codebold:`[ -f ~/mysql-5.5/include/mysql.h ] && echo "OK" || echo "Error"`
    | :codenormal:`OK`
    |
    | :codenormal:`# Check that the library subdirectory exists and has the necessary .so file.`
    | :codenormal:`$` :codebold:`[ -f ~/mysql-5.5/lib/libmysqlclient.so ] && echo "OK" || echo "Error"`
    | :codenormal:`OK`
    |
    | :codenormal:`# Check that the mysql client can connect using some factory defaults:`
    | :codenormal:`# port = 3306, user = 'root', user password = '', database = 'test'.`
    | :codenormal:`# These can be changed, provided one uses the changed values in`
    | :codenormal:`# all places.`
    | :codenormal:`$` :codebold:`~/mysql-5.5/bin/mysql --port=3306 -h 127.0.0.1 --user=root --password= --database=test`
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
    | :codenormal:`# Build the Tarantool server. Make certain that "cmake" gets the right`
    | :codenormal:`# paths for the MySQL include directory and the MySQL libmysqlclient`
    | :codenormal:`# library which were checked earlier.`
    | :codenormal:`$` :codebold:`cd ~/tarantool`
    | :codenormal:`$` :codebold:`make clean`
    | :codenormal:`$` :codebold:`rm CMakeCache.txt`
    | :codenormal:`$` :codebold:`cmake . -DWITH_MYSQL=on -DMYSQL_INCLUDE_DIR=~/mysql-5.5/include\\`
    | :codenormal:`>` |nbsp| |nbsp| :codebold:`-DMYSQL_LIBRARIES=~/mysql-5.5/lib/libmysqlclient.so`
    | :codenormal:`...`
    | :codenormal:`-- Found MySQL includes: ~/mysql-5.5/include/mysql.h`
    | :codenormal:`-- Found MySQL library: ~/mysql-5.5/lib/libmysqlclient.so`
    | :codenormal:`...`
    | :codenormal:`-- Configuring done`
    | :codenormal:`-- Generating done`
    | :codenormal:`-- Build files have been written to: ~/tarantool`
    | :codenormal:`$` :codebold:`make`
    | :codenormal:`...`
    | :codenormal:`Scanning dependencies of target mysql`
    | :codenormal:`[ 79%] Building CXX object src/module/mysql/CMakeFiles/mysql.dir/mysql.cc.o`
    | :codenormal:`Linking CXX shared library libmysql.so`
    | :codenormal:`[ 79%] Built target mysql`
    | :codenormal:`...`
    | :codenormal:`[100%] Built target man`
    | :codenormal:`$`
    |
    | :codenormal:`# The MySQL module should now be in ./src/module/mysql/mysql.so.`
    | :codenormal:`# If a "make install" had been done, then mysql.so would be in a`
    | :codenormal:`# different place, for example`
    | :codenormal:`# /usr/local/lib/x86_64-linux-gnu/tarantool/box/net/mysql.so.`
    | :codenormal:`# In that case there should be additional cmake options such as`
    | :codenormal:`# -DCMAKE_INSTALL_LIBDIR and -DCMAKE_INSTALL_PREFIX.`
    | :codenormal:`# For this example we assume that "make install" is not done.`
    |
    | :codenormal:`# Change directory to a directory which can be used for temporary tests.`
    | :codenormal:`# For this example we assume that the name of this directory is`
    | :codenormal:`# /home/pgulutzan/tarantool_sandbox. (Change "/home/pgulutzan" to whatever`
    | :codenormal:`# is the actual base directory for the machine that's used for this test.)`
    | :codenormal:`# Now, to help tarantool find the essential mysql.so file, execute these lines:`
    | :codebold:`cd /home/pgulutzan/tarantool_sandbox`
    | :codebold:`mkdir box`
    | :codebold:`mkdir box/net`
    | :codebold:`cp ~/tarantool/src/module/mysql/mysql.so ./box/net/mysql.so`
    |
    | :codenormal:`# Start the Tarantool server. Do not use a Lua initialization file.`
    |
    | :codenormal:`$` :codebold:`~/tarantool/src/tarantool`
    | :codenormal:`~/tarantool/src/tarantool: version 1.6.3-439-g7e1011b`
    | :codenormal:`type 'help' for interactive help`
    | :codenormal:`tarantool>` :codebold:`box.cfg{}`
    | :codenormal:`...`
    | :codenormal:`# Enter the following lines on the prompt (again, change "/home/pgulutzan"`
    | :codenormal:`# to whatever the real directory is that contains tarantool):`
    | :codenormal:`package.path = "/home/pgulutzan/tarantool/src/module/sql/?.lua;"..package.path`
    | :codenormal:`require("sql")`
    | :codenormal:`if type(box.net.sql) ~= "table" then error("net.sql load failed") end`
    | :codenormal:`require("box.net.mysql")`
    | :codenormal:`# ... Make sure that tarantool replies "true" for both calls to "require()".`
    |
    | :codenormal:`# Create a Lua function that will connect to the MySQL server,`
    | :codenormal:`# (using some factory default values for the port and user and password),`
    | :codenormal:`# retrieve one row, and display the row.`
    | :codenormal:`# For explanations of the statement types used here, read the`
    | :codenormal:`# Lua tutorial earlier in the Tarantool user manual.`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`function mysql_select ()`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local dbh = box.net.sql.connect(`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`'mysql', '127.0.0.1', 3306, 'root', '', 'test')`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local test = dbh:select('SELECT * FROM test WHERE s1 = 1')`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`local row = ''`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| :codebold:`for i, card in pairs(test) do`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`row = row .. card.s2 .. ' '`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| :codebold:`end`
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

This example assumes that a recent version of PostgreSQL has been installed.
The PostgreSQL library and include files are also necessary. On Ubuntu they
can be installed with

    | :codebold:`$ sudo apt-get install libpq-dev`

If that works, then cmake will find the necessary files without requiring any
special user input. However, because not all platforms are alike, for this
example the assumption is that the user must check that the appropriate
PostgreSQL files are present and must explicitly state where they are when
building Tarantool from source.

The example was run on a Linux machine where the base directory had a copy of
the Tarantool source on ~/tarantool, and a copy of PostgreSQL on /usr. The
postgres server is already running on the local host 127.0.0.1.

    | :codenormal:`# Check that the include subdirectory exists`
    | :codenormal:`# by looking for /usr/include/postgresql/libpq-fe-h.`
    | :codenormal:`$` :codebold:`[ -f /usr/include/postgresql/libpq-fe.h ] && echo "OK" || echo "Error"`
    | :codenormal:`OK`
    |
    | :codenormal:`# Check that the library subdirectory exists and has the necessary .so file.`
    | :codenormal:`$` :codebold:`[ -f /usr/lib/libpq.so ] && echo "OK" || echo "Error"`
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
    | :codenormal:`# Build the Tarantool server. Make certain that "cmake" gets the right`
    | :codenormal:`# paths for the PostgreSQL include directory and the PostgreSQL libpq`
    | :codenormal:`# library which were checked earlier.`
    | :codenormal:`$` :codebold:`cd ~/tarantool`
    | :codenormal:`$` :codebold:`make clean`
    | :codenormal:`$` :codebold:`rm CMakeCache.txt`
    | :codenormal:`$` :codebold:`cmake . -DWITH_POSTGRESQL=on -DPostgreSQL_LIBRARY=/usr/lib/libpq.so\\`
    | :codenormal:`>` |nbsp| :codebold:`-DPostgreSQL_INCLUDE_DIR=/usr/include/postgresql`
    | :codenormal:`...`
    | :codenormal:`-- Found PostgreSQL: /usr/lib/libpq.so (found version "9.3.2")`
    | :codenormal:`...`
    | :codenormal:`-- Configuring done`
    | :codenormal:`-- Generating done`
    | :codenormal:`-- Build files have been written to: ~/tarantool`
    | :codenormal:`$` :codebold:`make`
    | :codenormal:`...`
    | :codenormal:`[ 79%] Building CXX object src/plugin/pg/CMakeFiles/pg.dir/pg.cc.o`
    | :codenormal:`Linking CXX shared library libpg.so`
    | :codenormal:`[ 79%] Built target pg`
    | :codenormal:`...`
    | :codenormal:`[100%] Built target man`
    | :codenormal:`$`
    |
    | :codenormal:`# Change directory to a directory which can be used for temporary tests.`
    | :codenormal:`# For this example we assume that the name of this directory is`
    | :codenormal:`# /home/pgulutzan/tarantool_sandbox. (Change "/home/pgulutzan" to whatever`
    | :codenormal:`# is the actual base directory for the machine that's used for this test.)`
    | :codenormal:`# Now, to help tarantool find the essential mysql.so file, execute these lines:`
    | :codebold:`cd /home/pgulutzan/tarantool_sandbox`
    | :codebold:`mkdir box`
    | :codebold:`mkdir box/net`
    | :codebold:`cp ~/tarantool/src/module/pg/pg.so ./box/net/pg.so`
    |
    | :codenormal:`# Start the Tarantool server. Do not use a Lua initialization file.`
    |
    | :codenormal:`$` :codebold:`~/tarantool/src/tarantool`
    | :codenormal:`~/tarantool/src/tarantool: version 1.6.3-439-g7e1011b`
    | :codenormal:`type 'help' for interactive help`
    | :codenormal:`tarantool>` :codebold:`box.cfg{}`
    |
    | :codenormal:`# Enter the following lines on the prompt (again, change "/home/pgulutzan"`
    | :codenormal:`# to whatever the real directory is that contains tarantool):`
    | :codenormal:`package.path = "/home/pgulutzan/tarantool/src/module/sql/?.lua;"..package.path`
    | :codenormal:`require("sql")`
    | :codenormal:`if type(box.net.sql) ~= "table" then error("net.sql load failed") end`
    | :codenormal:`require("box.net.pg")`
    | :codenormal:`# ... Make sure that tarantool replies "true" for the calls to "require()".`
    |
    | :codenormal:`# Create a Lua function that will connect to the PostgreSQL server,`
    | :codenormal:`# retrieve one row, and display the row.`
    | :codenormal:`# For explanations of the statement types used here, read the`
    | :codenormal:`# Lua tutorial in the Tarantool user manual.`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`function postgresql_select ()`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`local dbh = box.net.sql.connect(`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| |nbsp| :codebold:`'pg', '127.0.0.1', 5432, 'postgres', 'postgres', 'postgres')`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`local test = dbh:select('SELECT * FROM test WHERE s1 = 1')`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`local row = ''`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`for i, card in pairs(test) do`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| |nbsp| :codebold:`row = row .. card.s2 .. ' '`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| |nbsp| :codebold:`end`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`return row`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`end!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console.delimiter('')!`
    | :codenormal:`tarantool>`
    |
    | :codenormal:`# Execute the Lua function.`
    | :codenormal:`tarantool>` :codebold:`postgresql_select()`
    | :codenormal:`---`
    | :codenormal:`- 'PostgreSQL row '`
    | :codenormal:`...`
    |
    | :codenormal:`# Observe the result. It contains "PostgreSQL row".`
    | :codenormal:`# So this is the row that was inserted into the PostgreSQL database.`
    | :codenormal:`# And now it's been selected with the Tarantool client.`
