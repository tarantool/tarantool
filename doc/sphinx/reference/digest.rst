.. _digest:

-------------------------------------------------------------------------------
                            Package `digest`
-------------------------------------------------------------------------------

.. module:: digest

A "digest" is a value which is returned by a function (usually a
`Cryptographic hash function`_), applied against a string.
Tarantool's digest package supports
five types of cryptographic hash functions (AES_, MD4_, MD5_, SHA-0_, SHA-1_, SHA-2_)
as well as a checksum function (CRC32_), two functions for base64_, and two
non-cryptographic hash functions (guava_, murmur_).
Some of the digest functionality is also present in the :ref:`crypto <crypto>` package.
The functions in digest are:

:codebold:`digest.aes256cbc.encrypt(`:codeitalic:`string`, :codeitalic:`key`) |br|
Returns 256-bit binary string = digest made with AES.

:codebold:`digest.md4(`:codeitalic:`string`) |br|
Returns 128-bit binary string = digest made with MD4. |br|

:codebold:`digest.md4_hex(`:codeitalic:`string`) |br|
Returns 32-byte string = hexadecimal of a digest calculated with md4.

:codebold:`digest.md5(`:codeitalic:`string`) |br|
Returns 128-bit binary string = digest made with MD5.

:codebold:`digest.md5_hex(`:codeitalic:`string`) |br|
Returns 32-byte string = hexadecimal of a digest calculated with md5.

:codebold:`digest.sha(`:codeitalic:`string`) |br|
Returns 160-bit binary string = digest made with SHA-0. Not recommended.

:codebold:`digest.sha_hex(`:codeitalic:`string`) |br|
Returns 40-byte string = hexadecimal of a digest calculated with sha.

:codebold:`digest.sha1(`:codeitalic:`string`) |br|
Returns 160-bit binary string = digest made with SHA-1.

:codebold:`digest.sha1_hex(`:codeitalic:`string`) |br|
Returns 40-byte string = hexadecimal of a digest calculated with sha1.

:codebold:`digest.sha224(`:codeitalic:`string`) |br|
Returns 224-bit binary string = digest made with SHA-2.

:codebold:`digest.sha224_hex(`:codeitalic:`string`) |br|
Returns 56-byte string = hexadecimal of a digest calculated with sha224.

:codebold:`digest.sha256(`:codeitalic:`string`) |br|
Returns 256-bit binary string =  digest made with SHA-2.

:codebold:`digest.sha256_hex(`:codeitalic:`string`) |br|
Returns 64-byte string = hexadecimal of a digest calculated with sha256.

:codebold:`digest.sha384(`:codeitalic:`string`) |br|
Returns 384-bit binary string =  digest made with SHA-2.

:codebold:`digest.sha384_hex(`:codeitalic:`string`) |br|
Returns 96-byte string = hexadecimal of a digest calculated with sha384.

:codebold:`digest.sha512(`:codeitalic:`string`) |br|
Returns 512-bit binary tring = digest made with SHA-2.

:codebold:`digest.sha512_hex(`:codeitalic:`string`) |br|
Returns 128-byte string = hexadecimal of a digest calculated with sha512.

:codebold:`digest.base64_encode(`:codeitalic:`string`) |br|
Returns base64 encoding from a regular string.

:codebold:`digest.base64_decode(`:codeitalic:`string`) |br|
Returns a regular string from a base64 encoding.

:codebold:`digest.urandom(`:codeitalic:`integer`) |br|
Returns array of random bytes with length = integer.

:codebold:`digest.crc32(`:codeitalic:`string`) |br|
Returns 32-bit checksum made with CRC32.

    The crc32 and crc32_update functions use the `CRC-32C (Castagnoli)`_ polynomial
    value: 0x11EDC6F41 / 4812730177. If it is necessary to be
    compatible with other checksum functions in other
    programming languages, ensure that the other functions use
    the same polynomial value. |br| For example, in Python,
    install the crcmod package and say:

      >>> import crcmod
      >>> fun = crcmod.mkCrcFun('4812730177')
      >>> fun('string')
      3304160206L

.. _CRC-32C (Castagnoli): https://en.wikipedia.org/wiki/Cyclic_redundancy_check#Standards_and_common_use

:codebold:`digest.crc32.new()` |br|
Initiates incremental crc32.
See :ref:`incremental methods <incremental-digests>` notes.

.. _digest-guava:

:codebold:`digest.guava(`:codeitalic:`integer, integer`) |br|
Returns a number made with consistent hash.

    The guava function uses the `Consistent Hashing`_ algorithm of
    the Google guava library. The first parameter should be a
    hash code; the second parameter should be the number of
    buckets; the returned value will be an integer between 0
    and the number of buckets. For example,

    :codenormal:`tarantool>` :codebold:`digest.guava(10863919174838991, 11)` |br|
    :codenormal:`---` |br|
    :codenormal:`- 8` |br|
    :codenormal:`...` |br|

:codebold:`digest.murmur(`:codeitalic:`string`) |br|
Returns 32-bit binary string = digest made with MurmurHash.

:codebold:`digest.murmur.new([`:codeitalic:`seed`]) |br|
Initiates incremental MurmurHash.
See :ref:`incremental methods <incremental-digests>` notes.

.. _incremental-digests:

=========================================
Incremental methods in the digest package
=========================================

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

    :codenormal:`tarantool>` :codebold:`digest = require('digest')` |br|
    :codenormal:`---` |br|
    :codenormal:`...` |br|
    :codenormal:`tarantool>` :codebold:`function password_insert()` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>`   :codebold:`box.space.tester:insert{12345, digest.sha1('^S^e^c^ret Wordpass')}` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>`   :codebold:`return 'OK'` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`end` |br|
    :codenormal:`---` |br|
    :codenormal:`...` |br|
    :codenormal:`tarantool>` :codebold:`function password_check(password)` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` |nbsp| :codebold:`local t` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` |nbsp| :codebold:`local t = box.space.tester:select{12345}` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` |nbsp| :codebold:`if digest.sha1(password) == t[2] then` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` |nbsp| |nbsp| |nbsp| :codebold:`print('Password is valid')` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` |nbsp| :codebold:`else` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` |nbsp| |nbsp| |nbsp| :codebold:`print('Password is not valid')` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` |nbsp| :codebold:`end` |br|
    |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`>` :codebold:`end` |br|
    :codenormal:`---` |br|
    :codenormal:`...` |br|
    :codenormal:`tarantool>` :codebold:`password_insert()` |br|
    :codenormal:`---` |br|
    :codenormal:`- 'OK'` |br|
    :codenormal:`...` |br|

If a later user calls the ``password_check()`` function and enters
the wrong password, the result is an error.

    :codenormal:`tarantool>` :codebold:`password_check('Secret Password')` |br|
    :codenormal:`Password is not valid` |br|
    :codenormal:`---` |br|
    :codenormal:`...` |br|

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
