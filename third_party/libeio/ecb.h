/*
 * libecb - http://software.schmorp.de/pkg/libecb
 *
 * Copyright (©) 2009-2015 Marc Alexander Lehmann <libecb@schmorp.de>
 * Copyright (©) 2011 Emanuele Giaquinta
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modifica-
 * tion, are permitted provided that the following conditions are met:
 *
 *   1.  Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *   2.  Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License ("GPL") version 2 or any later version,
 * in which case the provisions of the GPL are applicable instead of
 * the above. If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the BSD license, indicate your decision
 * by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL. If you do not delete the
 * provisions above, a recipient may use your version of this file under
 * either the BSD or the GPL.
 */

#ifndef ECB_H
#define ECB_H

/* 16 bits major, 16 bits minor */
#define ECB_VERSION 0x00010004

#ifdef _WIN32
  typedef   signed char   int8_t;
  typedef unsigned char  uint8_t;
  typedef   signed short  int16_t;
  typedef unsigned short uint16_t;
  typedef   signed int    int32_t;
  typedef unsigned int   uint32_t;
  #if __GNUC__
    typedef   signed long long int64_t;
    typedef unsigned long long uint64_t;
  #else /* _MSC_VER || __BORLANDC__ */
    typedef   signed __int64   int64_t;
    typedef unsigned __int64   uint64_t;
  #endif
  #ifdef _WIN64
    #define ECB_PTRSIZE 8
    typedef uint64_t uintptr_t;
    typedef  int64_t  intptr_t;
  #else
    #define ECB_PTRSIZE 4
    typedef uint32_t uintptr_t;
    typedef  int32_t  intptr_t;
  #endif
#else
  #include <inttypes.h>
  #if UINTMAX_MAX > 0xffffffffU
    #define ECB_PTRSIZE 8
  #else
    #define ECB_PTRSIZE 4
  #endif
#endif

#define ECB_GCC_AMD64 (__amd64 || __amd64__ || __x86_64 || __x86_64__)
#define ECB_MSVC_AMD64 (_M_AMD64 || _M_X64)

/* work around x32 idiocy by defining proper macros */
#if ECB_GCC_AMD64 || ECB_MSVC_AMD64
  #if _ILP32
    #define ECB_AMD64_X32 1
  #else
    #define ECB_AMD64 1
  #endif
#endif

/* many compilers define _GNUC_ to some versions but then only implement
 * what their idiot authors think are the "more important" extensions,
 * causing enormous grief in return for some better fake benchmark numbers.
 * or so.
 * we try to detect these and simply assume they are not gcc - if they have
 * an issue with that they should have done it right in the first place.
 */
#if !defined __GNUC_MINOR__ || defined __INTEL_COMPILER || defined __SUNPRO_C || defined __SUNPRO_CC || defined __llvm__ || defined __clang__
  #define ECB_GCC_VERSION(major,minor) 0
#else
  #define ECB_GCC_VERSION(major,minor) (__GNUC__ > (major) || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#endif

#define ECB_CLANG_VERSION(major,minor) (__clang_major__ > (major) || (__clang_major__ == (major) && __clang_minor__ >= (minor)))

#if __clang__ && defined __has_builtin
  #define ECB_CLANG_BUILTIN(x) __has_builtin (x)
#else
  #define ECB_CLANG_BUILTIN(x) 0
#endif

#if __clang__ && defined __has_extension
  #define ECB_CLANG_EXTENSION(x) __has_extension (x)
#else
  #define ECB_CLANG_EXTENSION(x) 0
#endif

#define ECB_CPP   (__cplusplus+0)
#define ECB_CPP11 (__cplusplus >= 201103L)

#if ECB_CPP
  #define ECB_C            0
  #define ECB_STDC_VERSION 0
#else
  #define ECB_C            1
  #define ECB_STDC_VERSION __STDC_VERSION__
#endif

#define ECB_C99   (ECB_STDC_VERSION >= 199901L)
#define ECB_C11   (ECB_STDC_VERSION >= 201112L)

#if ECB_CPP
  #define ECB_EXTERN_C extern "C"
  #define ECB_EXTERN_C_BEG ECB_EXTERN_C {
  #define ECB_EXTERN_C_END }
#else
  #define ECB_EXTERN_C extern
  #define ECB_EXTERN_C_BEG
  #define ECB_EXTERN_C_END
#endif

/*****************************************************************************/

/* ECB_NO_THREADS - ecb is not used by multiple threads, ever */
/* ECB_NO_SMP     - ecb might be used in multiple threads, but only on a single cpu */

#if ECB_NO_THREADS
  #define ECB_NO_SMP 1
#endif

#if ECB_NO_SMP
  #define ECB_MEMORY_FENCE do { } while (0)
