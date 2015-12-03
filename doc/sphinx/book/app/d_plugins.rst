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

This example assumes that MySQL 5.5 or MySQL 5.6 has been installed. Recent
MariaDB versions should also work. The package that matters most is the MySQL
client developer package, typically named something like libmysqlclient-dev.
The file that matters most from this package is libmysqlclient.so or a similar name.
One can use :code:`find` or :code:`whereis` to see what directories these files
are installed in.

It will be necessary to install Tarantool's MySQL driver shared library, load
it, and use it to connect to a MySQL server. After that, one can pass any MySQL
statement to the server and receive results, including multiple result sets.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Check the instructions for :ref:`Downloading and installing a binary package <downloading-and-installing-a-binary-package>`
that apply for the environment where tarantool was installed. In addition to
installing :code:`tarantool`, install :code:`tarantool-dev`. For example, on
Ubuntu, add the line

.. code-block:: bash

    sudo apt-get install tarantool-dev

Now, for the MySQL driver shared library, there are two ways to install:

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
       With LuaRocks
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Begin by installing luarocks and making sure that tarantool is among the upstream
servers, as in the instructions on `rocks.tarantool.org`_, the Tarantool luarocks
page. Now execute this:

.. cssclass:: highlight
.. parsed-literal::

    luarocks install mysql [MYSQL_LIBDIR = *path*]
                           [MYSQL_INCDIR = *path*]
                           [--local]

For example:

.. code-block:: bash

    luarocks install mysql MYSQL_LIBDIR=/usr/local/mysql/lib

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
       With GitHub
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Go the site `github.com/tarantool/mysql`_. Follow the instructions there, saying:

.. code-block:: bash

    git clone https://github.com/tarantool/mysql.git
    cd mysql && cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
    make
    make install

At this point it is a good idea to check that the installation produced a file
named :code:`driver.so`, and to check that this file is on a directory that is
searched by the :code:`require` request.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         Connecting
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Begin by making a :code:`require` request for the mysql driver. We will assume
that the name is :code:`mysql` in further examples.

.. code-block:: lua

    mysql = require('mysql')

Now, say:

.. cssclass:: highlight
.. parsed-literal::

    *connection_name* = mysql.connect(*connection options*)

The connection-options parameter is a table. Possible options are:

* :samp:`host = {host-name}` - string, default value = 'localhost'
* :samp:`port = {port-number}` - number, default value = 3306
* :samp:`user = {user-name}` - string, default value is operating-system user name
* :samp:`password = {password}` - string, default value is blank
* :samp:`db = {database-name}` - string, default value is blank

The names are similar to the names that MySQL's mysql client uses, for details
see the MySQL manual at `dev.mysql.com/doc/refman/5.6/en/connecting.html`_.
To connect with a Unix socket rather than with TCP, specify ``host = 'unix/'``
and :samp:`port = {socket-name}`.

Example, using a table literal enclosed in {braces}:

.. code-block:: lua

    conn = mysql.connect({
        host = '127.0.0.1',
        port = 3306,
        user = 'p',
        password = 'p',
        db = 'test'
    })
    -- OR
    conn = mysql.connect({
        host = 'unix/',
        port = '/var/run/mysqld/mysqld.sock'
    })

Example, creating a function which sets each option in a separate line:

.. code-block:: tarantoolsession

    tarantool> -- Connection function. Usage: conn = mysql_connect()
    tarantool> function mysql_connection()
             >   local p = {}
             >   p.host = 'widgets.com'
             >   p.db = 'test'
             >   conn = mysql.connect(p)
             >   return conn
             > end
    ---
    ...
    tarantool> conn = mysql_connect()
    ---
    ...

We will assume that the name is 'conn' in further examples.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        How to ping
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To ensure that a connection is working, the request is:

.. cssclass:: highlight
.. parsed-literal::

    *connection-name*:ping()

**Example:**

.. code-block:: tarantoolsession

    tarantool> conn:ping()
    ---
    - true
    ...

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Executing a statement
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For all MySQL statements, the request is:

