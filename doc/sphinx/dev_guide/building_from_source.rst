.. _building-from-source:

-------------------------------------------------------------------------------
                             Building from source
-------------------------------------------------------------------------------

For downloading Tarantool source and building it, the platforms can differ and the
preferences can differ. But the steps are always the same. Here in the manual we'll
explain what the steps are, then on the Internet you can look at some example scripts.

1. Get tools and libraries that will be necessary for building
   and testing. The absolutely necessary ones are:

   * A program for downloading source repositories. |br| In this case the necessary program
     is ``git``. Although tarantool.org/dist has source tarballs (the files whose names
     end in `-src.tar.gz`), the latest complete source downloads are on github.com, and
     from github one gets with git.

   * A C/C++ compiler. |br| Ordinarily the compiler is ``GCC`` version 4.6 or later, on
     Mac OS X it should be ``Clang`` version 3.2 or later.

   * A program for managing the build process. |br| This is always ``CMake``
     for GNU/Linux and FreeBSD. The CMake version should be 2.8 or later.

   Here are names of tools and libraries which may have to be installed in advance,
   using ``sudo apt-get`` (for Ubuntu), ``sudo yum install`` (for CentOS), or the
   equivalent on other platforms. Different platforms may use slightly different
   names. Do not worry about the ones marked `optional, for build with -DENABLE_DOC`
   unless you intend to work on the documentation.

   * **binutils-dev** or **binutils-devel**   # contains GNU BFD for printing stack traces
   * **gcc or clang**                         # see above
   * **git**                                  # see above
   * **cmake**                                # see above
   * **libreadline-dev**                      # for interactive mode
   * **libncurses5-dev** or **ncurses-devel** # see above
   * **xsltproc**                             # optional, for build with -DENABLE_DOC
   * **lynx**                                 # optional, for build with -DENABLE_DOC
   * **jing**                                 # optional, for build with -DENABLE_DOC
   * **libxml2-utils**                        # optional, for build with -DENABLE_DOC
   * **docbook5-xml**                         # optional, for build with -DENABLE_DOC
   * **docbook-xsl-ns**                       # optional, for build with -DENABLE_DOC
   * **w3c-sgml-lib**                         # optional, for build with -DENABLE_DOC
   * **libsaxon-java**                        # optional, for build with -DENABLE_DOC
   * **libxml-commons-resolver1.1-java**      # optional, for build with -DENABLE_DOC
   * **libxerces2-java**                      # optional, for build with -DENABLE_DOC
   * **libxslthl-java**                       # optional, for build with -DENABLE_DOC
   * **autoconf**                             # optional, appears only in Mac OS scripts
   * **zlib1g** or **zlib**                   # optional, appears only in Mac OS scripts

2. Set up python modules for running the test suite or creating documentation.
   This step is optional. Python modules are not necessary for building Tarantool
   itself, unless one intends to use the ``-DENABLE_DOC`` option in step 6 or the
   "Run the test suite" option in step 8. Say:

   .. code-block:: bash

     python --version

   You should see that the python version is greater than 2.6 --
   preferably 2.7 -- and less than 3.0

   On Ubuntu you can get modules from the repository:

   .. code-block:: bash

     # For test suite
     sudo apt-get install python-daemon python-yaml python-argparse
     # For documentation
     sudo apt-get install python-jinja2 python-markdown


   On CentOS too you can get modules from the repository:

   .. code-block:: bash

     sudo yum install python26 python26-PyYAML python26-argparse

   But in general it is best to set up the modules by getting a tarball and
   doing the setup with ``python setup.py``, thus:

   .. code-block:: bash

     # python module for parsing YAML (pyYAML): For test suite:
     # (If wget fails, check the http://pyyaml.org/wiki/PyYAML
     # to see what the current version is.)
     cd ~
     wget http://pyyaml.org/download/pyyaml/PyYAML-3.10.tar.gz
     tar -xzf PyYAML-3.10.tar.gz
     cd PyYAML-3.10
     sudo python setup.py install

     # python module for helping programs become daemons (daemon):
     # For test suite: (if wget fails, check the
     # http://pypi.python.org/pypi/python-daemon
     # to see what the current version is.)
     cd ~
     wget http://pypi.python.org/packages/source/p/python-daemon/python-daemon-1.5.5.tar.gz
     tar -xzvf python-daemon-1.5.5.tar.gz
     cd python-daemon-1.5.5
     sudo python setup.py install

     # python module for text-to-html conversion (markdown):
     # For documentation: (If wget fails, check the
     # http://pypi.python.org/pypi/Markdown/
     # to see what the current version is.)
     cd ~
     wget https://pypi.python.org/packages/source/M/Markdown/Markdown-2.3.1.tar.gz
     tar -xzvf Markdown-2.3.1.tar.gz
     cd Markdown-2.3.1
     sudo python setup.py install

     # python module which includes Jinja2 template engine:
     # For documentation:
     sudo pip install pelican

     # python module for HTML scraping: For documentation:
     cd ~
     wget http://www.crummy.com/software/BeautifulSoup/bs3/download//3.x/BeautifulSoup-3.2.1.tar.gz
     tar -xzvf BeautifulSoup-3.2.1.tar.gz
     cd BeautifulSoup-3.2.1
     sudo python setup.py install