#endif

/* http://www-01.ibm.com/support/knowledgecenter/SSGH3R_13.1.0/com.ibm.xlcpp131.aix.doc/compiler_ref/compiler_builtins.html */
#if __xlC__ && ECB_CPP
  #include <builtins.h>
#endif

#ifndef ECB_MEMORY_FENCE
  #if ECB_GCC_VERSION(2,5) || defined __INTEL_COMPILER || (__llvm__ && __GNUC__) || __SUNPRO_C >= 0x5110 || __SUNPRO_CC >= 0x5110
    #if __i386 || __i386__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("lock; orb $0, -1(%%esp)" : : : "memory")
      #define ECB_MEMORY_FENCE_ACQUIRE __asm__ __volatile__ (""                        : : : "memory")
      #define ECB_MEMORY_FENCE_RELEASE __asm__ __volatile__ ("")
    #elif ECB_GCC_AMD64
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("mfence"   : : : "memory")
      #define ECB_MEMORY_FENCE_ACQUIRE __asm__ __volatile__ (""         : : : "memory")
      #define ECB_MEMORY_FENCE_RELEASE __asm__ __volatile__ ("")
    #elif __powerpc__ || __ppc__ || __powerpc64__ || __ppc64__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("sync"     : : : "memory")
    #elif defined __ARM_ARCH_6__  || defined __ARM_ARCH_6J__  \
       || defined __ARM_ARCH_6K__ || defined __ARM_ARCH_6ZK__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("mcr p15,0,%0,c7,c10,5" : : "r" (0) : "memory")
    #elif defined __ARM_ARCH_7__  || defined __ARM_ARCH_7A__  \
       || defined __ARM_ARCH_7M__ || defined __ARM_ARCH_7R__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("dmb"      : : : "memory")
    #elif __aarch64__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("dmb ish"  : : : "memory")
    #elif (__sparc || __sparc__) && !(__sparc_v8__ || defined __sparcv8)
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("membar #LoadStore | #LoadLoad | #StoreStore | #StoreLoad" : : : "memory")
      #define ECB_MEMORY_FENCE_ACQUIRE __asm__ __volatile__ ("membar #LoadStore | #LoadLoad"                            : : : "memory")
      #define ECB_MEMORY_FENCE_RELEASE __asm__ __volatile__ ("membar #LoadStore             | #StoreStore")
    #elif defined __s390__ || defined __s390x__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("bcr 15,0" : : : "memory")
    #elif defined __mips__
      /* GNU/Linux emulates sync on mips1 architectures, so we force its use */
      /* anybody else who still uses mips1 is supposed to send in their version, with detection code. */
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ (".set mips2; sync; .set mips0" : : : "memory")
    #elif defined __alpha__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("mb"       : : : "memory")
    #elif defined __hppa__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ (""         : : : "memory")
      #define ECB_MEMORY_FENCE_RELEASE __asm__ __volatile__ ("")
    #elif defined __ia64__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("mf"       : : : "memory")
    #elif defined __m68k__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ (""         : : : "memory")
    #elif defined __m88k__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ ("tb1 0,%%r0,128" : : : "memory")
    #elif defined __sh__
      #define ECB_MEMORY_FENCE         __asm__ __volatile__ (""         : : : "memory")
    #endif
  #endif
#endif

