-------------------------------------------------------------------------------
                        Getting started
-------------------------------------------------------------------------------


This chapter shows how to download, how to install, and how to start Tarantool
for the first time.

For production, if possible, you should download a binary (executable) package.
This will ensure that you have the same build of the same version that the
developers have. That makes analysis easier if later you need to report a problem,
and avoids subtle problems that might happen if you used different tools or
different parameters when building from source. The section about binaries is
“`Downloading and installing a binary package`_”.

For development, you will want to download a source package and make the binary
by yourself using a C/C++ compiler and common tools. Although this is a bit harder,
it gives more control. And the source packages include additional files, for example
the Tarantool test suite. The section about source is “:ref:`building-from-source`”.

If the installation has already been done, then you should try it out. So we've
provided some instructions that you can use to make a temporary “sandbox”. In a
few minutes you can start the server and type in some database-manipulation
statements. The section about sandbox is “`Starting Tarantool and making your first database`_”.

.. _downloading-and-installing-a-binary-package:

=====================================================================
            Downloading and installing a binary package
=====================================================================

The repositories for the “stable” release are at `tarantool.org/dist/stable`_.
The repositories for the “master” release are at `tarantool.org/dist/master`_.
Since this is the manual for the “master” release, all instructions use
`tarantool.org/dist/master`_.

An automatic build system creates, tests and publishes packages for every
push into the master branch. Therefore if you looked at
`tarantool.org/dist/master`_ you would see that there are source files and
subdirectories for the packages that will be described in this section.

To download and install the package that's appropriate for your environment,
start a shell (terminal) and enter one of the following sets of command-line
instructions.

More advice for binary downloads is at http://tarantool.org/download.html.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                    Debian GNU/Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There is always an up-to-date Debian repository at
http://tarantool.org/dist/master/debian. The repository contains builds for
Debian unstable "Sid", stable "Wheezy", forthcoming "Jessie". Add the
tarantool.org repository to your apt sources list. $release is an environment
variable which will contain the Debian version code e.g. "Wheezy":

.. code-block:: bash

    wget http://tarantool.org/dist/public.key
    sudo apt-key add ./public.key
    release=`lsb_release -c -s`
    # append two lines to a list of source repositories
    echo "deb http://tarantool.org/dist/master/debian/ $release main" | \
    sudo tee -a /etc/apt/sources.list.d/tarantool.list
    echo "deb-src http://tarantool.org/dist/master/debian/ $release main" | \
    sudo tee -a /etc/apt/sources.list.d/tarantool.list
    # install
    sudo apt-get update
    sudo apt-get install tarantool

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                        Ubuntu
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There is always an up-to-date Ubuntu repository at
http://tarantool.org/dist/master/ubuntu. The repository contains builds for
Ubuntu 12.04 "precise", 13.10 "saucy", and 14.04 "trusty". Add the tarantool.org
repository to your apt sources list. $release is an environment variable which
will contain the Ubuntu version code e.g. "precise". If you want the version
that comes with Ubuntu, start with the lines that follow the '# install' comment:

.. code-block:: bash

    cd ~
    wget http://tarantool.org/dist/public.key
    sudo apt-key add ./public.key
    release=`lsb_release -c -s`
    # append two lines to a list of source repositories
    echo "deb http://tarantool.org/dist/master/ubuntu/ $release main" | \
    sudo tee -a /etc/apt/sources.list.d/tarantool.list
    echo "deb-src http://tarantool.org/dist/master/ubuntu/ $release main" | \
    sudo tee -a /etc/apt/sources.list.d/tarantool.list
    # install
    sudo apt-get update
    sudo apt-get install tarantool


~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                        CentOS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These instructions are applicable for CentOS version 6 or 7, and RHEL version
6 or 7. Pick the CentOS repository which fits your CentOS/RHEL version and
your x86 platform:

* http://tarantool.org/dist/master/centos/6/os/i386 for version 6, x86-32
* http://tarantool.org/dist/master/centos/6/os/x86_64 for version 6, x86-64
* http://tarantool.org/dist/master/centos/7/os/x86_64 for version 7, x86-64

Add the following section to your yum repository list
(``/etc/yum.repos.d/tarantool.repo``) (in these instructions ``$releasever``
i.e. CentOS release version must be either 6 or 7 and ``$basearch`` i.e. base
architecture must be either i386 or x86_64):

.. cssclass:: highlight
.. parsed-literal::

    # [tarantool]
    name=CentOS-$releasever - Tarantool
    baseurl=http://tarantool.org/dist/master/centos/*$releasever*/os/*$basearch*/
    enabled=1
    gpgcheck=0

