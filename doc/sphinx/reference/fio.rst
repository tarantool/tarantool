-------------------------------------------------------------------------------
                                   Package `fio`
-------------------------------------------------------------------------------

.. _fio-section:

Tarantool supports file input/output with an API that is similar to POSIX
syscalls. All operations are performed asynchronously. Multiple fibers can
access the same file simultaneously.

.. module:: fio

=================================================
         Common pathname manipulations
=================================================

.. function:: pathjoin(partial-string [, partial-string ...])

    Concatenate partial string, separated by '/' to form a path name.

    :param string partial-string: one or more strings to be concatenated.
    :return: path name
    :rtype:  string

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.pathjoin('/etc', 'default', 'myfile')`
    | :codenormal:`---`
    | :codenormal:`- /etc/default/myfile`
    | :codenormal:`...`

.. function:: basename(path-name[, suffix])

    Given a full path name, remove all but the final part (the file name).
    Also remove the suffix, if it is passed.

    :param string path-name: path name
    :param string suffix: suffix

    :return: file name
    :rtype:  string

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.basename('/path/to/my.lua', '.lua')`
    | :codenormal:`---`
    | :codenormal:`- my`
    | :codenormal:`...`

.. function:: dirname(path-name)

    Given a full path name, remove the final part (the file name).

    :param string path-name: path name

    :return: directory name, that is, path name except for file name.
    :rtype:  string

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.dirname('/path/to/my.lua')`
    | :codenormal:`---`
    | :codenormal:`- /path/to/`
    | :codenormal:`...`

=================================================
            Common file manipulations
=================================================

.. function:: umask(mask-bits)

    Set the mask bits used when creating files or directories. For a detailed
    description type "man 2 umask".

    :param number mask-bits: mask bits.
    :return: previous mask bits.
    :rtype:  number

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.umask(tonumber('755', 8)) -- pass 755 octal`
    | :codenormal:`---`
    | :codenormal:`- 493`
    | :codenormal:`...`

.. function:: lstat(path-name)
               stat(path-name)

    Returns information about a file object. For details type "man 2 lstat" or
    "man 2 stat".

    :param string path-name: path name of file.
    :return: fields which describe the file's block size, creation time, size,
             and other attributes.
    :rtype:  table

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.lstat('/etc')`
    | :codenormal:`---`
    | :codenormal:`- inode: 1048577`
    | |nbsp| |nbsp| :codenormal:`rdev: 0`
    | |nbsp| |nbsp| :codenormal:`size: 12288`
    | |nbsp| |nbsp| :codenormal:`atime: 1421340698`
    | |nbsp| |nbsp| :codenormal:`mode: 16877`
    | |nbsp| |nbsp| :codenormal:`mtime: 1424615337`
    | |nbsp| |nbsp| :codenormal:`nlink: 160`
    | |nbsp| |nbsp| :codenormal:`uid: 0`
    | |nbsp| |nbsp| :codenormal:`blksize: 4096`
    | |nbsp| |nbsp| :codenormal:`gid: 0`
    | |nbsp| |nbsp| :codenormal:`ctime: 1424615337`
    | |nbsp| |nbsp| :codenormal:`dev: 2049`
    | |nbsp| |nbsp| :codenormal:`blocks: 24`
    | :codenormal:`...`

.. function:: mkdir(path-name)
              rmdir(path-name)

    Create or delete a directory. For details type
    "man 2 mkdir" or "man 2 rmdir".

    :param string path-name: path of directory.
    :return: true if success, false if failure.
    :rtype:  boolean

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.mkdir('/etc')`
    | :codenormal:`---`
    | :codenormal:`- false`
    | :codenormal:`...`