#ifndef ECB_MEMORY_FENCE
  #if ECB_GCC_VERSION(4,7)
    /* see comment below (stdatomic.h) about the C11 memory model. */
    #define ECB_MEMORY_FENCE         __atomic_thread_fence (__ATOMIC_SEQ_CST)
    #define ECB_MEMORY_FENCE_ACQUIRE __atomic_thread_fence (__ATOMIC_ACQUIRE)
    #define ECB_MEMORY_FENCE_RELEASE __atomic_thread_fence (__ATOMIC_RELEASE)

  #elif ECB_CLANG_EXTENSION(c_atomic)
    /* see comment below (stdatomic.h) about the C11 memory model. */
    #define ECB_MEMORY_FENCE         __c11_atomic_thread_fence (__ATOMIC_SEQ_CST)
    #define ECB_MEMORY_FENCE_ACQUIRE __c11_atomic_thread_fence (__ATOMIC_ACQUIRE)
    #define ECB_MEMORY_FENCE_RELEASE __c11_atomic_thread_fence (__ATOMIC_RELEASE)

  #elif ECB_GCC_VERSION(4,4) || defined __INTEL_COMPILER || defined __clang__
    #define ECB_MEMORY_FENCE         __sync_synchronize ()
  #elif _MSC_VER >= 1500 /* VC++ 2008 */
    /* apparently, microsoft broke all the memory barrier stuff in Visual Studio 2008... */
    #pragma intrinsic(_ReadBarrier,_WriteBarrier,_ReadWriteBarrier)
    #define ECB_MEMORY_FENCE         _ReadWriteBarrier (); MemoryBarrier()
    #define ECB_MEMORY_FENCE_ACQUIRE _ReadWriteBarrier (); MemoryBarrier() /* according to msdn, _ReadBarrier is not a load fence */
    #define ECB_MEMORY_FENCE_RELEASE _WriteBarrier (); MemoryBarrier()
  #elif _MSC_VER >= 1400 /* VC++ 2005 */
    #pragma intrinsic(_ReadBarrier,_WriteBarrier,_ReadWriteBarrier)
    #define ECB_MEMORY_FENCE         _ReadWriteBarrier ()
    #define ECB_MEMORY_FENCE_ACQUIRE _ReadWriteBarrier () /* according to msdn, _ReadBarrier is not a load fence */
    #define ECB_MEMORY_FENCE_RELEASE _WriteBarrier ()
  #elif defined _WIN32
    #include <WinNT.h>
    #define ECB_MEMORY_FENCE         MemoryBarrier () /* actually just xchg on x86... scary */
  #elif __SUNPRO_C >= 0x5110 || __SUNPRO_CC >= 0x5110
    #include <mbarrier.h>
    #define ECB_MEMORY_FENCE         __machine_rw_barrier ()
    #define ECB_MEMORY_FENCE_ACQUIRE __machine_r_barrier  ()
    #define ECB_MEMORY_FENCE_RELEASE __machine_w_barrier  ()
  #elif __xlC__
    #define ECB_MEMORY_FENCE         __sync ()
  #endif
#endif

#ifndef ECB_MEMORY_FENCE
  #if ECB_C11 && !defined __STDC_NO_ATOMICS__
    /* we assume that these memory fences work on all variables/all memory accesses, */
    /* not just C11 atomics and atomic accesses */
    #include <stdatomic.h>
    /* Unfortunately, neither gcc 4.7 nor clang 3.1 generate any instructions for */
    /* any fence other than seq_cst, which isn't very efficient for us. */
    /* Why that is, we don't know - either the C11 memory model is quite useless */
    /* for most usages, or gcc and clang have a bug */
    /* I *currently* lean towards the latter, and inefficiently implement */
    /* all three of ecb's fences as a seq_cst fence */
    /* Update, gcc-4.8 generates mfence for all c++ fences, but nothing */
    /* for all __atomic_thread_fence's except seq_cst */
    #define ECB_MEMORY_FENCE         atomic_thread_fence (memory_order_seq_cst)
  #endif
#endif

#ifndef ECB_MEMORY_FENCE
  #if !ECB_AVOID_PTHREADS
    /*
     * if you get undefined symbol references to pthread_mutex_lock,
     * or failure to find pthread.h, then you should implement
     * the ECB_MEMORY_FENCE operations for your cpu/compiler
     * OR provide pthread.h and link against the posix thread library
     * of your system.
     */
    #include <pthread.h>
    #define ECB_NEEDS_PTHREADS 1
    #define ECB_MEMORY_FENCE_NEEDS_PTHREADS 1

    static pthread_mutex_t ecb_mf_lock = PTHREAD_MUTEX_INITIALIZER;
    #define ECB_MEMORY_FENCE do { pthread_mutex_lock (&ecb_mf_lock); pthread_mutex_unlock (&ecb_mf_lock); } while (0)
  #endif
#endif

#if !defined ECB_MEMORY_FENCE_ACQUIRE && defined ECB_MEMORY_FENCE
  #define ECB_MEMORY_FENCE_ACQUIRE ECB_MEMORY_FENCE
#endif

#if !defined ECB_MEMORY_FENCE_RELEASE && defined ECB_MEMORY_FENCE
  #define ECB_MEMORY_FENCE_RELEASE ECB_MEMORY_FENCE
#endif

/*****************************************************************************/

#if ECB_CPP
  #define ecb_inline static inline
#elif ECB_GCC_VERSION(2,5)
  #define ecb_inline static __inline__
#elif ECB_C99
  #define ecb_inline static inline
#else
  #define ecb_inline static
#endif

#if ECB_GCC_VERSION(3,3)
  #define ecb_restrict __restrict__
#elif ECB_C99
  #define ecb_restrict restrict
#else
  #define ecb_restrict
#endif

typedef int ecb_bool;

#define ECB_CONCAT_(a, b) a ## b
#define ECB_CONCAT(a, b) ECB_CONCAT_(a, b)
#define ECB_STRINGIFY_(a) # a
#define ECB_STRINGIFY(a) ECB_STRINGIFY_(a)
#define ECB_STRINGIFY_EXPR(expr) ((expr), ECB_STRINGIFY_ (expr))

