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
“ :ref:`building-from-source` ”

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
``box.net.sql.connect('mysql'|'pg', host, port, user, password, database)``.
The methods for select/insert/etc. are the same as the ones in the net.box package.


===========================================================
                  MySQL Example
===========================================================

This example assumes that MySQL 5.5 or MySQL 5.6 has been installed (recent
MariaDB versions should also work).

The example was run on a Linux machine where the base directory had a copy of
the Tarantool source on ~/tarantool, and a copy of MySQL on ~/mysql-5.5. The
mysqld server is already running on the local host 127.0.0.1.

::

    # Check that the include subdirectory exists by looking for .../include/mysql.h.
    # (If this fails, there's a chance that it's in .../include/mysql/mysql.h instead.)
    $ [ -f ~/mysql-5.5/include/mysql.h ] && echo "OK" || echo "Error"
    OK

    # Check that the library subdirectory exists and has the necessary .so file.
    $ [ -f ~/mysql-5.5/lib/libmysqlclient.so ] && echo "OK" || echo "Error"
    OK

    # Check that the mysql client can connect using some factory defaults:
    # port = 3306, user = 'root', user password = '', database = 'test'.
    # These can be changed, provided one uses the changed values in
    # all places.
    $ ~/mysql-5.5/bin/mysql --port=3306 -h 127.0.0.1 --user=root --password= --database=test
    Welcome to the MySQL monitor.  Commands end with ; or \g.
    Your MySQL connection id is 25
    Server version: 5.5.35 MySQL Community Server (GPL)
    ...
    Type 'help;' or '\h' for help. Type '\c' to clear the current input statement.

    # Insert a row in database test, and quit.
    mysql> CREATE TABLE IF NOT EXISTS test (s1 INT, s2 VARCHAR(50));
    Query OK, 0 rows affected (0.13 sec)
    mysql> INSERT INTO test.test VALUES (1,'MySQL row');
    Query OK, 1 row affected (0.02 sec)
    mysql> **QUIT**
    Bye

    # Build the Tarantool server. Make certain that "cmake" gets the right
    # paths for the MySQL include directory and the MySQL libmysqlclient
    # library which were checked earlier.
    $ cd ~/tarantool
    $ make clean
    $ rm CMakeCache.txt
    $ cmake . -DWITH_MYSQL=on -DMYSQL_INCLUDE_DIR=~/mysql-5.5/include\
    >  -DMYSQL_LIBRARIES=~/mysql-5.5/lib/libmysqlclient.so
    ...
    -- Found MySQL includes: ~/mysql-5.5/include/mysql.h
    -- Found MySQL library: ~/mysql-5.5/lib/libmysqlclient.so
    ...
    -- Configuring done
    -- Generating done
    -- Build files have been written to: ~/tarantool
    $ make
    ...
    Scanning dependencies of target mysql
    [ 79%] Building CXX object src/module/mysql/CMakeFiles/mysql.dir/mysql.cc.o
    Linking CXX shared library libmysql.so
    [ 79%] Built target mysql
    ...
    [100%] Built target man
    $

    # The MySQL module should now be in ./src/module/mysql/mysql.so.
    # If a "make install" had been done, then mysql.so would be in a
    # different place, for example
    # /usr/local/lib/x86_64-linux-gnu/tarantool/box/net/mysql.so.
    # In that case there should be additional cmake options such as
    # -DCMAKE_INSTALL_LIBDIR and -DCMAKE_INSTALL_PREFIX.
    # For this example we assume that "make install" is not done.

    # Change directory to a directory which can be used for temporary tests.
    # For this example we assume that the name of this directory is
    # /home/pgulutzan/tarantool_sandbox. (Change "/home/pgulutzan" to whatever
    # is the actual base directory for the machine that's used for this test.)
    # Now, to help tarantool find the essential mysql.so file, execute these lines:
    cd /home/pgulutzan/tarantool_sandbox
    mkdir box
    mkdir box/net
    cp ~/tarantool/src/module/mysql/mysql.so ./box/net/mysql.so

    # Start the Tarantool server. Do not use a Lua initialization file.

    $ ~/tarantool/src/tarantool
    ~/tarantool/src/tarantool: version 1.6.3-439-g7e1011b
    type 'help' for interactive help
    tarantool>  box.cfg{}
    ...
    # Enter the following lines on the prompt (again, change "/home/pgulutzan"
    # to whatever the real directory is that contains tarantool):
    package.path = "/home/pgulutzan/tarantool/src/module/sql/?.lua;"..package.path
    require("sql")
    if type(box.net.sql) ~= "table" then error("net.sql load failed") end
    require("box.net.mysql")
    # ... Make sure that tarantool replies "true" for both calls to "require()".

    # Create a Lua function that will connect to the MySQL server,
    # (using some factory default values for the port and user and password),
    # retrieve one row, and display the row.
    # For explanations of the statement types used here, read the
    # Lua tutorial earlier in the Tarantool user manual.
    tarantool> console = require('console'); console.delimiter('!')
    tarantool> function mysql_select ()
            ->   local dbh = box.net.sql.connect(
            ->       'mysql', '127.0.0.1', 3306, 'root', '', 'test')
            ->   local test = dbh:select('SELECT * FROM test WHERE s1 = 1')
            ->    local row = ''
            ->   for i, card in pairs(test) do
            ->     row = row .. card.s2 .. ' '
            ->     end
            ->   return row
            ->   end!
    ---
    ...
    tarantool> console.delimiter('')!
    tarantool>

    # Execute the Lua function.
    tarantool> mysql_select()
    ---
    - 'MySQL row '
    ...
    # Observe the result. It contains "MySQL row".
    # So this is the row that was inserted into the MySQL database.
    # And now it's been selected with the Tarantool client.

