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

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.pathjoin('/etc', 'default', 'myfile')
        ---
        - /etc/default/myfile
        ...

.. function:: basename(path-name[, suffix])

    Given a full path name, remove all but the final part (the file name).
    Also remove the suffix, if it is passed.

    :param string path-name: path name
    :param string suffix: suffix

    :return: file name
    :rtype:  string

    **Example:**

     .. code-block:: tarantoolsession

        tarantool> fio.basename('/path/to/my.lua', '.lua')
        ---
        - my
        ...

.. function:: dirname(path-name)

    Given a full path name, remove the final part (the file name).

    :param string path-name: path name

    :return: directory name, that is, path name except for file name.
    :rtype:  string

    **Example:**

     .. code-block:: tarantoolsession

        tarantool> fio.dirname('path/to/my.lua')
        ---
        - 'path/to/'
        ...

=================================================
            Common file manipulations
=================================================

.. function:: umask(mask-bits)

    Set the mask bits used when creating files or directories. For a detailed
    description type "man 2 umask".

    :param number mask-bits: mask bits.
    :return: previous mask bits.
    :rtype:  number

    **Example:**

     .. code-block:: tarantoolsession

        tarantool> fio.umask(tonumber('755', 8))
        ---
        - 493
        ...

.. function:: lstat(path-name)
               stat(path-name)

    Returns information about a file object. For details type "man 2 lstat" or
    "man 2 stat".

    :param string path-name: path name of file.
    :return: fields which describe the file's block size, creation time, size,
             and other attributes.
    :rtype:  table

    **Example:**

     .. code-block:: tarantoolsession

        tarantool> fio.lstat('/etc')
        ---
        - inode: 1048577
          rdev: 0
          size: 12288
          atime: 1421340698
          mode: 16877
          mtime: 1424615337
          nlink: 160
          uid: 0
          blksize: 4096
          gid: 0
          ctime: 1424615337
          dev: 2049
          blocks: 24
        ...

.. function:: mkdir(path-name)
              rmdir(path-name)

    Create or delete a directory. For details type
    "man 2 mkdir" or "man 2 rmdir".

    :param string path-name: path of directory.
    :return: true if success, false if failure.
    :rtype:  boolean

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.mkdir('/etc')
        ---
        - false
        ...

.. function:: glob(path-name)

    Return a list of files that match an input string. The list is constructed
    with a single flag that controls the behavior of the function: GLOB_NOESCAPE.
    For details type "man 3 glob".

    :param string path-name: path-name, which may contain wildcard characters.
    :return: list of files whose names match the input string
    :rtype:  table

    Possible errors: nil.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.glob('/etc/x*')
        ---
        - - /etc/xdg
          - /etc/xml
          - /etc/xul-ext
        ...

.. function:: tempdir()

    Return the name of a directory that can be used to store temporary files.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.tempdir()
        ---
        - /tmp/lG31e7
        ...

.. function:: cwd()

    Return the name of the current working directory.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.cwd()
        ---
        - /home/username/tarantool_sandbox
        ...

.. function:: link     (src, dst)
              symlink  (src, dst)
              readlink (src)
              unlink   (src)

    Functions to create and delete links. For details type "man readlink",
    "man 2 link", "man 2 symlink", "man 2 unlink"..

    :param string src: existing file name.
    :param string dst: linked name.

    :return: ``fio.link`` and ``fio.symlink`` and ``fio.unlink`` return true if
             success, false if failure. ``fio.readlink`` returns the link value
             if success, nil if failure.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.link('/home/username/tmp.txt', '/home/username/tmp.txt2')
        ---
        - true
        ...
        tarantool> fio.unlink('/home/username/tmp.txt2')
        ---
        - true
        ...

.. function:: rename(path-name, new-path-name)

    Rename a file or directory. For details type "man 2 rename".

    :param string     path-name: original name.
    :param string new-path-name: new name.

    :return: true if success, false if failure.
    :rtype:  boolean

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.rename('/home/username/tmp.txt', '/home/username/tmp.txt2')
        ---
        - true
        ...

.. function:: chown(path-name, owner-user, owner-group)
              chmod(path-name, new-rights)

    Manage the rights to file objects, or ownership of file objects.
    For details type "man 2 chown" or "man 2 chmod".

    :param string owner-user: new user uid.
    :param string owner-group: new group uid.
    :param number new-rights: new permissions

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.chmod('/home/username/tmp.txt', tonumber('0755', 8))
        ---
        - true
        ...
        tarantool> fio.chown('/home/username/tmp.txt', 'username', 'username')
        ---
        - true
        ...

.. function:: truncate(path-name, new-size)

    Reduce file size to a specified value. For details type "man 2 truncate".

    :param string path-name:
    :param number new-size:

    :return: true if success, false if failure.
    :rtype:  boolean

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.truncate('/home/username/tmp.txt', 99999)
        ---
        - true
        ...

.. function:: sync()

    Ensure that changes are written to disk. For details type "man 2 sync".

    :return: true if success, false if failure.
    :rtype:  boolean

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fio.sync()
        ---
        - true
        ...

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

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fh = fio.open('/home/username/tmp.txt', {'O_RDWR', 'O_APPEND'})
        ---
        ...
        tarantool> fh -- display file handle returned by fio.open
        ---
        - fh: 11
        ...

.. class:: file-handle

    .. method:: close()

        Close a file that was opened with ``fio.open``. For details type "man 2 close".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: true if success, false on failure.
        :rtype:  boolean

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fh:close() -- where fh = file-handle
            ---
            - true
            ...

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

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fh:pread(25, 25)
            ---
            - |
              elete from t8//
              insert in
            ...

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

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fh:write('new data')
            ---
            - true
            ...

    .. method:: truncate(new-size)

        Change the size of an open file. Differs from ``fio.truncate``, which
        changes the size of a closed file.

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: true if success, false if failure.
        :rtype:  boolean

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fh:truncate(0)
            ---
            - true
            ...

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

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fh:seek(20, 'SEEK_SET')
            ---
            - 20
            ...

    .. method:: stat()

        Return statistics about an open file. This differs from ``fio.stat``
        which return statistics about a closed file. For details type "man 2 stat".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: details about the file.
        :rtype:  table

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fh:stat()
            ---
            - inode: 729866
              rdev: 0
              size: 100
              atime: 140942855
              mode: 33261
              mtime: 1409430660
              nlink: 1
              uid: 1000
              blksize: 4096
              gid: 1000
              ctime: 1409430660
              dev: 2049
              blocks: 8
            ...


    .. method:: fsync()
                fdatasync()

        Ensure that file changes are written to disk, for an open file.
        Compare ``fio.sync``, which is for all files. For details type
        "man 2 fsync" or "man 2 fdatasync".

        :param userdata fh: file-handle as returned by ``fio.open()``.
        :return: true if success, false if failure.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fh:fsync()
            ---
            - true
            ...
