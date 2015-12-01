-------------------------------------------------------------------------------
                            Package `digest`
-------------------------------------------------------------------------------

.. module:: digest

A "digest" is a value which is returned by a function (usually a
`Cryptographic hash function`_), applied against a string. Tarantool supports
five types of cryptographic hash functions (SHA-0_, SHA-1_, SHA-2_, MD4_, MD5)
as well as a checksum function (CRC32_), two functions for base64_, and two
non-cryptographic hash functions (guava_, murmur_). The functions in digest are:

    :func:`digest.sha() <digest.sha>`, |br|
    :func:`digest.sha_hex() <digest.sha_hex>`, |br|
    :func:`digest.sha1() <digest.sha1>`, |br|
    :func:`digest.sha1_hex() <digest.sha1_hex>`, |br|
    :func:`digest.sha224() <digest.sha224>`, |br|
    :func:`digest.sha224_hex() <digest.sha224_hex>`, |br|
    :func:`digest.sha256() <digest.sha256>`, |br|
    :func:`digest.sha256_hex() <digest.sha256_hex>`, |br|
    :func:`digest.sha384() <digest.sha384>`, |br|
    :func:`digest.sha384_hex() <digest.sha384_hex>`, |br|
    :func:`digest.sha512() <digest.sha512>`, |br|
    :func:`digest.sha512_hex() <digest.sha512_hex>`, |br|
    :func:`digest.md4() <digest.md4>`, |br|
    :func:`digest.md4_hex() <digest.md4_hex>`, |br|
    :func:`digest.md5() <digest.md5>`, |br|
    :func:`digest.md5_hex() <digest.md5_hex>`, |br|
    :func:`digest.crc32() <digest.crc32>`, |br|
    :func:`digest.crc32.update() <digest.crc32.update>`, |br|
    :func:`digest.crc32.new() <digest.crc32.new>`, |br|
    :func:`digest.base64_encode() <digest.base64_encode>`, |br|
    :func:`digest.base64_decode() <digest.base64_decode>`, |br|
    :func:`digest.urandom() <digest.urandom>`, |br|
    :func:`digest.guava() <digest.guava>`, |br|
    :func:`digest.murmur() <digest.murmur>`, |br|
    :func:`digest.murmur.new() <digest.murmur.new>` |br|


.. function:: digest.sha({string})

    Returns 160-bit digest made with SHA-0. Not recommended.


.. function:: digest.sha_hex({string})

    Returns hexadecimal of a digest calculated with sha.


.. function:: digest.sha1({string})

    Returns 160-bit digest made with SHA-1.


.. function:: digest.sha1_hex({string})

    Returns hexadecimal of a digest calculated with sha1.


.. function:: digest.sha224({string})

    Returns 224-bit digest made with SHA-2.


.. function:: digest.sha224_hex({string})

    Returns hexadecimal of a digest calculated with sha224.


.. function:: digest.sha256({string})

    Returns 256-bit digest made with SHA-2.


.. function:: digest.sha256_hex({string})

    Returns hexadecimal of a digest calculated with sha256.


.. function:: digest.sha384({string})

    Returns 384-bit digest made with SHA-2.


.. function:: digest.sha384_hex({string})

    Returns hexadecimal of a digest calculated with sha384.


.. function:: digest.sha512({string})

    Returns 512-bit digest made with SHA-2.


.. function:: digest.sha512_hex({string})

    Returns hexadecimal of a digest calculated with sha512.


.. function:: digest.md4({string})

    Returns 128-bit digest made with MD4.


.. function:: digest.md4_hex({string})

    Returns hexadecimal of a digest calculated with md4.


.. function:: digest.md5({string})

    Returns 256-bit digest made with MD5.


.. function:: digest.md5_hex({string})

    Returns hexadecimal of a digest calculated with md5.


.. function:: digest.crc32({string})

    Returns 32-bit checksum made with CRC32.

    The crc32 and crc32_update function use the `CRC-32C (Castagnoli)`_ polynomial
    value: 0x11EDC6F41 / 4812730177. If it is necessary to be
    compatible with other checksum functions in other
    programming languages, ensure that the other functions use
    the same polynomial value. |br| For example, in Python,
    install the crcmod package and say:

    .. code-block:: pycon

      >>> import crcmod
      >>> fun = crcmod.mkCrcFun('4812730177')
      >>> fun('string')
      3304160206L

    .. _CRC-32C (Castagnoli): https://en.wikipedia.org/wiki/Cyclic_redundancy_check#Standards_and_common_use


.. function:: digest.crc32.update({number, string})

    Returns update of a checksum calculated with CRC32. See :func:`digest.crc32() <digest.crc32>` notes.


.. function:: digest.crc32.new()

    Initiates incremental crc32. See :func:`incremental digests <digest.murmur.new>` notes.


.. function:: digest.base64_encode({string})

    Returns base64 encoding from a regular string.


.. function:: digest.base64_decode({string})

    Returns a regular string from a base64 encoding.


.. function:: digest.urandom({integer})

    Returns array of random bytes with length = integer.


.. function:: digest.guava({integer}, {integer})

    Returns a number made with consistent hash.

    The guava function uses the `Consistent Hashing`_ algorithm of
    the Google guava library. The first parameter should be a
    hash code; the second parameter should be the number of
    buckets; the returned value will be an integer between 0
    and the number of buckets. For example,

    .. code-block:: tarantoolsession

        tarantool> digest.guava(10863919174838991, 11)
        ---
        - 8
        ...


.. function:: digest.murmur({string})

    Returns 32-bit digest made with MurmurHash.


.. function:: digest.murmur.new([{seed}])

    Initiates incremental MurmurHash.

    Suppose that a digest is done for a string 'A',
    then a new part 'B' is appended to the string,
    then a new digest is required.
    The new digest could be recomputed for the whole string 'AB',
    but it is faster to take what was computed
    before for 'A' and apply changes based on the new part 'B'.
    This is called multi-step or "incremental" digesting,
    which Tarantool supports with crc32 and with murmur ...

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

    tarantool> digest = require('diges')
    ---
    ...
    tarantool> function password_insert()
             >   box.space.tester:insert{12345, digest.sha1('^S^e^c^ret Wordpass')}
             >   return 'OK'
             > end
    ---
    ...
    tarantool> function password_check(password)
             >   local t
             >   local t = box.space.tester:select{12345}
             >   if digest.sha1(password) == t[2] then
             >     print('Password is valid')
             >   else
             >     print('Password is not valid')
             >   end
             > end
    ---
    ...
    tarantool> password_insert()
    ---
    - 'OK'
    ...

If a later user calls the ``password_check()`` function and enters
the wrong password, the result is an error.

.. code-block:: tarantoolsession

    tarantool> password_check('Secret Password')
    Password is not valid
    ---
    ...

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