.. function:: glob(path-name)

    Return a list of files that match an input string. The list is constructed
    with a single flag that controls the behavior of the function: GLOB_NOESCAPE.
    For details type "man 3 glob".

    :param string path-name: path-name, which may contain wildcard characters.
    :return: list of files whose names match the input string
    :rtype:  table

    Possible errors: nil.

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.glob('/etc/x*')`
    | :codenormal:`---`
    | :codenormal:`- - /etc/xdg`
    | |nbsp| |nbsp| :codenormal:`- /etc/xml`
    | |nbsp| |nbsp| :codenormal:`- /etc/xul-ext`
    | :codenormal:`...`


.. function:: tempdir()

    Return the name of a directory that can be used to store temporary files.

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.tempdir()`
    | :codenormal:`---`
    | :codenormal:`- /tmp/lG31e7`
    | :codenormal:`...`


.. function:: cwd()

    Return the name of the current working directory.

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.cwd()`
    | :codenormal:`---`
    | :codenormal:`- /home/username/tarantool_sandbox`
    | :codenormal:`...`


.. function:: link     (src , dst)
              symlink  (src , dst)
              readlink (src)
              unlink   (src)

    Functions to create and delete links. For details type "man readlink",
    "man 2 link", "man 2 symlink", "man 2 unlink"..

    :param string src: existing file name.
    :param string dst: linked name.

    :return: ``fio.link`` and ``fio.symlink`` and ``fio.unlink`` return true if
             success, false if failure. ``fio.readlink`` returns the link value
             if success, nil if failure.

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.link('/home/username/tmp.txt', '/home/username/tmp.txt2')`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`fio.unlink('/home/pgulutzan/tmp.txt2')`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`

.. function:: rename(path-name, new-path-name)

    Rename a file or directory. For details type "man 2 rename".

    :param string     path-name: original name.
    :param string new-path-name: new name.

    :return: true if success, false if failure.
    :rtype:  boolean

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.rename('/home/username/tmp.txt', '/home/username/tmp.txt2')`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`

.. function:: chown(path-name, owner-user, owner-group)
              chmod(path-name, new-rights)

    Manage the rights to file objects, or ownership of file objects.
    For details type "man 2 chown" or "man 2 chmod".

    :param string owner-user: new user uid.
    :param string owner-group: new group uid.
    :param number new-rights: new permissions

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.chmod('/home/username/tmp.txt', tonumber('0755', 8))`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`fio.chown('/home/username/tmp.txt', 'username', 'username')`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`

.. function:: truncate(path-name, new-size)

    Reduce file size to a specified value. For details type "man 2 truncate".

    :param string path-name:
    :param number new-size:

    :return: true if success, false if failure.
    :rtype:  boolean

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.truncate('/home/username/tmp.txt', 99999)`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`

.. function:: sync()

    Ensure that changes are written to disk. For details type "man 2 sync".

    :return: true if success, false if failure.
    :rtype:  boolean

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fio.sync()`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`

.. function:: open(path-name [, flags])

    Open a file in preparation for reading or writing or seeking.

    :param string path-name:
    :param number flags: Flags can be passed as a number or as string
                         constants, for example '``O_RDONLY``',
                         '``O_WRONLY``', '``O_RDWR``'. Flags can be
                         combined by enclosing them in braces.
    :return: file handle (later - fh)
    :rtype:  userdata

    Possible errors: nil.

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fh = fio.open('/home/username/tmp.txt', {'O_RDWR', 'O_APPEND'})`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`fh -- display file handle returned by fio.open`
    | :codenormal:`---`
    | :codenormal:`- fh: 11`
    | :codenormal:`...`

