=====================================================================
                            Perl
=====================================================================

The most commonly used Perl driver is `DR::Tarantool`_. It is not supplied as
part of the Tarantool repository; it must be installed separately. The most
common way to install it is with `CPAN, the Comprehensive Perl Archive Network`_.
`DR::Tarantool`_ requires other modules which should be installed first. For
example, on Ubuntu, the installation could look like this:

.. code-block:: console

    $ sudo cpan install AnyEvent
    $ sudo cpan install Devel::GlobalDestruction
    $ sudo cpan install Coro
    $ sudo cpan install Test::Pod
    $ sudo cpan install Test::Spelling
    $ sudo cpan install PAR::Dist
    $ sudo cpan install List::MoreUtils
    $ sudo cpan install DR::Tarantool

Here is a complete Perl program that inserts [99999,'BB'] into space[999] via
the Perl API. Before trying to run, check that the server is listening and
that :code:`examples` exists, as :ref:`described earlier <connector-setting>`. To run, paste the code into
a file named example.pl and say :code:`perl example.pl`. The program will connect
using an application-specific definition of the space. The program will open a
socket connection with the tarantool server at localhost:3301, then send an
INSERT request, then — if all is well — end without displaying any messages.
If tarantool is not running on localhost with listen address = 3301, the program
will print “Connection refused”.

.. code-block:: perl

    #!/usr/bin/perl
    use DR::Tarantool ':constant', 'tarantool';
    use DR::Tarantool ':all';
    use DR::Tarantool::MsgPack::SyncClient;

    my $tnt = DR::Tarantool::MsgPack::SyncClient->connect(
      host    => '127.0.0.1',                      # look for tarantool on localhost
      port    => 3301,                             # on port 3301
      user    => 'guest',                          # username. one could also say 'password=>...'

      spaces  => {
        999 => {                                   # definition of space[999] ...
          name => 'examples',                      #   space[999] name = 'examples'
          default_type => 'STR',                   #   space[999] field type is 'STR' if undefined
          fields => [ {                            #   definition of space[999].fields ...
              name => 'field1', type => 'NUM' } ], #     space[999].field[1] name='field1',type='NUM'
          indexes => {                             #   definition of space[999] indexes ...
            0 => {
              name => 'primary', fields => [ 'field1' ] } } } } );

    $tnt->insert('examples' => [ 99999, 'BB' ]);

The example program only shows one command and does not show all that's
necessary for good practice. For that, please see `DR::Tarantool`_ CPAN repository.

.. _DR::Tarantool: http://search.cpan.org/~unera/DR-Tarantool/
.. _CPAN, the Comprehensive Perl Archive Network: https://en.wikipedia.org/wiki/Cpan

