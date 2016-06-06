.. _building-documentation:

-------------------------------------------------------------------------------
Building documentation
-------------------------------------------------------------------------------

After building and testing your local instance of Tarantool, you can build a
local version of this documentation and contribute to it.

Documentation is based on the Python-based Sphinx generator. So, make sure
install all Python modules indicated in the :ref:`building-from-source` section
of this documentation. The procedure below implies that you already took those
steps and successfully tested your instance of Tarantool.

1. Build a local version of the existing documentation package.

Run the following set of commands (the example below is based on Ubuntu OS, but the 
procedure is similar for other supported platforms):

   .. code-block:: bash

    cd ~/tarantool cmake -DENABLE_DOC=TRUE make -C doc

Documentation is created and stored at ``doc/www/output``.

2. Set up a web-server.

Note that if your Tarantool Database runs on a Virtual machine, you need to make
sure that your host and client machines operate in the same network, i.e., to
configure port forwarding. If you use Oracle VM VirtualBox, follow the
guidelines below:

* To create a network, navigate to **Network > Advanced > Port Forwarding** in
  your VirtualBox instance menu.
  
* Enable the **Cable connected** checkbox. 

* Click the **Port Forwarding** button.

* Set Host and Guest Ports to ``8000``, Host IP to ``127.0.0.1`` and Guest
IP to ``10.0.2.15``. Make sure to check the IP of your VB instance, it must
be 10.0.2.15 (``ifconfig`` command)

* Save your settings.

If all the prerequisites are met, run the following command to set up a
web-server (the example below is based on Ubuntu, but the procedure is similar
for other supported OS's). Make sure to run it from the documentation output
folder, as specified below:

   .. code-block:: bash

     cd ~/tarantool/doc/www/output
     python -m SimpleHTTPServer 8000

3. Open your browser and enter ``127.0.0.1:8000`` into the address box. If your
local documentation build is valid, the HTML version will be displayed in the
browser.

To contribute to documentation, use the ``.rst`` format for drafting and submit
your updates as "Pull Requests" via GitHub.

To comply with the writing and formatting style, use guidelines provided in the
documentation, common sense and existing documents.

Note that if you suggest creating a new documentation section (i.e., a whole new
page), it has to be saved to the relevant section at GitHub.

* Root folder for documentation source files is located at
  https://github.com/tarantool/tarantool/tree/1.6/doc/sphinx.

* Source files for the developers' guide are located at
  https://github.com/tarantool/tarantool/tree/1.6/doc/sphinx/dev_guide.
