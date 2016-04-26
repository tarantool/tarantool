.. _crypto:

-------------------------------------------------------------------------------
                            Package `crypto`
-------------------------------------------------------------------------------

.. module:: crypto

"Crypto" is short for "Cryptography", which generally refers to
the production of a digest value from a function (usually a
`Cryptographic hash function`_), applied against a string.
Tarantool's crypto package supports
ten  types of cryptographic hash functions (AES_, DES_, DSS_, MD4_, MD5_, MDC2_, RIPEMD_, SHA-0_, SHA-1_, SHA-2_).
Some of the crypto functionality is also present in the :ref:`digest` package.
The functions in crypto are:

:codebold:`crypto.cipher.`:codeitalic:`{aes128|aes192|aes256|des}.{cbc|cfg|ecb|ofb}.{encrypt|decrypt}` (:codeitalic:`string`, :codeitalic:`key` :codenormal:`[,`:codeitalic:`initialization vector`:codenormal:`])` |br|
Pass or return a cipher derived from the string, key, and (optionally, sometimes) initialization vector.
The four choices :codenormal:`aes128|aes192|aes256|des` indicate whether the algorithm will be aes-128 (with
192-bit binary strings using AES), aes-192 (with 192-bit binary strings using AES), aes-256 (with
256-bit binary strings using AES), or des (with 56-bit binary strings using DES, though DES is not recommended). |br|
Examples; |br|
:codenormal:`crypto.cipher.aes192.cbc.encrypt('string', 'key', 'initialization')` |br|
:codenormal:`crypto.cipher.aes256.ecb.decrypt('string', 'key')` |br|


:codebold:`crypto.digest.`:codeitalic:`{dss|dss1|md4|md5|mdc2|ripemd160|sha|sha1|sha224|sha256|sha384|sha512}`:codenormal:`(`:codeitalic:`string`:codenormal:`)` |br|
Pass or return a digest derived from the string.
The twelve choices :codenormal:`dss|dss1|md4|md5|mdc2|ripemd160|sha|sha1|sha224|sha256|sha384|sha512`
indicate whether the algorithm will be dss (using DSS), dss (using DSS-1), md4 (with 128-bit
binary strings using MD4), md5 (with 128-bit binary strings using MD5), mdc2 (using MDC2),
sha (with 160-bit binary strings using SHA-0), sha-1 (with 160-bit binary strings using SHA-1),
sha-224 (with 224-bit binary strings using SHA-2), sha-256 (with 256-bit binary strings using SHA_2),
sha-384 (with 384-bit binary strings using SHA_2), or sha-512 (with 512-bit binary strings using SHA-2). |br|
Examples: |br|
:codenormal:`crypto.digest.md4('string')` |br|
:codenormal:`crypto.digest.sha512('string')` |br|

=========================================
Incremental methods in the crypto package
=========================================

    Suppose that a digest is done for a string 'A',
    then a new part 'B' is appended to the string,
    then a new digest is required.
    The new digest could be recomputed for the whole string 'AB',
    but it is faster to take what was computed
    before for 'A' and apply changes based on the new part 'B'.
    This is called multi-step or "incremental" digesting,
    which Tarantool supports for all crypto functions ...

.. code-block:: lua

      crypto = require('crypto')

      -- print aes-192 digest of 'AB', with one step, then incrementally
      print(crypto.cipher.aes192.cbc.encrypt('AB', 'key'))
      c = crypto.cipher.aes192.cbc.encrypt.new()
      c:init()
      c:update('A', 'key')
      c:update('B', 'key')
      print(c:result())
      c:free()

      -- print sha-256 digest of 'AB', with one step, then incrementally
      print(crypto.digest.sha256('AB'))
      c = crypto.digest.sha256.new()
      c:init()
      c:update('A')
      c:update('B')
      print(c:result())
      c:free()

========================================================
Getting the same results from digest and crypto packages
========================================================

The following functions are equivalent.
For example, the digest function and the
crypto function x will both produce the same result.

:codenormal:`crypto.cipher.aes256.cbc.encrypt('string', 'key') == digest.aes256cbc.encrypt('string', 'key')` |br|
:codenormal:`crypto.digest.md4('string') == digest.md4('string')` |br|
:codenormal:`crypto.digest.md5('string') == digest.md5('string')` |br|
:codenormal:`crypto.digest.sha('string') == digest.sha('string')` |br|
:codenormal:`crypto.digest.sha1('string') == digest.sha1('string')` |br|
:codenormal:`crypto.digest.sha224('string') == digest.sha224('string')` |br|
:codenormal:`crypto.digest.sha256('string') == digest.sha256('string')` |br|
:codenormal:`crypto.digest.sha384('string') == digest.sha384('string')` |br|
:codenormal:`crypto.digest.sha512('string') == digest.sha512('string')` |br|

.. _AES: https://en.wikipedia.org/wiki/Advanced_Encryption_Standard
.. _DES: https://en.wikipedia.org/wiki/Data_Encryption_Standard
.. _DSS: https://en.wikipedia.org/wiki/Payment_Card_Industry_Data_Security_Standard
.. _SHA-0: https://en.wikipedia.org/wiki/Sha-0
.. _SHA-1: https://en.wikipedia.org/wiki/Sha-1
.. _SHA-2: https://en.wikipedia.org/wiki/Sha-2
.. _MD4: https://en.wikipedia.org/wiki/Md4
.. _MD5: https://en.wikipedia.org/wiki/Md5
.. _MDC2: https://en.wikipedia.org/wiki/MDC-2
.. _RIPEMD: http://homes.esat.kuleuven.be/~bosselae/ripemd160.html
.. _Cryptographic hash function: https://en.wikipedia.org/wiki/Cryptographic_hash_function
.. _Consistent Hashing: https://en.wikipedia.org/wiki/Consistent_hashing