#define ecb_function_ ecb_inline

#if ECB_GCC_VERSION(3,1) || ECB_CLANG_VERSION(2,8)
  #define ecb_attribute(attrlist)        __attribute__ (attrlist)
#else
  #define ecb_attribute(attrlist)
#endif

#if ECB_GCC_VERSION(3,1) || ECB_CLANG_BUILTIN(__builtin_constant_p)
  #define ecb_is_constant(expr)          __builtin_constant_p (expr)
#else
  /* possible C11 impl for integral types
  typedef struct ecb_is_constant_struct ecb_is_constant_struct;
  #define ecb_is_constant(expr)          _Generic ((1 ? (struct ecb_is_constant_struct *)0 : (void *)((expr) - (expr)), ecb_is_constant_struct *: 0, default: 1)) */

  #define ecb_is_constant(expr)          0
#endif

#if ECB_GCC_VERSION(3,1) || ECB_CLANG_BUILTIN(__builtin_expect)
  #define ecb_expect(expr,value)         __builtin_expect ((expr),(value))
#else
  #define ecb_expect(expr,value)         (expr)
#endif

#if ECB_GCC_VERSION(3,1) || ECB_CLANG_BUILTIN(__builtin_prefetch)
  #define ecb_prefetch(addr,rw,locality) __builtin_prefetch (addr, rw, locality)
#else
  #define ecb_prefetch(addr,rw,locality)
#endif

/* no emulation for ecb_decltype */
#if ECB_CPP11
  // older implementations might have problems with decltype(x)::type, work around it
  template<class T> struct ecb_decltype_t { typedef T type; };
  #define ecb_decltype(x) ecb_decltype_t<decltype (x)>::type
#elif ECB_GCC_VERSION(3,0) || ECB_CLANG_VERSION(2,8)
  #define ecb_decltype(x) __typeof__ (x)
#endif

#if _MSC_VER >= 1300
  #define ecb_deprecated __declspec (deprecated)
#else
  #define ecb_deprecated ecb_attribute ((__deprecated__))
#endif

#if _MSC_VER >= 1500
  #define ecb_deprecated_message(msg) __declspec (deprecated (msg))
#elif ECB_GCC_VERSION(4,5)
  #define ecb_deprecated_message(msg) ecb_attribute ((__deprecated__ (msg))
#else
  #define ecb_deprecated_message(msg) ecb_deprecated
#endif

#if _MSC_VER >= 1400
  #define ecb_noinline __declspec (noinline)
#else
  #define ecb_noinline ecb_attribute ((__noinline__))
#endif

#define ecb_unused     ecb_attribute ((__unused__))
#define ecb_const      ecb_attribute ((__const__))
#define ecb_pure       ecb_attribute ((__pure__))

#if ECB_C11 || __IBMC_NORETURN
  /* http://www-01.ibm.com/support/knowledgecenter/SSGH3R_13.1.0/com.ibm.xlcpp131.aix.doc/language_ref/noreturn.html */
  #define ecb_noreturn   _Noreturn
#elif ECB_CPP11
  #define ecb_noreturn   [[noreturn]]
#elif _MSC_VER >= 1200
  /* http://msdn.microsoft.com/en-us/library/k6ktzx3s.aspx */
  #define ecb_noreturn   __declspec (noreturn)
#else
  #define ecb_noreturn   ecb_attribute ((__noreturn__))
#endif

#if ECB_GCC_VERSION(4,3)
  #define ecb_artificial ecb_attribute ((__artificial__))
  #define ecb_hot        ecb_attribute ((__hot__))
  #define ecb_cold       ecb_attribute ((__cold__))
#else
  #define ecb_artificial
  #define ecb_hot
  #define ecb_cold
#endif

/* put around conditional expressions if you are very sure that the  */
/* expression is mostly true or mostly false. note that these return */
/* booleans, not the expression.                                     */
#define ecb_expect_false(expr) ecb_expect (!!(expr), 0)
#define ecb_expect_true(expr)  ecb_expect (!!(expr), 1)
/* for compatibility to the rest of the world */
#define ecb_likely(expr)   ecb_expect_true  (expr)
#define ecb_unlikely(expr) ecb_expect_false (expr)

/* count trailing zero bits and count # of one bits */
#if ECB_GCC_VERSION(3,4) \
    || (ECB_CLANG_BUILTIN(__builtin_clz) && ECB_CLANG_BUILTIN(__builtin_clzll) \
        && ECB_CLANG_BUILTIN(__builtin_ctz) && ECB_CLANG_BUILTIN(__builtin_ctzll) \
        && ECB_CLANG_BUILTIN(__builtin_popcount))
  /* we assume int == 32 bit, long == 32 or 64 bit and long long == 64 bit */
  #define ecb_ld32(x)      (__builtin_clz      (x) ^ 31)
  #define ecb_ld64(x)      (__builtin_clzll    (x) ^ 63)
  #define ecb_ctz32(x)      __builtin_ctz      (x)
  #define ecb_ctz64(x)      __builtin_ctzll    (x)
  #define ecb_popcount32(x) __builtin_popcount (x)
  /* no popcountll */
