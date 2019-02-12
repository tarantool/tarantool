/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

/*
 *
 * This file contains inline asm code for retrieving "high-performance"
 * counters for x86 class CPUs.
 */
#ifndef SQL_HWTIME_H
#define SQL_HWTIME_H

/*
 * The following routine only works on pentium-class (or newer) processors.
 * It uses the RDTSC opcode to read the cycle count value out of the
 * processor and returns that value.  This can be used for high-res
 * profiling.
 */
#if (defined(__GNUC__) || defined(_MSC_VER)) && \
      (defined(i386) || defined(__i386__) || defined(_M_IX86))

#if defined(__GNUC__)

__inline__ sql_uint64
sqlHwtime(void)
{
	unsigned int lo, hi;
	__asm__ __volatile__("rdtsc":"=a"(lo), "=d"(hi));
	return (sql_uint64) hi << 32 | lo;
}

#elif defined(_MSC_VER)

__declspec(naked)
__inline sql_uint64 __cdecl
sqlHwtime(void)
{
	__asm {
		rdtsc ret;
 return value at EDX:EAX}
}

#endif

#elif (defined(__GNUC__) && defined(__x86_64__))

__inline__ sql_uint64
sqlHwtime(void)
{
	unsigned long val;
	__asm__ __volatile__("rdtsc":"=A"(val));
	return val;
}

#elif (defined(__GNUC__) && defined(__ppc__))

__inline__ sql_uint64
sqlHwtime(void)
{
	unsigned long long retval;
	unsigned long junk;
	__asm__ __volatile__("\n\
          1:      mftbu   %1\n\
                  mftb    %L0\n\
                  mftbu   %0\n\
                  cmpw    %0,%1\n\
                  bne     1b":"=r"(retval), "=r"(junk));
	return retval;
}

#else

#error Need implementation of sqlHwtime() for your platform.

  /*
   * To compile without implementing sqlHwtime() for your platform,
   * you can remove the above #error and use the following
   * stub function.  You will lose timing support for many
   * of the debugging and testing utilities, but it should at
   * least compile and run.
   */
sql_uint64
sqlHwtime(void)
{
	return ((sql_uint64) 0);
}

#endif

#endif				/* !defined(SQL_HWTIME_H) */