.. cssclass:: highlight
.. parsed-literal::

    *connection-name*:execute(*sql-statement* [, *parameters*])

where :code:`sql-statement` is a string, and the optional :code:`parameters`
are extra values that can be plugged in to replace any question marks ("?"s)
in the SQL statement.

**Example:**

.. code-block:: tarantoolsession

    tarantool> conn:execute('select table_name from information_schema.tables')
    ---
    - - table_name: ALL_PLUGINS
      - table_name: APPLICABLE_ROLES
      - table_name: CHARACTER_SETS
      <...>
    - 78
    ...

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      Closing connection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To end a session that began with :code:`mysql.connect`, the request is:

.. cssclass:: highlight
.. parsed-literal::

    *connection-name*:close()

**Example:**

.. code-block:: tarantoolsession

    tarantool> conn:close()
    ---
    ...

For further information, including examples of rarely-used requests, see the
README.md file at `github.com/tarantool/mysql`_.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
           Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example was run on an Ubuntu 12.04 ("precise") machine where tarantool had
been installed in a /usr subdirectory, and a copy of MySQL had been installed
on ~/mysql-5.5. The mysqld server is already running on the local host 127.0.0.1.

.. code-block:: console

    $ export TMDIR=~/mysql-5.5
    $ # Check that the include subdirectory exists by looking
    $ # for .../include/mysql.h. (If this fails, there's a chance
    $ # that it's in .../include/mysql/mysql.h instead.)
    $ [ -f $TMDIR/include/mysql.h ] && echo "OK" || echo "Error"
    OK

    $ # Check that the library subdirectory exists and has the
    $ # necessary .so file.
    $ [ -f $TMDIR/lib/libmysqlclient.so ] && echo "OK" || echo "Error"
    OK

    $ # Check that the mysql client can connect using some factory
    $ # defaults: port = 3306, user = 'root', user password = '',
    $ # database = 'test'. These can be changed, provided one uses
    $ # the changed values in all places.
    $ $TMDIR/bin/mysql --port=3306 -h 127.0.0.1 --user=root \
        --password= --database=test
    Welcome to the MySQL monitor.  Commands end with ; or \g.
    Your MySQL connection id is 25
    Server version: 5.5.35 MySQL Community Server (GPL)
    ...
    Type 'help;' or '\h' for help. Type '\c' to clear ...

    $ # Insert a row in database test, and quit.
    mysql> CREATE TABLE IF NOT EXISTS test (s1 INT, s2 VARCHAR(50));
    Query OK, 0 rows affected (0.13 sec)
    mysql> INSERT INTO test.test VALUES (1,'MySQL row');
    Query OK, 1 row affected (0.02 sec)
    mysql> QUIT
    Bye

    $ # Install luarocks
    $ sudo apt-get -y install luarocks | grep -E "Setting up|already"
    Setting up luarocks (2.0.8-2) ...

    $ # Set up the Tarantool rock list in ~/.luarocks,
    $ # following instructions at rocks.tarantool.org
    $ mkdir ~/.luarocks
    $ echo "rocks_servers = {[[http://rocks.tarantool.org/]]}" >> \
        ~/.luarocks/config.lua

    $ # Ensure that the next "install" will get files from Tarantool
    $ # master repository. The resultant display is normal for Ubuntu
    $ # 12.04 precise
    $ cat /etc/apt/sources.list.d/tarantool.list
    deb http://tarantool.org/dist/master/ubuntu/ precise main
    deb-src http://tarantool.org/dist/master/ubuntu/ precise main

    $ # Install tarantool-dev. The displayed line should show version = 1.6
    $ sudo apt-get -y install tarantool-dev | grep -E "Setting up|already"
    Setting up tarantool-dev (1.6.6.222.g48b98bb~precise-1) ...
    $

    $ # Use luarocks to install locally, that is, relative to $HOME
    $ luarocks install mysql MYSQL_LIBDIR=/usr/local/mysql/lib --local
    Installing http://rocks.tarantool.org/mysql-scm-1.rockspec...
    ... (more info about building the Tarantool/MySQL driver appears here)
    mysql scm-1 is now built and installed in ~/.luarocks/

    $ # Ensure driver.so now has been created in a place
    $ # tarantool will look at
    $ find ~/.luarocks -name "driver.so"
    ~/.luarocks/lib/lua/5.1/mysql/driver.so

    $ # Change directory to a directory which can be used for
    $ # temporary tests. For this example we assume that the name
    $ # of this directory is /home/pgulutzan/tarantool_sandbox.
    $ # (Change "/home/pgulutzan" to whatever is the user's actual
    $ # home directory for the machine that's used for this test.)
    $ cd /home/pgulutzan/tarantool_sandbox

    $ # Start the Tarantool server. Do not use a Lua initialization file.

    $ tarantool
    tarantool: version 1.6.6-222-g48b98bb
    type 'help' for interactive help
    tarantool>