#else
  ecb_function_ ecb_const int ecb_ctz32 (uint32_t x);
  ecb_function_ ecb_const int
  ecb_ctz32 (uint32_t x)
  {
    int r = 0;

    x &= ~x + 1; /* this isolates the lowest bit */

#if ECB_branchless_on_i386
    r += !!(x & 0xaaaaaaaa) << 0;
    r += !!(x & 0xcccccccc) << 1;
    r += !!(x & 0xf0f0f0f0) << 2;
    r += !!(x & 0xff00ff00) << 3;
    r += !!(x & 0xffff0000) << 4;
#else
    if (x & 0xaaaaaaaa) r +=  1;
    if (x & 0xcccccccc) r +=  2;
    if (x & 0xf0f0f0f0) r +=  4;
    if (x & 0xff00ff00) r +=  8;
    if (x & 0xffff0000) r += 16;
#endif

    return r;
  }

  ecb_function_ ecb_const int ecb_ctz64 (uint64_t x);
  ecb_function_ ecb_const int
  ecb_ctz64 (uint64_t x)
  {
    int shift = x & 0xffffffffU ? 0 : 32;
    return ecb_ctz32 (x >> shift) + shift;
  }

  ecb_function_ ecb_const int ecb_popcount32 (uint32_t x);
  ecb_function_ ecb_const int
  ecb_popcount32 (uint32_t x)
  {
    x -=  (x >> 1) & 0x55555555;
    x  = ((x >> 2) & 0x33333333) + (x & 0x33333333);
    x  = ((x >> 4) + x) & 0x0f0f0f0f;
    x *= 0x01010101;

    return x >> 24;
  }

  ecb_function_ ecb_const int ecb_ld32 (uint32_t x);
  ecb_function_ ecb_const int ecb_ld32 (uint32_t x)
  {
    int r = 0;

    if (x >> 16) { x >>= 16; r += 16; }
    if (x >>  8) { x >>=  8; r +=  8; }
    if (x >>  4) { x >>=  4; r +=  4; }
    if (x >>  2) { x >>=  2; r +=  2; }
    if (x >>  1) {           r +=  1; }

    return r;
  }

  ecb_function_ ecb_const int ecb_ld64 (uint64_t x);
  ecb_function_ ecb_const int ecb_ld64 (uint64_t x)
  {
    int r = 0;

    if (x >> 32) { x >>= 32; r += 32; }

    return r + ecb_ld32 (x);
  }
#endif

ecb_function_ ecb_const ecb_bool ecb_is_pot32 (uint32_t x);
ecb_function_ ecb_const ecb_bool ecb_is_pot32 (uint32_t x) { return !(x & (x - 1)); }
ecb_function_ ecb_const ecb_bool ecb_is_pot64 (uint64_t x);
ecb_function_ ecb_const ecb_bool ecb_is_pot64 (uint64_t x) { return !(x & (x - 1)); }

ecb_function_ ecb_const uint8_t  ecb_bitrev8  (uint8_t  x);
ecb_function_ ecb_const uint8_t  ecb_bitrev8  (uint8_t  x)
{
  return (  (x * 0x0802U & 0x22110U)
          | (x * 0x8020U & 0x88440U)) * 0x10101U >> 16;
}

ecb_function_ ecb_const uint16_t ecb_bitrev16 (uint16_t x);
ecb_function_ ecb_const uint16_t ecb_bitrev16 (uint16_t x)
{
  x = ((x >>  1) &     0x5555) | ((x &     0x5555) <<  1);
  x = ((x >>  2) &     0x3333) | ((x &     0x3333) <<  2);
  x = ((x >>  4) &     0x0f0f) | ((x &     0x0f0f) <<  4);
  x = ( x >>  8              ) | ( x               <<  8);

  return x;
}

ecb_function_ ecb_const uint32_t ecb_bitrev32 (uint32_t x);
ecb_function_ ecb_const uint32_t ecb_bitrev32 (uint32_t x)
{
  x = ((x >>  1) & 0x55555555) | ((x & 0x55555555) <<  1);
  x = ((x >>  2) & 0x33333333) | ((x & 0x33333333) <<  2);
  x = ((x >>  4) & 0x0f0f0f0f) | ((x & 0x0f0f0f0f) <<  4);
  x = ((x >>  8) & 0x00ff00ff) | ((x & 0x00ff00ff) <<  8);
  x = ( x >> 16              ) | ( x               << 16);

  return x;
}

