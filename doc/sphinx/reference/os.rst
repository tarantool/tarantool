.. _package-os:

-------------------------------------------------------------------------------
                            Package `os`
-------------------------------------------------------------------------------

.. module:: os

The os package contains the functions
:ref:`execute() <os-execute>`,
:ref:`rename() <os-rename>`,
:ref:`getenv() <os-getenv>`,
:ref:`remove() <os-remove>`,
:ref:`date() <os-date>`,
:ref:`exit() <os-exit>`,
:ref:`time() <os-time>`,
:ref:`clock() <os-clock>`,
:ref:`tmpname() <os-tmpname>`.
Most of these functions are described in the Lua manual
Chapter 22 `The Operating System Library <https://www.lua.org/pil/contents.html#22>`_.

.. _os-execute:

:codenormal:`os.`:codebold:`execute`:codenormal:`(`:codeitalic:`shell-command`:codenormal:`)`

Execute by passing to the shell.

Parameters: (string) shell-command = what to execute.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.execute('ls -l /usr')` |br|
:codenormal:`total 200` |br|
:codenormal:`drwxr-xr-x   2 root root 65536 Apr 22 15:49 bin` |br|
:codenormal:`drwxr-xr-x  59 root root 20480 Apr 18 07:58 include` |br|
:codenormal:`drwxr-xr-x 210 root root 65536 Apr 18 07:59 lib` |br|
:codenormal:`drwxr-xr-x  12 root root  4096 Apr 22 15:49 local` |br|
:codenormal:`drwxr-xr-x   2 root root 12288 Jan 31 09:50 sbin` |br|

.. _os-rename:

:codenormal:`os.`:codebold:`rename`:codenormal:`(`:codeitalic:`old-name,new-name`:codenormal:`)`

Rename a file or directory.

Parameters: (string) old-name = name of existing file or directory,
(string) new-name = changed name of file or directory.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.rename('local','foreign')` |br|
:codenormal:`---` |br|
:codenormal:`- null` |br|
:codenormal:`- 'local: No such file or directory'` |br|
:codenormal:`- 2` |br|
:codenormal:`...` |br|

.. _os-getenv:

:codenormal:`os.`:codebold:`getenv`:codenormal:`(`:codeitalic:`variable-name`:codenormal:`)`

Get environment variable.

Parameters: (string) variable-name = environment variable name.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.getenv('PATH')` |br|
:codenormal:`---` |br|
:codenormal:`- /usr/local/sbin:/usr/local/bin:/usr/sbin` |br|
:codenormal:`...` |br|

.. _os-remove:

:codenormal:`os.`:codebold:`remove`:codenormal:`(`:codeitalic:`name`:codenormal:`)`

Remove file or directory.

Parameters: (string) name = name of file or directory which will be removed.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.remove('file')` |br|
:codenormal:`---` |br|
:codenormal:`- true` |br|
:codenormal:`...` |br|

.. _os-date:

:codenormal:`os.`:codebold:`date`:codenormal:`(`:codeitalic:`format-string` :codenormal:`[,`:codeitalic:`time-since-epoch`:codenormal:`])`

Return a formatted date.

Parameters: (string) format-string = instructions; (string) time-since-epoch =
number of seconds since 1970-01-01. If time-since-epoch is omitted, it is assumed to be the current time.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.date("%A %B %d")` |br|
:codenormal:`---` |br|
:codenormal:`- Sunday April 24` |br|
:codenormal:`...`

.. _os-exit:

:codenormal:`os.`:codebold:`exit`:codenormal:`()`

Exit the program. If this is done on the server, then the server stops.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.exit()` |br|
:codenormal:`user@user-shell:~/tarantool_sandbox$``

.. _os-time:

:codenormal:`os.`:codebold:`time`:codenormal:`()`

Return the number of seconds since the epoch.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.time()` |br|
:codenormal:`---` |br|
:codenormal:`- 1461516945` |br|
:codenormal:`...` |br|

.. _os-clock:

:codenormal:`os.`:codebold:`clock`:codenormal:`()`

Return the number of CPU seconds since the program start.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.clock()` |br|
:codenormal:`---` |br|
:codenormal:`- 0.05` |br|
:codenormal:`...` |br|

.. _os-tmpname:

:codenormal:`os.`:codebold:`tmpname`:codenormal:`()`

Return a name for a temporary file.

**Example:** |br|
:codenormal:`tarantool>` :codebold:`os.tmpname()` |br|
:codenormal:`---` |br|
:codenormal:`- /tmp/lua_7SW1m2` |br|
:codenormal:`...` |br|