===========================================================
                  PostgreSQL Example
===========================================================

This example assumes that a recent version of PostgreSQL has been installed.
The PostgreSQL library and include files are also necessary. On Ubuntu they
can be installed with

.. code-block:: bash

    $ sudo apt-get install libpq-dev

If that works, then cmake will find the necessary files without requiring any
special user input. However, because not all platforms are alike, for this
example the assumption is that the user must check that the appropriate
PostgreSQL files are present and must explicitly state where they are when
building Tarantool from source.

The example was run on a Linux machine where the base directory had a copy of
the Tarantool source on ~/tarantool, and a copy of PostgreSQL on /usr. The
postgres server is already running on the local host 127.0.0.1.

::

    # Check that the include subdirectory exists
    # by looking for /usr/include/postgresql/libpq-fe-h.
    $ [ -f /usr/include/postgresql/libpq-fe.h ] && echo "OK" || echo "Error"
    OK

    # Check that the library subdirectory exists and has the necessary .so file.
    $ [ -f /usr/lib/libpq.so ] && echo "OK" || echo "Error"
    OK

    # Check that the psql client can connect using some factory defaults:
    # port = 5432, user = 'postgres', user password = 'postgres', database = 'postgres'.
    # These can be changed, provided one changes them in all places.
    # Insert a row in database postgres, and quit.
    $ psql -h 127.0.0.1 -p 5432 -U postgres -d postgres
    Password for user postgres:
    psql (9.3.0, server 9.3.2)
    SSL connection (cipher: DHE-RSA-AES256-SHA, bits: 256)
    Type "help" for help.

    postgres=# CREATE TABLE test (s1 INT, s2 VARCHAR(50));
    CREATE TABLE
    postgres=# INSERT INTO test VALUES (1,'PostgreSQL row');
    INSERT 0 1
    postgres=# \q
    $

    # Build the Tarantool server. Make certain that "cmake" gets the right
    # paths for the PostgreSQL include directory and the PostgreSQL libpq
    # library which were checked earlier.
    $ cd ~/tarantool
    $ make clean
    $ rm CMakeCache.txt
    $ cmake . -DWITH_POSTGRESQL=on -DPostgreSQL_LIBRARY=/usr/lib/libpq.so\
    >  -DPostgreSQL_INCLUDE_DIR=/usr/include/postgresql
    ...
    -- Found PostgreSQL: /usr/lib/libpq.so (found version "9.3.2")
    ...
    -- Configuring done
    -- Generating done
    -- Build files have been written to: ~/tarantool
    $ make
    ...
    [ 79%] Building CXX object src/plugin/pg/CMakeFiles/pg.dir/pg.cc.o
    Linking CXX shared library libpg.so
    [ 79%] Built target pg
    ...
    [100%] Built target man
    $

    # Change directory to a directory which can be used for temporary tests.
    # For this example we assume that the name of this directory is
    # /home/pgulutzan/tarantool_sandbox. (Change "/home/pgulutzan" to whatever
    # is the actual base directory for the machine that's used for this test.)
    # Now, to help tarantool find the essential mysql.so file, execute these lines:
    cd /home/pgulutzan/tarantool_sandbox
    mkdir box
    mkdir box/net
    cp ~/tarantool/src/module/pg/pg.so ./box/net/pg.so

    # Start the Tarantool server. Do not use a Lua initialization file.

    $ ~/tarantool/src/tarantool
    ~/tarantool/src/tarantool: version 1.6.3-439-g7e1011b
    type 'help' for interactive help
    tarantool>   box.cfg{}

    # Enter the following lines on the prompt (again, change "/home/pgulutzan"
    # to whatever the real directory is that contains tarantool):
    package.path = "/home/pgulutzan/tarantool/src/module/sql/?.lua;"..package.path
    require("sql")
    if type(box.net.sql) ~= "table" then error("net.sql load failed") end
    require("box.net.pg")
    # ... Make sure that tarantool replies "true" for the calls to "require()".

    # Create a Lua function that will connect to the PostgreSQL server,
    # retrieve one row, and display the row.
    # For explanations of the statement types used here, read the
    # Lua tutorial in the Tarantool user manual.
    tarantool> console = require('console'); console.delimiter('!')
    tarantool> function postgresql_select ()
            ->   local dbh = box.net.sql.connect(
            ->       'pg', '127.0.0.1', 5432, 'postgres', 'postgres', 'postgres')
            ->   local test = dbh:select('SELECT * FROM test WHERE s1 = 1')
            ->   local row = ''
            ->   for i, card in pairs(test) do
            ->     row = row .. card.s2 .. ' '
            ->     end
             >   return row
            ->   end!
    ---
    ...
    tarantool> console.delimiter('')!
    tarantool>

    # Execute the Lua function.
    tarantool> postgresql_select()
    ---
    - 'PostgreSQL row '
    ...

    # Observe the result. It contains "PostgreSQL row".
    # So this is the row that was inserted into the PostgreSQL database.
    # And now it's been selected with the Tarantool client.