/* popcount64 is only available on 64 bit cpus as gcc builtin */
/* so for this version we are lazy */
ecb_function_ ecb_const int ecb_popcount64 (uint64_t x);
ecb_function_ ecb_const int
ecb_popcount64 (uint64_t x)
{
  return ecb_popcount32 (x) + ecb_popcount32 (x >> 32);
}

ecb_inline ecb_const uint8_t  ecb_rotl8  (uint8_t  x, unsigned int count);
ecb_inline ecb_const uint8_t  ecb_rotr8  (uint8_t  x, unsigned int count);
ecb_inline ecb_const uint16_t ecb_rotl16 (uint16_t x, unsigned int count);
ecb_inline ecb_const uint16_t ecb_rotr16 (uint16_t x, unsigned int count);
ecb_inline ecb_const uint32_t ecb_rotl32 (uint32_t x, unsigned int count);
ecb_inline ecb_const uint32_t ecb_rotr32 (uint32_t x, unsigned int count);
ecb_inline ecb_const uint64_t ecb_rotl64 (uint64_t x, unsigned int count);
ecb_inline ecb_const uint64_t ecb_rotr64 (uint64_t x, unsigned int count);

ecb_inline ecb_const uint8_t  ecb_rotl8  (uint8_t  x, unsigned int count) { return (x >> ( 8 - count)) | (x << count); }
ecb_inline ecb_const uint8_t  ecb_rotr8  (uint8_t  x, unsigned int count) { return (x << ( 8 - count)) | (x >> count); }
ecb_inline ecb_const uint16_t ecb_rotl16 (uint16_t x, unsigned int count) { return (x >> (16 - count)) | (x << count); }
ecb_inline ecb_const uint16_t ecb_rotr16 (uint16_t x, unsigned int count) { return (x << (16 - count)) | (x >> count); }
ecb_inline ecb_const uint32_t ecb_rotl32 (uint32_t x, unsigned int count) { return (x >> (32 - count)) | (x << count); }
ecb_inline ecb_const uint32_t ecb_rotr32 (uint32_t x, unsigned int count) { return (x << (32 - count)) | (x >> count); }
ecb_inline ecb_const uint64_t ecb_rotl64 (uint64_t x, unsigned int count) { return (x >> (64 - count)) | (x << count); }
ecb_inline ecb_const uint64_t ecb_rotr64 (uint64_t x, unsigned int count) { return (x << (64 - count)) | (x >> count); }

#if ECB_GCC_VERSION(4,3) || (ECB_CLANG_BUILTIN(__builtin_bswap32) && ECB_CLANG_BUILTIN(__builtin_bswap64))
  #if ECB_GCC_VERSION(4,8) || ECB_CLANG_BUILTIN(__builtin_bswap16)
  #define ecb_bswap16(x)  __builtin_bswap16 (x)
  #else
  #define ecb_bswap16(x) (__builtin_bswap32 (x) >> 16)
  #endif
  #define ecb_bswap32(x)  __builtin_bswap32 (x)
  #define ecb_bswap64(x)  __builtin_bswap64 (x)
#elif _MSC_VER
  #include <stdlib.h>
  #define ecb_bswap16(x) ((uint16_t)_byteswap_ushort ((uint16_t)(x)))
  #define ecb_bswap32(x) ((uint32_t)_byteswap_ulong  ((uint32_t)(x)))
  #define ecb_bswap64(x) ((uint64_t)_byteswap_uint64 ((uint64_t)(x)))
#else
  ecb_function_ ecb_const uint16_t ecb_bswap16 (uint16_t x);
  ecb_function_ ecb_const uint16_t
  ecb_bswap16 (uint16_t x)
  {
    return ecb_rotl16 (x, 8);
  }

  ecb_function_ ecb_const uint32_t ecb_bswap32 (uint32_t x);
  ecb_function_ ecb_const uint32_t
  ecb_bswap32 (uint32_t x)
  {
    return (((uint32_t)ecb_bswap16 (x)) << 16) | ecb_bswap16 (x >> 16);
  }

  ecb_function_ ecb_const uint64_t ecb_bswap64 (uint64_t x);
  ecb_function_ ecb_const uint64_t
  ecb_bswap64 (uint64_t x)
  {
    return (((uint64_t)ecb_bswap32 (x)) << 32) | ecb_bswap32 (x >> 32);
  }
#endif

#if ECB_GCC_VERSION(4,5) || ECB_CLANG_BUILTIN(__builtin_unreachable)
  #define ecb_unreachable() __builtin_unreachable ()