Configure tarantool and load mysql module. Make sure that tarantool doesn't
reply "error" for the call to "require()".

.. code-block:: tarantoolsession

    tarantool> box.cfg{}
    ...
    tarantool> mysql = require('mysql')
    ---
    ...

Create a Lua function that will connect to the MySQL server, (using some factory
default values for the port and user and password), retrieve one row, and
display the row. For explanations of the statement types used here, read the
Lua tutorial earlier in the Tarantool user manual.

.. code-block:: tarantoolsession

    tarantool> function mysql_select ()
             >   local conn = mysql.connect({
             >     host = '127.0.0.1',
             >     port = 3306,
             >     user = 'root',
             >     db = 'test'
             >   })
             >   local test = conn:execute('SELECT * FROM test WHERE s1 = 1')
             >   local row = ''
             >   for i, card in pairs(test) do
             >       row = row .. card.s2 .. ' '
             >       end
             >   conn:close()
             >   return row
             > end
    ---
    ...
    tarantool> mysql_select()
    ---
    - 'MySQL row '
    ...

Observe the result. It contains "MySQL row". So this is the row that was inserted
into the MySQL database. And now it's been selected with the Tarantool client.

===========================================================
                  PostgreSQL Example
===========================================================

This example assumes that PostgreSQL 8 or PostgreSQL 9 has been installed.
More recent versions should also work. The package that matters most is the
PostgreSQL developer package, typically named something like libpq-dev.
On Ubuntu this can be installed with:

.. code-block:: bash

    sudo apt-get install libpq-dev

However, because not all platforms are alike, for this example the assumption
is that the user must check that the appropriate PostgreSQL files are present
and must explicitly state where they are when building the Tarantool/PostgreSQL
driver. One can use :code:`find` or :code:`whereis` to see what directories
PostgreSQL files are installed in.

It will be necessary to install Tarantool's PostgreSQL driver shared library,
load it, and use it to connect to a PostgreSQL server. After that, one can pass
any PostgreSQL statement to the server and receive results.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Check the instructions for
:ref:`Downloading and installing a binary package <downloading-and-installing-a-binary-package>`
that apply for the environment where tarantool was installed. In addition to
installing :code:`tarantool`, install :code:`tarantool-dev`. For example, on
Ubuntu, add the line:

.. code-block:: bash

    sudo apt-get install tarantool-dev

Now, for the PostgreSQL driver shared library, there are two ways to install:

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
       With LuaRocks
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Begin by installing luarocks and making sure that tarantool is among the upstream
servers, as in the instructions on `rocks.tarantool.org`_, the Tarantool luarocks
page. Now execute this:

.. cssclass:: highlight
.. parsed-literal::

    luarocks install pg [POSTGRESQL_LIBDIR = *path*]
                        [POSTGRESQL_INCDIR = *path*]
                        [--local]

For example:

.. code-block:: bash

    luarocks install pg POSTGRESQL_LIBDIR=/usr/local/postgresql/lib

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
       With GitHub
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Go the site `github.com/tarantool/pg`_. Follow the instructions there, saying:

