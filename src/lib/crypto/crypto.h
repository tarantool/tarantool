#ifndef TARANTOOL_LIB_CRYPTO_H_INCLUDED
#define TARANTOOL_LIB_CRYPTO_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(__cplusplus)
extern "C" {
#endif
/**
 * Cryptography library based on OpenSSL. It provides wrappers
 * around OpenSSL functions encapsulating compatibility issues and
 * problematic non-standard C API.
 *
 * Most of the cipher algorithms here are block-wise, with a
 * secret key and sometimes with an additional public key. A
 * secret key should be shared among communicating nodes and never
 * transmitted explicitly.
 *
 * A public key in some algorithms is also called initial vector,
 * and is an additional entropy factor. It prevents an attack when
 * an attacker somehow learns a purpose or content of one data
 * packet and immediately learns purpose of all the same looking
 * packets. Initial vector should be generated randomly for each
 * data packet, nonetheless it is not private and can be sent
 * without encryption.
 */

enum crypto_algo {
	/** None to disable encryption. */
	CRYPTO_ALGO_NONE,
	/**
	 * AES - Advanced Encryption Standard. It is a symmetric
	 * key block cipher algorithm. AES encodes data in blocks
	 * of 128 bits, for what it uses a key and an initial
	 * vector. Key size can be 128, 192 and 256 bits. AES
	 * still is considered strong cipher.
	 */
	CRYPTO_ALGO_AES128,
	CRYPTO_ALGO_AES192,
	CRYPTO_ALGO_AES256,
	/**
	 * DES - Data Encryption Standard. It is a symmetric key
	 * block cipher algorithm. It has a fixed block size 64
	 * bits, a key of size 56 working bits and 8 not used for
	 * encryption. Its initial vector size is 64 bits as well.
	 * Note, that DES is not considered secure because of too
	 * small key size.
	 */
	CRYPTO_ALGO_DES,
	crypto_algo_MAX,
};

extern const char *crypto_algo_strs[];

enum crypto_mode {
	/**
	 * Electronic CodeBook. The message is split into blocks,
	 * and each block is encrypted separately. Weak, because
	 * data patterns leak. But fast, can be parallelized. And
	 * does not require an initial vector.
	 *
	 *  C_i = Encrypt(P_i)
	 */
	CRYPTO_MODE_ECB,
	/**
	 * Cipher Block Chaining. Each block of plain data is
	 * XORed with the previous encrypted block before being
	 * encrypted. The most commonly used mode. Encryption
	 * can't be parallelized because of sequential dependency
	 * of blocks, but decryption *can be*, because each block
	 * in fact depends only on single previous encrypted
	 * block. A decoder does not need to decrypt C_i-1 to
	 * decrypt C_i.
	 *
	 *  C_i = Encrypt(P_i ^ C_i-1).
	 *  C_0 = IV
	 */
	CRYPTO_MODE_CBC,
	/**
	 * Cipher FeedBack. Works similarly to CBC, but a bit
	 * different. Each block of plain data is encrypted as
	 * XOR of the plain data with second encryption of the
	 * previous block. This mode is able to encode a message
	 * byte by byte, without addition of a padding. Decryption
	 * can be parallelized.
	 *
	 *  C_i = Encrypt(C_i-1) ^ P_i
	 *  C_0 = IV
	 */
	CRYPTO_MODE_CFB,
	/**
	 * Output FeedBack. The same as CFB, but encryption can be
	 * parallelized partially.
	 *
	 * C_i = P_i ^ Encrypt(I_i)
	 * I_i = Encrypt(I_i-1)
	 * I_0 = IV
	 */
	CRYPTO_MODE_OFB,
	crypto_mode_MAX,
};

extern const char *crypto_mode_strs[];

/**
 * Values obtained from EVP_CIPHER_*() API, but the constants are
 * needed in some places at compilation time. For example, for
 * statically sized buffers.
 */
enum {
	CRYPTO_AES_BLOCK_SIZE = 16,
	CRYPTO_AES_IV_SIZE = 16,
	CRYPTO_AES128_KEY_SIZE = 16,
	CRYPTO_AES192_KEY_SIZE = 24,
	CRYPTO_AES256_KEY_SIZE = 32,

	CRYPTO_DES_BLOCK_SIZE = 8,
	CRYPTO_DES_IV_SIZE = 8,
	CRYPTO_DES_KEY_SIZE = 8,

	CRYPTO_MAX_KEY_SIZE = 32,
	CRYPTO_MAX_IV_SIZE = 16,
	CRYPTO_MAX_BLOCK_SIZE = 16,
};

/**
 * OpenSSL API provides generic methods to do both encryption and
 * decryption depending on one 'int' parameter passed into
 * initialization function EVP_CipherInit_ex. Here these constants
 * are assigned to more readable names.
 */
enum crypto_direction {
	CRYPTO_DIR_DECRYPT = 0,
	CRYPTO_DIR_ENCRYPT = 1,
};

struct crypto_stream;

/**
 * Crypto stream is an object allowing to encrypt/decrypt data
 * packets with different public and secret keys step by step.
 */
struct crypto_stream *
crypto_stream_new(enum crypto_algo algo, enum crypto_mode mode,
		  enum crypto_direction dir);

/**
 * Start a new data packet. @a key and @a iv are secret and public
 * keys respectively.
 * @retval 0 Success.
 * @retval -1 Error. Diag is set.
 */
int
crypto_stream_begin(struct crypto_stream *s, const char *key, int key_size,
		    const char *iv, int iv_size);

/**
 * Encrypt/decrypt next part of the current data packet.
 * @retval -1 Error. Diag is set.
 * @return Number of written bytes. If > @a out_size then nothing
 *         is written, needed number of bytes is returned.
 */
int
crypto_stream_append(struct crypto_stream *s, const char *in, int in_size,
		     char *out, int out_size);

/**
 * Finalize the current data packet. Note, a margin can be added
 * to the result.
 * @retval -1 Error. Diag is set.
 * @return Number of written bytes. If > @a out_size then nothing
 *         is written, needed number of bytes is returned.
 */
int
crypto_stream_commit(struct crypto_stream *s, char *out, int out_size);

/** Delete a stream, free its memory. */
void
crypto_stream_delete(struct crypto_stream *s);

struct crypto_codec;

/**
 * Create a new codec working with a specified @a algo algorithm
 * in @a mode. It is remarkable that both algorithm and mode
 * strongly affect secrecy and efficiency. Codec is similar to
 * stream, but provides shorter and simpler API, and can both
 * decrypt and encrypt without recreation.
 */
struct crypto_codec *
crypto_codec_new(enum crypto_algo algo, enum crypto_mode mode,
		 const char *key, int key_size);

/**
 * Generate a new initial vector, dump it into @a out buffer and
 * return number of written (or wanted to be written) bytes.
 */
int
crypto_codec_gen_iv(struct crypto_codec *c, char *out, int out_size);

/**
 * Initial vector size. It is a constant depending on an
 * algorithm and mode.
 */
int
crypto_codec_iv_size(const struct crypto_codec *c);

/**
 * Encrypt plain data by codec @a c.
 * @param c Codec.
 * @param iv Initial vector.
 * @param in Plain input data.
 * @param in_size Byte size of @a in.
 * @param out Output buffer.
 * @param out_size Byte size of @a out.
 *
 * @retval -1 Error. Diag is set.
 * @return Number of written bytes. If > @a out_size then nothing
 *         is written, needed number of bytes is returned.
 */
int
crypto_codec_encrypt(struct crypto_codec *c, const char *iv,
		     const char *in, int in_size, char *out, int out_size);

/**
 * Decrypt cipher by codec @a c.
 * @param c Codec.
 * @param iv Initial vector used to encode @a in.
 * @param in Cipher to decode.
 * @param in_size Byte size of @a in.
 * @param out Output buffer.
 * @param out_size Byte size of @a out.
 *
 * @retval -1 Error. Diag is set.
 * @return Number of written bytes. If > @a out_size then nothing
 *         is written, needed number of bytes is returned.
 */
int
crypto_codec_decrypt(struct crypto_codec *c, const char *iv,
		     const char *in, int in_size, char *out, int out_size);

/** Delete a codec, free its memory. */
void
crypto_codec_delete(struct crypto_codec *c);

void
crypto_init(void);

void
crypto_free(void);

#if defined(__cplusplus)
}
#endif

#endif /* TARANTOOL_LIB_CRYPTO_H_INCLUDED */