#else
  /* this seems to work fine, but gcc always emits a warning for it :/ */
  ecb_inline ecb_noreturn void ecb_unreachable (void);
  ecb_inline ecb_noreturn void ecb_unreachable (void) { }
#endif

/* try to tell the compiler that some condition is definitely true */
#define ecb_assume(cond) if (!(cond)) ecb_unreachable (); else 0

ecb_inline ecb_const unsigned char ecb_byteorder_helper (void);
ecb_inline ecb_const unsigned char
ecb_byteorder_helper (void)
{
  /* the union code still generates code under pressure in gcc, */
  /* but less than using pointers, and always seems to */
  /* successfully return a constant. */
  /* the reason why we have this horrible preprocessor mess */
  /* is to avoid it in all cases, at least on common architectures */
  /* or when using a recent enough gcc version (>= 4.6) */
#if ((__i386 || __i386__) && !__VOS__) || _M_IX86 || ECB_GCC_AMD64 || ECB_MSVC_AMD64
  return 0x44;
#elif __BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return 0x44;
#elif __BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return 0x11;
#else
  union
  {
    uint32_t i;
    uint8_t c;
  } u = { 0x11223344 };
  return u.c;
#endif
}

ecb_inline ecb_const ecb_bool ecb_big_endian    (void);
ecb_inline ecb_const ecb_bool ecb_big_endian    (void) { return ecb_byteorder_helper () == 0x11; }
ecb_inline ecb_const ecb_bool ecb_little_endian (void);
ecb_inline ecb_const ecb_bool ecb_little_endian (void) { return ecb_byteorder_helper () == 0x44; }

#if ECB_GCC_VERSION(3,0) || ECB_C99
  #define ecb_mod(m,n) ((m) % (n) + ((m) % (n) < 0 ? (n) : 0))
#else
  #define ecb_mod(m,n) ((m) < 0 ? ((n) - 1 - ((-1 - (m)) % (n))) : ((m) % (n)))
#endif

#if ECB_CPP
  template<typename T>
  static inline T ecb_div_rd (T val, T div)
  {
    return val < 0 ? - ((-val + div - 1) / div) : (val          ) / div;
  }
  template<typename T>
  static inline T ecb_div_ru (T val, T div)
  {
    return val < 0 ? - ((-val          ) / div) : (val + div - 1) / div;
  }
#else
  #define ecb_div_rd(val,div) ((val) < 0 ? - ((-(val) + (div) - 1) / (div)) : ((val)            ) / (div))
  #define ecb_div_ru(val,div) ((val) < 0 ? - ((-(val)            ) / (div)) : ((val) + (div) - 1) / (div))
#endif

#if ecb_cplusplus_does_not_suck
  /* does not work for local types (http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2657.htm) */
  template<typename T, int N>
  static inline int ecb_array_length (const T (&arr)[N])
  {
    return N;
  }
#else
  #define ecb_array_length(name) (sizeof (name) / sizeof (name [0]))
#endif

/*******************************************************************************/
/* floating point stuff, can be disabled by defining ECB_NO_LIBM */

/* basically, everything uses "ieee pure-endian" floating point numbers */
/* the only noteworthy exception is ancient armle, which uses order 43218765 */
#if 0 \
    || __i386 || __i386__ \
    || ECB_GCC_AMD64 \
    || __powerpc__ || __ppc__ || __powerpc64__ || __ppc64__ \
    || defined __s390__ || defined __s390x__ \
    || defined __mips__ \
    || defined __alpha__ \
    || defined __hppa__ \
    || defined __ia64__ \
    || defined __m68k__ \
    || defined __m88k__ \
    || defined __sh__ \
    || defined _M_IX86 || defined ECB_MSVC_AMD64 || defined _M_IA64 \
    || (defined __arm__ && (defined __ARM_EABI__ || defined __EABI__ || defined __VFP_FP__ || defined _WIN32_WCE || defined __ANDROID__)) \
    || defined __aarch64__
  #define ECB_STDFP 1
  #include <string.h> /* for memcpy */
#else
  #define ECB_STDFP 0
#endif

