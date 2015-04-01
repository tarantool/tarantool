-------------------------------------------------------------------------------
                            Package `digest`
-------------------------------------------------------------------------------

A "digest" is a value which is returned by a `Cryptographic hash function`_ applied
against a string. Tarantool supports five types of cryptographic hash functions
(MD4_, MD5_, SHA-0_, SHA-1_, SHA-2_) as well as a checksum function (CRC32_) and two
functions for base64_. The functions in digest are:

.. module:: digest

.. function:: crc32(string)
              crc32_update(number, string)

    Returns 32-bit checksum made with CRC32. |br|
    Returns update of a checksum calculated with CRC32.

    .. NOTE::

      This function uses the `CRC-32C (Castagnoli)`_ polynomial
      value: 0x11EDC6F41 / 4812730177. If it is necessary to be
      compatible with other checksum functions in other
      programming languages, ensure that the other functions use
      the same polynomial value. |br| For example, in Python,
      install the crcmod package and say:

      .. code-block:: python

        >>> import crcmod
        >>> fun = crcmod.mkCrcFun('4812730177')
        >>> fun('string')
        3304160206L

.. _CRC-32C (Castagnoli): https://en.wikipedia.org/wiki/Cyclic_redundancy_check#Standards_and_common_use

.. function:: sha(string)
              sha_hex(string)

    Returns 160-bit digest made with SHA-0. Not recommended. |br|
    Returns hexadecimal of a digest calculated with sha.

.. function:: sha1(string)
              sha1_hex(string)

    Returns 160-bit digest made with SHA-1. |br|
    Returns hexadecimal of a digest calculated with sha1.

.. function:: sha224(string)
              sha224_hex(string)

    Returns 224-bit digest made with SHA-2. |br|
    Returns hexadecimal of a digest calculated with sha224.

.. function:: sha256(string)
              sha256_hex(string)

    Returns 256-bit digest made with SHA-2. |br|
    Returns hexadecimal of a digest calculated with sha256.

.. function:: sha384(string)
              sha384_hex(string)

    Returns 384-bit digest made with SHA-2. |br|
    Returns hexadecimal of a digest calculated with sha384.

.. function:: sha512(string)
              sha512_hex(string)

    Returns 512-bit digest made with SHA-2. |br|
    Returns hexadecimal of a digest calculated with sha512.

.. function:: md4(string)
              md4_hex(string)

    Returns 128-bit digest made with MD4. |br|
    Returns hexadecimal of a digest calculated with md4.

.. function:: md5(string)
              md5_hex(string)

    Returns 256-bit digest made with MD5. |br|
    Returns hexadecimal of a digest calculated with md5.

.. function:: base64_encode(string)
              base64_decode(string)

    Returns base64 encoding from a regular string. |br|
    Returns a regular string from a base64 encoding.

.. function:: urandom(integer)

    Returns array of random bytes with length = integer.

.. function:: guava(integer, integer)

    Returns a number made with consistent hash.

    .. NOTE::

        This function uses the `Consistent Hashing`_ algorithm of
        the Google guava library. The first parameter should be a
        hash code; the second parameter should be the number of
        buckets; the returned value will be an integer between 0
        and the number of buckets. For example,

        .. code-block:: lua

            localhost> digest.guava(10863919174838991, 11)
            8

=================================================
                     Example
=================================================

In the following example, the user creates two functions, ``password_insert()``
which inserts a SHA-1_ digest of the word "**^S^e^c^ret Wordpass**" into a tuple
set, and ``password_check()`` which requires input of a password.

.. code-block:: lua

    localhost> digest = require('digest')
    localhost> -- this means ignore line feeds until next '!'
    localhost> console = require('console'); console.delimiter('!')
    localhost> function password_insert()
            ->   box.space.tester:insert{12345,
            ->       digest.sha1('^S^e^c^ret Wordpass')}
            ->   return 'OK'
            ->   end!
    ---
    ...
    localhost> function password_check(password)
            ->   local t
            ->   t=box.space.tester:select{12345}
            ->   if (digest.sha1(password)==t[2]) then
            ->     print('Password is valid')
            ->     else
            ->       print('Password is not valid')
            ->     end
            -> end!
    ---
    ...
    localhost> password_insert()!
    Call OK, 1 rows affected
    ['OK']
    localhost> -- back to normal: commands end with line feed!
    localhost> console.delimiter('')

If a later user calls the ``password_check()`` function and enters
the wrong password, the result is an error.

.. code-block:: lua

    localhost> password_check ('Secret Password')
    ---
    Password is not valid
    ...

.. _MD4: https://en.wikipedia.org/wiki/Md4
.. _MD5: https://en.wikipedia.org/wiki/Md5
.. _SHA-0: https://en.wikipedia.org/wiki/Sha-0
.. _SHA-1: https://en.wikipedia.org/wiki/Sha-1
.. _SHA-2: https://en.wikipedia.org/wiki/Sha-2
.. _CRC32: https://en.wikipedia.org/wiki/Cyclic_redundancy_check
.. _base64: https://en.wikipedia.org/wiki/Base64
.. _Cryptographic hash function: https://en.wikipedia.org/wiki/Cryptographic_hash_function
.. _Consistent Hashing: https://en.wikipedia.org/wiki/Consistent_hashing
.. _CRC-32C (Castagnoli): https://en.wikipedia.org/wiki/Cyclic_redundancy_check#Standards_and_common_use