.. class:: file-handle

    .. method:: close()

        Close a file that was opened with ``fio.open``. For details type "man 2 close".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: true if success, false on failure.
        :rtype:  boolean

        | EXAMPLE
        | :codenormal:`tarantool>` :codebold:`fh:close() -- where fh = file-handle`
        | :codenormal:`---`
        | :codenormal:`- true`
        | :codenormal:`...`

    .. method:: pread(count, offset)
                pwrite(new-string, offset)

        Perform read/write random-access operation on a file, without affecting
        the current seek position of the file.
        For details type "man 2 pread" or "man 2 pwrite".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :param number count: number of bytes to read
        :param string new-string: value to write
        :param number offset: offset within file where reading or writing begins
        :return: ``fh:pwrite`` returns true if success, false if failure.
                 ``fh:pread`` returns the data that was read, or nil if failure.

        | EXAMPLE
        | :codenormal:`tarantool>` :codebold:`fh:pread(25, 25)`
        | :codenormal:`---`
        | :codenormal:`- |-`
        | |nbsp| |nbsp| :codenormal:`elete from t8//`
        | |nbsp| |nbsp| :codenormal:`insert in`
        | :codenormal:`...`

    .. method:: read(count)
                write(new-string)

        Perform non-random-access read or write on a file. For details type
        "man 2 read" or "man 2 write".

        .. NOTE::

            ``fh:read`` and ``fh:write`` affect the seek position within the
            file, and this must be taken into account when working on the same
            file from multiple fibers. It is possible to limit or prevent file
            access from other fibers with ``fiber.ipc``.

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :param number count: number of bytes to read
        :param string new-string: value to write
        :return: ``fh:write`` returns true if success, false if failure.
                 ``fh:read`` returns the data that was read, or nil if failure.

        | EXAMPLE
        | :codenormal:`tarantool>` :codebold:`fh:write('new data')`
        | :codenormal:`---`
        | :codenormal:`- true`
        | :codenormal:`...`

    .. method:: truncate(new-size)

        Change the size of an open file. Differs from ``fio.truncate``, which
        changes the size of a closed file.

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: true if success, false if failure.
        :rtype:  boolean

        | EXAMPLE
        | :codenormal:`tarantool>` :codebold:`fh:truncate(0)`
        | :codenormal:`---`
        | :codenormal:`- true`
        | :codenormal:`...`

    .. method:: seek(position [, offset-from])

        Shift position in the file to the specified position. For details type
        "man 2 seek".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :param number position: position to seek to
        :param string offset-from: '``SEEK_END``' = end of file, '``SEEK_CUR``'
                    = current position, '``SEEK_SET``' = start of file.
        :return: the new position if success
        :rtype:  number

        Possible errors: nil.

        | EXAMPLE
        | :codenormal:`tarantool>` :codebold:`fh:seek(20, 'SEEK_SET')`
        | :codenormal:`---`
        | :codenormal:`- 20`
        | :codenormal:`...`


    .. method:: stat()

        Return statistics about an open file. This differs from ``fio.stat``
        which return statistics about a closed file. For details type "man 2 stat".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: details about the file.
        :rtype:  table

        | EXAMPLE
        | :codenormal:`tarantool>` :codebold:`fh:stat()`
        | :codenormal:`---`
        | :codenormal:`- inode: 729866`
        | |nbsp| |nbsp| :codenormal:`rdev: 0`
        | |nbsp| |nbsp| :codenormal:`size: 100`
        | |nbsp| |nbsp| :codenormal:`atime: 1409429855`
        | |nbsp| |nbsp| :codenormal:`mode: 33261`
        | |nbsp| |nbsp| :codenormal:`mtime: 1409430660`
        | |nbsp| |nbsp| :codenormal:`nlink: 1`
        | |nbsp| |nbsp| :codenormal:`uid: 1000`
        | |nbsp| |nbsp| :codenormal:`blksize: 4096`
        | |nbsp| |nbsp| :codenormal:`gid: 1000`
        | |nbsp| |nbsp| :codenormal:`ctime: 1409430660`
        | |nbsp| |nbsp| :codenormal:`dev: 2049`
        | |nbsp| |nbsp| :codenormal:`blocks: 8`
        | :codenormal:`...`

    .. method:: fsync()
                fdatasync()

        Ensure that file changes are written to disk, for an open file.
        Compare ``fio.sync``, which is for all files. For details type
        "man 2 fsync" or "man 2 fdatasync".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: true if success, false if failure.

        | EXAMPLE
        | :codenormal:`tarantool>` :codebold:`fh:fsync()`
        | :codenormal:`---`
        | :codenormal:`- true`
        | :codenormal:`...`

