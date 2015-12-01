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

   * A C/C++ compiler. |br| Ordinarily the compiler is `gcc` and ``g++`` version 4.6 or later, on
     Mac OS X it should be ``Clang`` version 3.2 or later.

   * A program for managing the build process. |br| This is always ``CMake``
     for GNU/Linux and FreeBSD. The CMake version should be 2.8 or later.

   Here are names of tools and libraries which may have to be installed in advance,
   using ``sudo apt-get`` (for Ubuntu), ``sudo yum install`` (for CentOS), or the
   equivalent on other platforms. Different platforms may use slightly different
   names. Ignore the ones marked `optional, only in Mac OS scripts`
   unless the platform is Mac OS. Ignore the one marked `optional,
   only for documentation` unless the intent is to use the ``-DENABLE_DOC`` option in step 5.

   * **gcc and g++, or clang**                # see above
   * **git**                                  # see above
   * **cmake**                                # see above
   * **libreadline-dev or libreadline6-dev or readline-devel**  # for interactive mode
   * **autoconf**                             # optional, only in Mac OS scripts
   * **zlib1g** or **zlib**                   # optional, only in Mac OS scripts
   * **doxygen**                              # optional, only for documentation

2. Set up python modules for running the test suite or creating documentation.
   This step is optional. Python modules are not necessary for building Tarantool
   itself, unless one intends to use the ``-DENABLE_DOC`` option in step 5 or the
   "Run the test suite" option in step 7. Say: |br|
   ``python --version`` |br|
   You should see that the python version is greater than 2.6 --
   preferably 2.7 -- and less than 3.0.
   It may be necessary to install python first.

   On Ubuntu you can get modules from the repository:

   .. code-block:: bash

     # For both test suite and documentation
     sudo apt-get install python-pip python-dev python-yaml
     # For test suite
     sudo apt-get install python-daemon
     # For documentation
     sudo apt-get install python-sphinx python-pelican python-beautifulsoup

   On CentOS 6 too you can get modules from the repository:

   .. code-block:: bash

     sudo yum install python26 python26-PyYAML etc.

   If modules are not available on a repository,
   it is best to set up the modules by getting a tarball and
   doing the setup with ``python setup.py``, thus:

   .. code-block:: bash

     # On some machines this initial command may be necessary:
     # wget https://bootstrap.pypa.io/ez_setup.py -O - | sudo python

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

     # python module for HTML scraping: For documentation:
     cd ~
     wget http://www.crummy.com/software/BeautifulSoup/bs3/download//3.x/BeautifulSoup-3.2.1.tar.gz
     tar -xzvf BeautifulSoup-3.2.1.tar.gz
     cd BeautifulSoup-3.2.1
     sudo python setup.py install

   Finally, use Python :code:`pip` to bring in Python packages
   that may not be up-to-date in the distro repositories.
   (On CentOS 7 it will be necessary to install pip first,
   with :code:`sudo yum install epel-release` followed by
   :code:`sudo yum install python-pip`.)

   .. code-block:: bash

     # For test suite
     pip install tarantool\>0.4 --user
     # For documentation
     sudo pip install pelican
     sudo pip install breathe
     sudo pip install -U sphinx

3. Use :code:`git` to download the latest source code from the
   Tarantool 1.6 master branch on github.com. |br| |br|
   :code:`cd ~` |br|
   :code:`git clone https://github.com/tarantool/tarantool.git ~/tarantool`

4. Use ``git`` again so that third-party contributions will be seen as well.
   This step is only necessary once, the first time you do a download. There
   is an alternative -- say ``git clone --recursive`` in step 3 -- but we
   prefer this method because it works with older versions of ``git``.

   .. code-block:: bash

     cd ~/tarantool
     git submodule init
     git submodule update --recursive
     cd ../

   On rare occasions, the submodules will need to be updated again with the
   command: ``git submodule update --init --recursive``.

5. Use CMake to initiate the build.

   .. code-block:: bash

     cd ~/tarantool
     make clean         # unnecessary, added for good luck
     rm CMakeCache.txt  # unnecessary, added for good luck
     cmake .            # Start build with build type=Debug, no doc

   On some platforms it may be necessary to specify the C and C++ versions,
   for example |br|
   :code:`CC=gcc-4.8 CXX=g++-4.8 cmake .` |br|
   The option for specifying build type is :samp:`-DCMAKE_BUILD_TYPE={type}` where
   :samp:`{type} = Debug | Release | RelWithDebInfo` and a
   reasonable choice for production is ``-DCMAKE_BUILD_TYPE=RelWithDebInfo``
   (``Debug`` is a default used by project maintainers and ``Release`` is used
   when the highest performance is required).

   The option for asking to build documentation is :code:`-DENABLE_DOC=true|false`,
   which outputs HTML documentation (such as what you're reading now) to the
   subdirectory doc/www/output/doc. Tarantool uses the `Sphinx <http://sphinx-doc.org/>`_
   simplified markup system.
   Since most users do not need to rebuild the documentation,
   the reasonable option
   is ``-DENABLE_DOC=false`` or just don't use the ``-DENABLE_DOC`` clause at all.

6. Use make to complete the build.

   .. code-block:: bash

     make

   It's possible to say ``make install`` too, but that's not generally done.

7. Run the test suite. This step is optional. |br| Tarantool's developers always
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

8. Make an rpm. |br| This step is optional. It's only for people who want to
   redistribute Tarantool. Package maintainers who want to build with rpmbuild
   should consult the rpm-build instructions for the appropriate platform.

This is the end of the list of steps to take for source downloads.

For your added convenience, github.com has README files with example scripts: |br|
`README.CentOS <https://github.com/tarantool/tarantool/blob/master/README.CentOS>`_ for CentOS 5.8, |br|
`README.FreeBSD <https://github.com/tarantool/tarantool/blob/master/README.FreeBSD>`_ for FreeBSD 10.1, |br|
`README.MacOSX <https://github.com/tarantool/tarantool/blob/master/README.MacOSX>`_ for Mac OS X `Lion`, |br|
`README.md <https://github.com/tarantool/tarantool/blob/master/README.md>`_ for generic GNU/Linux. |br|
These example scripts assume that the intent is to download from the master
branch, build the server (but not the documentation), and run tests after build.

The python connector can be installed with ``sudo easy_install pip`` and ``sudo pip install tarantool``.