#ifndef ECB_NO_LIBM

  #include <math.h> /* for frexp*, ldexp*, INFINITY, NAN */

  /* only the oldest of old doesn't have this one. solaris. */
  #ifdef INFINITY
    #define ECB_INFINITY INFINITY
  #else
    #define ECB_INFINITY HUGE_VAL
  #endif

  #ifdef NAN
    #define ECB_NAN NAN
  #else
    #define ECB_NAN ECB_INFINITY
  #endif

  #if ECB_C99 || _XOPEN_VERSION >= 600 || _POSIX_VERSION >= 200112L
    #define ecb_ldexpf(x,e) ldexpf ((x), (e))
    #define ecb_frexpf(x,e) frexpf ((x), (e))
  #else
    #define ecb_ldexpf(x,e) (float) ldexp ((double) (x), (e))
    #define ecb_frexpf(x,e) (float) frexp ((double) (x), (e))
  #endif

  /* converts an ieee half/binary16 to a float */
  ecb_function_ ecb_const float ecb_binary16_to_float (uint16_t x);
  ecb_function_ ecb_const float
  ecb_binary16_to_float (uint16_t x)
  {
    int e = (x >> 10) & 0x1f;
    int m = x & 0x3ff;
    float r;

    if      (!e     ) r = ecb_ldexpf (m        ,    -24);
    else if (e != 31) r = ecb_ldexpf (m + 0x400, e - 25);
    else if (m      ) r = ECB_NAN;
    else              r = ECB_INFINITY;

    return x & 0x8000 ? -r : r;
  }

  /* convert a float to ieee single/binary32 */
  ecb_function_ ecb_const uint32_t ecb_float_to_binary32 (float x);
  ecb_function_ ecb_const uint32_t
  ecb_float_to_binary32 (float x)
  {
    uint32_t r;

    #if ECB_STDFP
      memcpy (&r, &x, 4);
    #else
      /* slow emulation, works for anything but -0 */
      uint32_t m;
      int e;

      if (x == 0e0f                    ) return 0x00000000U;
      if (x > +3.40282346638528860e+38f) return 0x7f800000U;
      if (x < -3.40282346638528860e+38f) return 0xff800000U;
      if (x != x                       ) return 0x7fbfffffU;

      m = ecb_frexpf (x, &e) * 0x1000000U;

      r = m & 0x80000000U;

      if (r)
        m = -m;

      if (e <= -126)
        {
          m &= 0xffffffU;
          m >>= (-125 - e);
          e = -126;
        }

      r |= (e + 126) << 23;
      r |= m & 0x7fffffU;
    #endif

    return r;
  }

  /* converts an ieee single/binary32 to a float */
  ecb_function_ ecb_const float ecb_binary32_to_float (uint32_t x);
  ecb_function_ ecb_const float
  ecb_binary32_to_float (uint32_t x)
  {
    float r;

    #if ECB_STDFP
      memcpy (&r, &x, 4);
    #else
      /* emulation, only works for normals and subnormals and +0 */
      int neg = x >> 31;
      int e = (x >> 23) & 0xffU;

      x &= 0x7fffffU;

      if (e)
        x |= 0x800000U;
      else
        e = 1;

      /* we distrust ldexpf a bit and do the 2**-24 scaling by an extra multiply */
      r = ecb_ldexpf (x * (0.5f / 0x800000U), e - 126);

      r = neg ? -r : r;
    #endif

    return r;
  }

  /* convert a double to ieee double/binary64 */
  ecb_function_ ecb_const uint64_t ecb_double_to_binary64 (double x);
  ecb_function_ ecb_const uint64_t
  ecb_double_to_binary64 (double x)
  {
    uint64_t r;

    #if ECB_STDFP
      memcpy (&r, &x, 8);
    #else
      /* slow emulation, works for anything but -0 */
      uint64_t m;
      int e;

      if (x == 0e0                     ) return 0x0000000000000000U;
      if (x > +1.79769313486231470e+308) return 0x7ff0000000000000U;
      if (x < -1.79769313486231470e+308) return 0xfff0000000000000U;
      if (x != x                       ) return 0X7ff7ffffffffffffU;

      m = frexp (x, &e) * 0x20000000000000U;

      r = m & 0x8000000000000000;;

      if (r)
        m = -m;

      if (e <= -1022)
        {
          m &= 0x1fffffffffffffU;
          m >>= (-1021 - e);
          e = -1022;
        }

      r |= ((uint64_t)(e + 1022)) << 52;
      r |= m & 0xfffffffffffffU;
    #endif

    return r;
  }

  /* converts an ieee double/binary64 to a double */
  ecb_function_ ecb_const double ecb_binary64_to_double (uint64_t x);
  ecb_function_ ecb_const double
  ecb_binary64_to_double (uint64_t x)
  {
    double r;

    #if ECB_STDFP
      memcpy (&r, &x, 8);
    #else
      /* emulation, only works for normals and subnormals and +0 */
      int neg = x >> 63;
      int e = (x >> 52) & 0x7ffU;

      x &= 0xfffffffffffffU;

      if (e)
        x |= 0x10000000000000U;
      else
        e = 1;

      /* we distrust ldexp a bit and do the 2**-53 scaling by an extra multiply */
      r = ldexp (x * (0.5 / 0x10000000000000U), e - 1022);

      r = neg ? -r : r;
    #endif

    return r;
  }

#endif

#endif