For example, if you have CentOS version 6 and x86-64, you can add the new section thus:

.. code-block:: bash

    echo "[tarantool]" | \
    sudo tee /etc/yum.repos.d/tarantool.repo
    echo "name=CentOS-6 - Tarantool"| sudo tee -a /etc/yum.repos.d/tarantool.repo
    echo "baseurl=http://tarantool.org/dist/master/centos/6/os/x86_64/" | \
    sudo tee -a /etc/yum.repos.d/tarantool.repo
    echo "enabled=1" | sudo tee -a /etc/yum.repos.d/tarantool.repo
    echo "gpgcheck=0" | sudo tee -a /etc/yum.repos.d/tarantool.repo

Then install with :code:`sudo yum install tarantool`.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                          Fedora
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These instructions are applicable for Fedora 19, 20 or rawhide. Pick the Fedora
repository, for example http://tarantool.org/dist/master/fedora/20/x86_64 for
version 20, x86-64. Add the following section to your yum repository list
(``/etc/yum.repos.d/tarantool.repo``) (in these instructions
``$releasever`` i.e. Fedora release version must be 19, 20 or rawhide and
``$basearch`` i.e. base architecture must be x86_64):

.. cssclass:: highlight
.. parsed-literal::

    [tarantool]
    name=Fedora-$releasever - Tarantool
    baseurl=http://tarantool.org/dist/master/fedora/*$releasever*/*$basearch*/
    enabled=1
    gpgcheck=0

For example, if you have Fedora version 20, you can add the new section thus:

.. code-block:: bash

    echo "[tarantool]" | \
    sudo tee /etc/yum.repos.d/tarantool.repo
    echo "name=Fedora-20 - Tarantool"| sudo tee -a /etc/yum.repos.d/tarantool.repo
    echo "baseurl=http://tarantool.org/dist/master/fedora/20/x86_64/" | \
    sudo tee -a /etc/yum.repos.d/tarantool.repo
    echo "enabled=1" | sudo tee -a /etc/yum.repos.d/tarantool.repo
    echo "gpgcheck=0" | sudo tee -a /etc/yum.repos.d/tarantool.repo

Then install with :code:`sudo yum install tarantool`.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                          Gentoo
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There is a tarantool portage overlay. Use layman to add the overlay to your system:

.. code-block:: bash

    layman -S
    layman -a tarantool
    emerge dev-db/tarantool -av

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                         FreeBSD
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

With your browser go to the FreeBSD ports page
http://www.freebsd.org/ports/index.html. Enter the search term: tarantool.
Choose the package you want.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                         Mac OS X
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is actually a “homebrew” recipe so it's not a true binary download,
some source code is involved. First upgrade Clang (the C compiler) to version 3.2
or later using Command Line Tools for Xcode disk image version 4.6+ from Apple
Developer web-site. Then download the recipe file from
`tarantool.org/dist/master/tarantool.rb`_. Make the file executable, execute it,
and the script in the file should handle the necessary steps with cmake, make,
and make install.

.. _tarantool.org/dist/master/tarantool.rb: http://tarantool.org/dist/master/tarantool.rb

.. _dup first database:

=====================================================================
        Starting Tarantool and making your first database
=====================================================================

Here is how to create a simple test database after installing.

1. Create a new directory. It's just for tests, you can delete it when the tests are over.

   .. code-block:: bash

       mkdir ~/tarantool_sandbox
       cd ~/tarantool_sandbox