5. Use ``git`` again so that third-party contributions will be seen as well.
   This step is only necessary once, the first time you do a download. There
   is an alternative -- say ``git clone --recursive`` in step 3 -- but we
   prefer this method because it works with older versions of ``git``.

   .. code-block:: bash

     cd ~/tarantool
     git submodule init
     git submodule update
     cd ../

   On rare occasions, the submodules will need to be updated again with the
   command: ``git submodule update --init``.

6. Use CMake to initiate the build.

   .. code-block: bash

     cd ~/tarantool
     make clean         # unnecessary, added for good luck
     rm CMakeCache.txt  # unnecessary, added for good luck
     cmake .            # Start build with build type=Debug, no doc

   The option for specifying build type is ``-DCMAKE_BUILD_TYPE=<type>`` where
   ``type = <None | Debug | Release | RelWithDebInfo | MinSizeRel>`` and a
   reasonable choice for production is ``-DCMAKE_BUILD_TYPE=RelWithDebInfo``
   (``Debug`` is used only by project maintainers and ``Release`` is used only
   when the highest performance is required).

   The option for asking to build documentation is ``-DENABLE_DOC=<true|false>``
   and the assumption is that only a minority will need to rebuild the
   documentation (such as what you're reading now), so details about
   documentation are in the developer manual, and the reasonable choice
   is ``-DENABLE_DOC=false`` or just don't use the ``-DENABLE_DOC`` clause at all.

7. Use make to complete the build.

   .. code-block:: bash

     make

   It's possible to say ``make install`` too, but that's not generally done.

8. Run the test suite. This step is optional. |br| Tarantool's developers always
   run the test suite before they publish new versions. You should run the test
   suite too, if you make any changes in the code. Assuming you downloaded to
   ``~/tarantool``, the principal steps are:

   .. code-block:: bash

     # make a subdirectory named `bin`
     mkdir ~/tarantool/bin
     # link python to bin (this may require superuser privilege)
     ln /usr/bin/python ~/tarantool/bin/python
     # get on the test subdirectory
     cd ~/tarantool/test
     # run tests using python
     PATH=~/tarantool/bin:$PATH ./test-run.py


   The output should contain reassuring reports, for example:

   .. code-block:: bash

     ======================================================================
     TEST                                            RESULT
     ------------------------------------------------------------
     box/bad_trigger.test.py                         [ pass ]
     box/call.test.py                                [ pass ]
     box/iproto.test.py                              [ pass ]
     box/xlog.test.py                                [ pass ]
     box/admin.test.lua                              [ pass ]
     box/auth_access.test.lua                        [ pass ]
     ... etc.

   There are more than 70 tests in the suite.

   To prevent later confusion, clean up what's in the `bin`
   subdirectory:

   .. code-block:: bash

     rm ~/tarantool/bin/python
     rmdir ~/tarantool/bin


9. Make an rpm. |br| This step is optional. It's only for people who want to
   redistribute Tarantool. Package maintainers who want to build with rpmbuild
   should consult the
   :doc:`Tarantool Developer Guide <index>`

This is the end of the list of steps to take for source downloads.

For your added convenience, github.com has README files with example scripts:

* `README.CentOS <https://github.com/tarantool/tarantool/blob/master/README.CentOS>`_
  for CentOS 5.8,
* `README.FreeBSD <https://github.com/tarantool/tarantool/blob/master/README.FreeBSD>`_
  for FreeBSD 8.3,
* `README.MacOSX <https://github.com/tarantool/tarantool/blob/master/README.MacOSX>`_
  for Mac OS X `Lion`,
* `README.md <https://github.com/tarantool/tarantool/blob/master/README.md>`_
  for generic GNU/Linux.

These example scripts assume that the intent is to download from the master
branch, build the server (but not the documentation), and run tests after build.

To build with SUSE 13.1, the steps are as described above, except that the
appropriate YaST2 package names are: binutils-devel, cmake, ncurses-devel,
lynx, jing, libxml2-devel, docbook_5, saxon, libxslt-devel. |br|
The python connector can be installed with ``sudo easy_install pip`` and ``sudo pip install tarantool``.

===========================================================
                How to build the XML manual
===========================================================

To build XML manual, you'll need:

* xsltproc
* docbook5-xml
* docbook-xsl-ns
* w3c-sgml-lib
* libsaxon-java (for saxon processing)
* libxml-commons-resolver1.1-java
* libxml2-utils
* libxerces2-java
* libxslthl-java
* lynx
* jing

When all pre-requisites are met, you should run:

.. code-block:: bash

    $ cmake . -DENABLE_DOC=YES

to enable documentation builder.

If you want to make tarantool user guide, you should run the
following command from tarantool root directory:

.. code-block:: bash

    $ make html

or

.. code-block:: bash

    $ cd doc/user
    $ make

The html version of the user guide will be genreated in doc/www/content/doc