.. code-block:: bash

    git clone https://github.com/tarantool/pg.git
    cd pg && cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
    make
    make install

At this point it is a good idea to check that the installation produced a file
named :code:`driver.so`, and to check that this file is on a directory that is
searched by the :code:`require` request.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         Connecting
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Begin by making a :code:`require` request for the pg driver. We will assume that
the name is :code:`pg` in further examples.

.. code-block:: lua

    pg = require('pg')

Now, say:

.. cssclass:: highlight
.. parsed-literal::

    *connection_name* = pg.connect(*connection options*)

The connection-options parameter is a table. Possible options are:

* :samp:`host = {host-name}` - string, default value = 'localhost'
* :samp:`port = {port-number}` - number, default value = 3306
* :samp:`user = {user-name}` - string, default value is operating-system user name
* :samp:`pass = {password}` or :samp:`password = {password}` - string, default value is blank
* :samp:`db = {database-name}` - string, default value is blank

The names are similar to the names that PostgreSQL itself uses.

Example, using a table literal enclosed in {braces}:

.. code-block:: lua

    conn = pg.connect({
        host = '127.0.0.1',
        port = 5432,
        user = 'p',
        password = 'p',
        db = 'test'
    })

Example, creating a function which sets each option in a separate line:

.. code-block:: tarantoolsession

    tarantool> function pg_connect()
             >   local p = {}
             >   p.host = 'widgets.com'
             >   p.db = 'test'
             >   local conn = pg.connect(p)
             >   return p
             > end
    ---
    ...
    tarantool> conn = pg_connect()
    ---
    ...

We will assume that the name is 'conn' in further examples.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        How to ping
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To ensure that a connection is working, the request is:

.. cssclass:: highlight
.. parsed-literal::

    *connection-name*:ping()


**Example:**

.. code-block:: tarantoolsession

    tarantool> conn:ping()
    ---
    - true
    ...

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Executing a statement
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For all PostgreSQL statements, the request is:

.. cssclass:: highlight
.. parsed-literal::

    *connection-name*:execute(*sql-statement* [, *parameters*])

where :code:`sql-statement` is a string, and the optional :code:`parameters`
are extra values that can be plugged in to replace any question marks ("?"s)
in the SQL statement.

**Example:**

.. code-block:: tarantoolsession

    tarantool> conn:execute('select tablename from pg_tables')
    ---
    - - table_name: ALL_PLUGINS
      - table_name: pg_statistics
      - table_name: pg_type
      <...>
    ...

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      Closing connection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To end a session that began with :code:`pg.connect`, the request is:

.. cssclass:: highlight
.. parsed-literal::

    *connection-name*:close()

**Example:**

    tarantool> conn:close()
    ---
    ...

For further information, including examples of rarely-used requests, see the
README.md file at `github.com/tarantool/pg`_.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
           Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example was run on an Ubuntu 12.04 ("precise") machine where tarantool had
been installed in a /usr subdirectory, and a copy of PostgreSQL had been installed
on /usr. The PostgreSQL server is already running on the local host 127.0.0.1.

