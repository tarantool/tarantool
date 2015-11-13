=====================================================================
                            PHP
=====================================================================

The PHP driver is `tarantool-php`_. It is not supplied as part of the Tarantool
repository; it must be installed separately. It can be installed with git. It
requires other modules which should be installed first. For example, on Ubuntu,
the installation could look like this:

.. code-block:: console

    $ sudo apt-get install php5-cli
    $ sudo apt-get install php5-dev
    $ sudo apt-get install php-pear
    $ cd ~
    $ git clone https://github.com/tarantool/tarantool-php.git
    $ cd tarantool-php
    $ phpize
    $ ./configure
    $ make
    $ # make install is optional


At this point there is a file named :code:`~/tarantool-php/modules/tarantool.so`.
PHP will only find it if the PHP initialization file :code:`php.ini` contains a
line like :code:`extension=./tarantool.so`, or if PHP is started with the option
:code:`-d extension=~/tarantool-php/modules/tarantool.so`.

Here is a complete PHP program that inserts [99999,'BB'] into a space named 'examples'
via the PHP API. Before trying to run, check that the server is listening and that
:code:`examples` exists, as :ref:`described earlier <connector-setting>`. To run, paste the code into a file named
example.php and say :code:`php -d extension=~/tarantool-php/modules/tarantool.so example.php`. The program will open a socket connection with
the tarantool server at localhost:3301, then send an INSERT request, then — if all is
well — print "Insert succeeded". If the tuple already exists, the program will print
“Duplicate key exists in unique index 'primary' in space 'examples'”.

.. code-block:: php

    <?php
    $tarantool = new Tarantool("localhost", 3301);
    try {
      $tarantool->insert("examples", array(99999, "BB"));
      print "Insert succeeded\n";
      }
    catch (Exception $e) {
      echo "Exception: ", $e->getMessage(), "\n";
      }
    ?>

The example program only shows one command and does not show all that's necessary
for good practice. For that, please see `tarantool-php`_ project at GitHub.

.. _tarantool-php: https://github.com/tarantool/tarantool-php