2. Start the server. The server name is tarantool.

   .. code-block:: bash

       # if you downloaded a binary with apt-get or yum, say this:
       /usr/bin/tarantool
       # if you downloaded and untarred a binary tarball to ~/tarantool, say this:
       ~/tarantool/bin/tarantool
       # if you built from a source download, say this:
       ~/tarantool/src/tarantool

   The server starts in interactive mode and outputs a command prompt.
   To turn on the database, :mod:`configure <box.cfg>` it:

   .. code-block:: tarantoolsession

      tarantool> box.cfg{listen = 3301}

   (this minimal example is sufficient).

   If all goes well, you will see the server displaying progress as it
   initializes, something like this:

   .. code-block:: tarantoolsession

       tarantool> box.cfg{listen = 3301}
       2014-08-07 09:41:41.077 ... version 1.6.3-439-g7e1011b
       2014-08-07 09:41:41.077 ... log level 5
       2014-08-07 09:41:41.078 ... mapping 1073741824 bytes for a shared arena...
       2014-08-07 09:41:41.079 ... initialized
       2014-08-07 09:41:41.081 ... initializing an empty data directory
       2014-08-07 09:41:41.095 ... creating './00000000000000000000.snap.inprogress'
       2014-08-07 09:41:41.095 ... saving snapshot './00000000000000000000.snap.inprogress'
       2014-08-07 09:41:41.127 ... done
       2014-08-07 09:41:41.128 ... primary: bound to 0.0.0.0:3301
       2014-08-07 09:41:41.128 ... ready to accept requests

   Now that the server is up, you could start up a different shell
   and connect to its primary port with

   .. code-block:: bash

       telnet 0 3301

   but for example purposes it is simpler to just leave the server
   running in "interactive mode". On production machines the
   interactive mode is just for administrators, but because it's
   convenient for learning it will be used for most examples in
   this manual. Tarantool is waiting for the user to type instructions.

   To create the first space and the first :ref:`index <box.index>`, try this:

   .. code-block:: tarantoolsession

       tarantool> s = box.schema.space.create('tester')
       tarantool> i = s:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})

   To insert three “tuples” (our name for “records”) into the first “space” of the database try this:

   .. code-block:: tarantoolsession

       tarantool> t = s:insert({1})
       tarantool> t = s:insert({2, 'Music'})
       tarantool> t = s:insert({3, 'Length', 93})

   To select a tuple from the first space of the database, using the first defined key, try this:

   .. code-block:: tarantoolsession

       tarantool> s:select{3}

   Your terminal screen should now look like this:

   .. code-block:: tarantoolsession

       tarantool> s = box.schema.space.create('tester')
       2014-06-10 12:04:18.158 ... creating './00000000000000000002.xlog.inprogress'
       ---
       ...
       tarantool> s:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})
       ---
       ...
       tarantool> t = s:insert{1}
       ---
       ...
       tarantool> t = s:insert{2, 'Music'}
       ---
       ...
       tarantool> t = s:insert{3, 'Length', 93}
       ---
       ...
       tarantool> s:select{3}
       ---
       - - [3, 'Length', 93]
       ...
       tarantool> 

   Now, to prepare for the example in the next section, try this:

   .. code-block:: tarantoolsession

       tarantool> box.schema.user.grant('guest','read,write,execute','universe')

.. _tarantool.org/dist/stable: http://tarantool.org/dist/stable
.. _tarantool.org/dist/master: http://tarantool.org/dist/master

=====================================================================
        Starting another Tarantool instance and connecting remotely
=====================================================================

In the previous section the first request was with ``box.cfg{listen = 3301}``.
The "listen" value can be any form of URI (uniform resource identifier);
in this case it's just a local port: port 3301.
It's possible to send requests to the listen URI via (a) telnet,
(b) a connector (which will be the subject of Chapter 8),
or (c) another instance of Tarantool. Let's try (c).

1. Switch to another terminal.
On Linux, for example, this means starting another instance of a Bash shell.
There is no need to use cd to switch to the ~/tarantool_sandbox directory.

2. Start the second instance of Tarantool. The server name is tarantool.

    .. code-block:: bash

        # if you downloaded a binary with apt-get or yum, say this:
        /usr/bin/tarantool
        # if you downloaded and untarred a binary tarball to ~/tarantool, say this:
        ~/tarantool/bin/tarantool
        # if you built from a source download, say this:
        ~/tarantool/src/tarantool

3. Try these requests:

    .. code-block:: lua

        console = require('console')
        console.connect('localhost:3301')
        box.space.tester:select{2}

The requests are saying "use the :ref:`console package <package-console>`
to connect to the Tarantool server that's listening on ``localhost:3301``, send
a request to that server, and display the result." The result in this case is
one of the tuples that was inserted earlier. Your terminal screen should now
look like this:

.. code-block:: lua

   <... ...>
   tarantool> console = require('console')
   ---
   ...
   tarantool> console.connect('localhost:3301')
   <...> [32628] main/101/interactive I> connected to localhost:3301
   ---
   ...
   localhost:3301> box.space.tester:select{2}
   ---
   - - [2, 'Music']
   ...
   localhost:3301> 

You can repeat box.space...:insert{} and box.space...:select{}
indefinitely, on either Tarantool instance.
When the testing is over: To drop the space: s:drop().
To stop tarantool: Ctrl+C. To stop tarantool (an alternative):
os.exit(). To stop tarantool (from another terminal):
sudo pkill -f tarantool.
To destroy the test: rm -r ~/tarantool_sandbox.

To review ... If you followed all the instructions
in this chapter, then so far you have: installed Tarantool
from either a binary or a source repository,
started up the Tarantool server, inserted and selected tuples.
