.. _digest:

-------------------------------------------------------------------------------
                            Package `digest`
-------------------------------------------------------------------------------

.. module:: digest

A "digest" is a value which is returned by a function (usually a `Cryptographic
hash function`_), applied against a string. Tarantool's digest package supports
five types of cryptographic hash functions (AES_, MD4_, MD5_, SHA-0_, SHA-1_,
SHA-2_) as well as a checksum function (CRC32_), two functions for base64_, and
two non-cryptographic hash functions (guava_, murmur_). Some of the digest
functionality is also present in the :ref:`crypto <crypto>` package. The
functions in digest are:

.. module:: digest

.. function:: md4(string)
              md4_hex(string)

    Returns 128-bit binary string = digest made with MD4.

    Returns 32-byte string = hexadecimal of a digest calculated with md4.

.. function:: md5(string)
              md5_hex(string)

    Returns 128-bit binary string = digest made with MD5.

    Returns 32-byte string = hexadecimal of a digest calculated with md5.

.. function:: sha(string)
              sha_hex(string)

    Returns 160-bit binary string = digest made with SHA-0. Not recommended.

    Returns 40-byte string = hexadecimal of a digest calculated with sha.

.. function:: sha1(string)
              sha1_hex(string)

    Returns 160-bit binary string = digest made with SHA-1.

    Returns 40-byte string = hexadecimal of a digest calculated with sha1.

.. function:: sha224(string)
              sha224_hex(string)

    Returns 224-bit binary string = digest made with SHA-2.

    Returns 56-byte string = hexadecimal of a digest calculated with sha224.

.. function:: sha256(string)
              sha256_hex(string)

    Returns 256-bit binary string =  digest made with SHA-2.

    Returns 64-byte string = hexadecimal of a digest calculated with sha256.

.. function:: sha384(string)
              sha384_hex(string)

    Returns 384-bit binary string =  digest made with SHA-2.

    Returns 96-byte string = hexadecimal of a digest calculated with sha384.

.. function:: sha512(string)
              sha512_hex(string)

    Returns 512-bit binary tring = digest made with SHA-2.

    Returns 128-byte string = hexadecimal of a digest calculated with sha512.

.. function:: base64_encode(string)
              base64_decode(string)

    Returns base64 encoding from a regular string.

    Returns a regular string from a base64 encoding.

.. function:: urandom(string)

    Returns array of random bytes with length = integer.

.. function:: crc32(string)

    Returns 32-bit checksum made with CRC32.

    The crc32 and crc32_update functions use the `CRC-32C (Castagnoli)`_
    polynomial value: 0x11EDC6F41 / 4812730177. If it is necessary to be
    compatible with other checksum functions in other programming languages,
    ensure that the other functions use the same polynomial value. |br|
    For example, in Python, install the crcmod package and say:

    .. code-block:: pycon

        >>> import crcmod
        >>> fun = crcmod.mkCrcFun('4812730177')
        >>> fun('string')
        3304160206L

    .. _CRC-32C (Castagnoli): https://en.wikipedia.org/wiki/Cyclic_redundancy_check#Standards_and_common_use

.. function:: digest.crc32.new()

    Initiates incremental crc32.
    See :ref:`incremental methods <incremental-digests>` notes.

.. _digest-guava:

.. function:: guava(integer, integer)

    Returns a number made with consistent hash.

    The guava function uses the `Consistent Hashing`_ algorithm of the Google
    guava library. The first parameter should be a hash code; the second
    parameter should be the number of buckets; the returned value will be an
    integer between 0 and the number of buckets. For example,

    .. code-block:: tarantoolsession

        tarantool> digest.guava(10863919174838991, 11)
        ---
        - 8
        ...

.. function:: murmur(string)

    Returns 32-bit binary string = digest made with MurmurHash.

.. function:: digest.murmur.new([seed])

    Initiates incremental MurmurHash.
    See :ref:`incremental methods <incremental-digests>` notes.

.. _incremental-digests:

=========================================
Incremental methods in the digest package
=========================================

Suppose that a digest is done for a string 'A', then a new part 'B' is appended
to the string, then a new digest is required. The new digest could be recomputed
for the whole string 'AB', but it is faster to take what was computed before for
'A' and apply changes based on the new part 'B'. This is called multi-step or
"incremental" digesting, which Tarantool supports with crc32 and with murmur..

.. code-block:: lua

      digest = require('digest')

      -- print crc32 of 'AB', with one step, then incrementally
      print(digest.crc32('AB'))
      c = digest.crc32.new()
      c:update('A')
      c:update('B')
      print(c:result())

      -- print murmur hash of 'AB', with one step, then incrementally
      print(digest.murmur('AB'))
      m = digest.murmur.new()
      m:update('A')
      m:update('B')
      print(m:result())

=================================================
                     Example
=================================================

In the following example, the user creates two functions, ``password_insert()``
which inserts a SHA-1_ digest of the word "**^S^e^c^ret Wordpass**" into a tuple
set, and ``password_check()`` which requires input of a password.

.. code-block:: tarantoolsession

    tarantool> digest = require('digest')
    ---
    ...
    tarantool> function password_insert()
             >     box.space.tester:insert{12345, digest.sha1('^S^e^c^ret Wordpass')}
             >     return 'OK'
             > end
    ---
    ...
    tarantool> function password_checkt(password)
             >     local t = box.space.tester:select{12345}
             >     if digest.sha1(password) == t[2] then
             >         return 'Password is valid'
             >     else
             >         return 'Password is not valid'
             >     end
             > end
    ---
    ...
    tarantool> password_insert()
    ---
    - 'OK'
    ...

If a later user calls the ``password_check()`` function and enters the wrong
password, the result is an error.

.. code-block:: tarantoolsession

    tarantool> password_check('Secret Password')
    ---
    - 'Password is not valid'
    ...

.. _AES: https://en.wikipedia.org/wiki/Advanced_Encryption_Standard
.. _SHA-0: https://en.wikipedia.org/wiki/Sha-0
.. _SHA-1: https://en.wikipedia.org/wiki/Sha-1
.. _SHA-2: https://en.wikipedia.org/wiki/Sha-2
.. _MD4: https://en.wikipedia.org/wiki/Md4
.. _MD5: https://en.wikipedia.org/wiki/Md5
.. _CRC32: https://en.wikipedia.org/wiki/Cyclic_redundancy_check
.. _base64: https://en.wikipedia.org/wiki/Base64
.. _Cryptographic hash function: https://en.wikipedia.org/wiki/Cryptographic_hash_function
.. _Consistent Hashing: https://en.wikipedia.org/wiki/Consistent_hashing
.. _CRC-32C (Castagnoli): https://en.wikipedia.org/wiki/Cyclic_redundancy_check#Standards_and_common_use
.. _guava: https://code.google.com/p/guava-libraries/wiki/HashingExplained
.. _Murmur: https://en.wikipedia.org/wiki/MurmurHash