.. code-block:: console


    $ # Check that the include subdirectory exists
    $ # by looking for /usr/include/postgresql/libpq-fe-h.
    $ [ -f /usr/include/postgresql/libpq-fe.h ] && echo "OK" || echo "Error"
    OK

    $ # Check that the library subdirectory exists and has the necessary .so file.
    $ [ -f /usr/lib/x86_64-linux-gnu/libpq.so ] && echo "OK" || echo "Error"
    OK

    $ # Check that the psql client can connect using some factory defaults:
    $ # port = 5432, user = 'postgres', user password = 'postgres',
    $ # database = 'postgres'. These can be changed, provided one changes
    $ # them in all places. Insert a row in database postgres, and quit.
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

    $ # Install luarocks
    $ sudo apt-get -y install luarocks | grep -E "Setting up|already"
    Setting up luarocks (2.0.8-2) ...

    $ # Set up the Tarantool rock list in ~/.luarocks,
    $ # following instructions at rocks.tarantool.org
    $ mkdir ~/.luarocks
    $ echo "rocks_servers = {[[http://rocks.tarantool.org/]]}" >> \
            ~/.luarocks/config.lua

    $ # Ensure that the next "install" will get files from Tarantool master
    $ # repository. The resultant display is normal for Ubuntu 12.04 precise
    $ cat /etc/apt/sources.list.d/tarantool.list
    deb http://tarantool.org/dist/master/ubuntu/ precise main
    deb-src http://tarantool.org/dist/master/ubuntu/ precise main

    $ # Install tarantool-dev. The displayed line should show version = 1.6
    $ sudo apt-get -y install tarantool-dev | grep -E "Setting up|already"
    Setting up tarantool-dev (1.6.6.222.g48b98bb~precise-1) ...
    $

    $ # Use luarocks to install locally, that is, relative to $HOME
    $ luarocks install pg POSTGRESQL_LIBDIR=/usr/lib/x86_64-linux-gnu --local
    Installing http://rocks.tarantool.org/pg-scm-1.rockspec...
    ... (more info about building the Tarantool/PostgreSQL driver appears here)
    pg scm-1 is now built and installed in ~/.luarocks/

    $ # Ensure driver.so now has been created in a place
    $ # tarantool will look at
    $ find ~/.luarocks -name "driver.so"
    ~/.luarocks/lib/lua/5.1/pg/driver.so

    $ # Change directory to a directory which can be used for
    $ # temporary tests. For this example we assume that the
    $ # name of this directory is /home/pgulutzan/tarantool_sandbox.
    $ # (Change "/home/pgulutzan" to whatever is the user's actual
    $ # home directory for the machine that's used for this test.)
    cd /home/pgulutzan/tarantool_sandbox

    $ # Start the Tarantool server. Do not use a Lua initialization file.

    $ tarantool
    tarantool: version 1.6.6-222-g48b98bb
    type 'help' for interactive help
    tarantool>

Configure tarantool and load pg module. Make sure that tarantool doesn't
reply "error" for the call to "require()".

.. code-block:: tarantoolsession

    tarantool> box.cfg{}
    ...
    tarantool> pg = require('pg')
    ---
    ...

Create a Lua function that will connect to the PostgreSQL server,
(using some factory default values for the port and user and password),
retrieve one row, and display the row.
For explanations of the statement types used here, read the
Lua tutorial earlier in the Tarantool user manual.

.. code-block:: tarantoolsession

    tarantool> function pg_select ()
             >   local conn = pg.connect({
             >     host = '127.0.0.1',
             >     port = 5432
             >     user = 'postgres'
             >     password = 'postgres'
             >     db = 'postgres'
             >   })
             >   local test = conn:execute('SELECT * FROM test WHERE s1 = 1')
             >   local row = ''
             >   for i, card in pairs(test) do
             >       row = row .. card.s2 .. ' '
             >       end
             >   conn:close()
             >   return row
             > end
    ---
    ...
    tarantool> pg_select()
    ---
    - 'PostgreSQL row '
    ...

Observe the result. It contains "PostgreSQL row". So this is the row that was
inserted into the PostgreSQL database. And now it's been selected with the
Tarantool client.

.. _gist.github.com/rtsisyk/aa95cf9ed9bbb538ff80: https://gist.github.com/rtsisyk/aa95cf9ed9bbb538ff80
.. _rocks.tarantool.org: http://rocks.tarantool.org/
.. _github.com/tarantool/mysql: https://github.com/tarantool/mysql
.. _dev.mysql.com/doc/refman/5.6/en/connecting.html: https://dev.mysql.com/doc/refman/5.6/en/connecting.html
.. _github.com/tarantool/mysql: https://github.com/tarantool/mysql
.. _github.com/tarantool/pg: https://github.com/tarantool/pg
