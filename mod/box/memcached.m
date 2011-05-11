
#line 1 "memcached.rl"
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#include <errcode.h>
#include <salloc.h>
#include <palloc.h>
#include <fiber.h>
#include <util.h>
#include <pickle.h>
#include "say.h"

#include <tarantool.h>
#include <cfg/tarantool_box_cfg.h>
#include <mod/box/box.h>
#include <stat.h>


#define STAT(_)					\
        _(MEMC_GET, 1)				\
        _(MEMC_GET_MISS, 2)			\
	_(MEMC_GET_HIT, 3)			\
	_(MEMC_EXPIRED_KEYS, 4)

ENUM(memcached_stat, STAT);
STRS(memcached_stat, STAT);
int stat_base;

struct index *memcached_index;

/* memcached tuple format:
   <key, meta, data> */

struct meta {
	u32 exptime;
	u32 flags;
	u64 cas;
} __packed__;


#line 72 "memcached.c"
static const char _memcached_actions[] = {
	0, 1, 11, 1, 12, 1, 13, 1, 
	14, 1, 15, 1, 16, 1, 17, 1, 
	18, 1, 22, 1, 23, 1, 24, 1, 
	25, 1, 26, 1, 27, 1, 28, 3, 
	21, 20, 5, 3, 21, 20, 6, 3, 
	21, 20, 7, 3, 21, 20, 8, 3, 
	21, 20, 9, 3, 21, 20, 10, 4, 
	13, 21, 20, 6, 4, 17, 21, 20, 
	5, 4, 18, 21, 20, 8, 4, 21, 
	19, 20, 0, 4, 21, 19, 20, 1, 
	4, 21, 19, 20, 2, 4, 21, 19, 
	20, 3, 4, 21, 19, 20, 4, 4, 
	22, 21, 20, 5, 4, 22, 21, 20, 
	6, 4, 22, 21, 20, 8, 5, 15, 
	21, 19, 20, 0, 5, 15, 21, 19, 
	20, 1, 5, 15, 21, 19, 20, 2, 
	5, 15, 21, 19, 20, 4, 5, 16, 
	21, 19, 20, 3, 5, 22, 21, 19, 
	20, 0, 5, 22, 21, 19, 20, 1, 
	5, 22, 21, 19, 20, 2, 5, 22, 
	21, 19, 20, 3, 5, 22, 21, 19, 
	20, 4
};

static const short _memcached_key_offsets[] = {
	0, 0, 10, 12, 13, 14, 18, 19, 
	22, 25, 28, 31, 34, 39, 40, 42, 
	43, 44, 45, 46, 47, 48, 50, 51, 
	52, 53, 54, 55, 59, 60, 63, 66, 
	69, 72, 75, 80, 81, 83, 84, 85, 
	86, 87, 88, 89, 91, 92, 93, 94, 
	98, 99, 102, 105, 108, 111, 114, 117, 
	120, 125, 126, 130, 131, 132, 133, 134, 
	135, 136, 139, 142, 143, 145, 146, 147, 
	151, 152, 155, 160, 161, 165, 166, 167, 
	168, 169, 170, 171, 174, 177, 178, 179, 
	180, 181, 185, 188, 189, 195, 200, 204, 
	205, 206, 207, 208, 209, 210, 213, 216, 
	217, 218, 219, 220, 221, 222, 223, 224, 
	227, 228, 234, 239, 243, 244, 245, 246, 
	247, 248, 249, 252, 255, 256, 257, 259, 
	263, 266, 267, 271, 272, 273, 274, 275, 
	276, 277, 278, 279, 280, 281, 282, 283, 
	284, 285, 286, 288, 289, 290, 291, 292, 
	293, 294, 295, 296, 300, 301, 304, 307, 
	310, 313, 316, 321, 322, 324, 325, 326, 
	327, 328, 329, 330, 332, 334, 335, 336, 
	340, 341, 344, 347, 350, 353, 356, 361, 
	362, 364, 365, 366, 367, 368, 369, 370, 
	372, 373, 374, 375, 377, 378
};

static const char _memcached_trans_keys[] = {
	97, 99, 100, 102, 103, 105, 112, 113, 
	114, 115, 100, 112, 100, 32, 13, 32, 
	9, 10, 32, 32, 48, 57, 32, 48, 
	57, 32, 48, 57, 32, 48, 57, 32, 
	48, 57, 10, 13, 32, 48, 57, 10, 
	32, 110, 111, 114, 101, 112, 108, 121, 
	10, 13, 112, 101, 110, 100, 32, 13, 
	32, 9, 10, 32, 32, 48, 57, 32, 
	48, 57, 32, 48, 57, 32, 48, 57, 
	32, 48, 57, 10, 13, 32, 48, 57, 
	10, 32, 110, 111, 114, 101, 112, 108, 
	121, 10, 13, 97, 115, 32, 13, 32, 
	9, 10, 32, 32, 48, 57, 32, 48, 
	57, 32, 48, 57, 32, 48, 57, 32, 
	48, 57, 32, 48, 57, 32, 48, 57, 
	10, 13, 32, 48, 57, 10, 10, 13, 
	32, 110, 111, 114, 101, 112, 108, 121, 
	10, 13, 32, 10, 13, 32, 101, 99, 
	108, 114, 32, 13, 32, 9, 10, 32, 
	32, 48, 57, 10, 13, 32, 48, 57, 
	10, 10, 13, 32, 110, 111, 114, 101, 
	112, 108, 121, 10, 13, 32, 10, 13, 
	32, 101, 116, 101, 32, 13, 32, 9, 
	10, 10, 13, 32, 10, 10, 13, 32, 
	110, 48, 57, 10, 13, 32, 48, 57, 
	10, 13, 32, 110, 111, 114, 101, 112, 
	108, 121, 10, 13, 32, 10, 13, 32, 
	108, 117, 115, 104, 95, 97, 108, 108, 
	10, 13, 32, 10, 10, 13, 32, 110, 
	48, 57, 10, 13, 32, 48, 57, 10, 
	13, 32, 110, 111, 114, 101, 112, 108, 
	121, 10, 13, 32, 10, 13, 32, 101, 
	116, 32, 115, 13, 32, 9, 10, 10, 
	13, 32, 10, 9, 10, 13, 32, 32, 
	110, 99, 114, 32, 114, 101, 112, 101, 
	110, 100, 32, 117, 105, 116, 10, 13, 
	10, 101, 112, 108, 97, 99, 101, 32, 
	13, 32, 9, 10, 32, 32, 48, 57, 
	32, 48, 57, 32, 48, 57, 32, 48, 
	57, 32, 48, 57, 10, 13, 32, 48, 
	57, 10, 32, 110, 111, 114, 101, 112, 
	108, 121, 10, 13, 101, 116, 116, 32, 
	13, 32, 9, 10, 32, 32, 48, 57, 
	32, 48, 57, 32, 48, 57, 32, 48, 
	57, 32, 48, 57, 10, 13, 32, 48, 
	57, 10, 32, 110, 111, 114, 101, 112, 
	108, 121, 10, 13, 97, 116, 115, 10, 
	13, 10, 0
};

static const char _memcached_single_lengths[] = {
	0, 10, 2, 1, 1, 2, 1, 1, 
	1, 1, 1, 1, 3, 1, 2, 1, 
	1, 1, 1, 1, 1, 2, 1, 1, 
	1, 1, 1, 2, 1, 1, 1, 1, 
	1, 1, 3, 1, 2, 1, 1, 1, 
	1, 1, 1, 2, 1, 1, 1, 2, 
	1, 1, 1, 1, 1, 1, 1, 1, 
	3, 1, 4, 1, 1, 1, 1, 1, 
	1, 3, 3, 1, 2, 1, 1, 2, 
	1, 1, 3, 1, 4, 1, 1, 1, 
	1, 1, 1, 3, 3, 1, 1, 1, 
	1, 2, 3, 1, 4, 3, 4, 1, 
	1, 1, 1, 1, 1, 3, 3, 1, 
	1, 1, 1, 1, 1, 1, 1, 3, 
	1, 4, 3, 4, 1, 1, 1, 1, 
	1, 1, 3, 3, 1, 1, 2, 2, 
	3, 1, 4, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 2, 1, 1, 1, 1, 1, 
	1, 1, 1, 2, 1, 1, 1, 1, 
	1, 1, 3, 1, 2, 1, 1, 1, 
	1, 1, 1, 2, 2, 1, 1, 2, 
	1, 1, 1, 1, 1, 1, 3, 1, 
	2, 1, 1, 1, 1, 1, 1, 2, 
	1, 1, 1, 2, 1, 0
};

static const char _memcached_range_lengths[] = {
	0, 0, 0, 0, 0, 1, 0, 1, 
	1, 1, 1, 1, 1, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 1, 0, 1, 1, 1, 
	1, 1, 1, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 1, 
	0, 1, 1, 1, 1, 1, 1, 1, 
	1, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 1, 
	0, 1, 1, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 1, 0, 0, 1, 1, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 1, 1, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 1, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 1, 0, 1, 1, 1, 
	1, 1, 1, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 1, 
	0, 1, 1, 1, 1, 1, 1, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0
};

static const short _memcached_index_offsets[] = {
	0, 0, 11, 14, 16, 18, 22, 24, 
	27, 30, 33, 36, 39, 44, 46, 49, 
	51, 53, 55, 57, 59, 61, 64, 66, 
	68, 70, 72, 74, 78, 80, 83, 86, 
	89, 92, 95, 100, 102, 105, 107, 109, 
	111, 113, 115, 117, 120, 122, 124, 126, 
	130, 132, 135, 138, 141, 144, 147, 150, 
	153, 158, 160, 165, 167, 169, 171, 173, 
	175, 177, 181, 185, 187, 190, 192, 194, 
	198, 200, 203, 208, 210, 215, 217, 219, 
	221, 223, 225, 227, 231, 235, 237, 239, 
	241, 243, 247, 251, 253, 259, 264, 269, 
	271, 273, 275, 277, 279, 281, 285, 289, 
	291, 293, 295, 297, 299, 301, 303, 305, 
	309, 311, 317, 322, 327, 329, 331, 333, 
	335, 337, 339, 343, 347, 349, 351, 354, 
	358, 362, 364, 369, 371, 373, 375, 377, 
	379, 381, 383, 385, 387, 389, 391, 393, 
	395, 397, 399, 402, 404, 406, 408, 410, 
	412, 414, 416, 418, 422, 424, 427, 430, 
	433, 436, 439, 444, 446, 449, 451, 453, 
	455, 457, 459, 461, 464, 467, 469, 471, 
	475, 477, 480, 483, 486, 489, 492, 497, 
	499, 502, 504, 506, 508, 510, 512, 514, 
	517, 519, 521, 523, 526, 528
};

static const unsigned char _memcached_trans_targs[] = {
	2, 44, 67, 103, 124, 132, 136, 143, 
	148, 172, 0, 3, 22, 0, 4, 0, 
	5, 0, 0, 5, 0, 6, 7, 0, 
	7, 8, 0, 9, 8, 0, 9, 10, 
	0, 11, 10, 0, 11, 12, 0, 197, 
	13, 14, 12, 0, 197, 0, 14, 15, 
	0, 16, 0, 17, 0, 18, 0, 19, 
	0, 20, 0, 21, 0, 197, 13, 0, 
	23, 0, 24, 0, 25, 0, 26, 0, 
	27, 0, 0, 27, 0, 28, 29, 0, 
	29, 30, 0, 31, 30, 0, 31, 32, 
	0, 33, 32, 0, 33, 34, 0, 197, 
	35, 36, 34, 0, 197, 0, 36, 37, 
	0, 38, 0, 39, 0, 40, 0, 41, 
	0, 42, 0, 43, 0, 197, 35, 0, 
	45, 0, 46, 0, 47, 0, 0, 47, 
	0, 48, 49, 0, 49, 50, 0, 51, 
	50, 0, 51, 52, 0, 53, 52, 0, 
	53, 54, 0, 55, 54, 0, 55, 56, 
	0, 197, 57, 58, 56, 0, 197, 0, 
	197, 57, 58, 59, 0, 60, 0, 61, 
	0, 62, 0, 63, 0, 64, 0, 65, 
	0, 197, 57, 66, 0, 197, 57, 66, 
	0, 68, 0, 69, 85, 0, 70, 0, 
	71, 0, 0, 71, 0, 72, 73, 0, 
	73, 74, 0, 197, 75, 76, 74, 0, 
	197, 0, 197, 75, 76, 77, 0, 78, 
	0, 79, 0, 80, 0, 81, 0, 82, 
	0, 83, 0, 197, 75, 84, 0, 197, 
	75, 84, 0, 86, 0, 87, 0, 88, 
	0, 89, 0, 0, 89, 0, 90, 197, 
	91, 92, 0, 197, 0, 197, 91, 92, 
	95, 93, 0, 197, 91, 94, 93, 0, 
	197, 91, 94, 95, 0, 96, 0, 97, 
	0, 98, 0, 99, 0, 100, 0, 101, 
	0, 197, 91, 102, 0, 197, 91, 102, 
	0, 104, 0, 105, 0, 106, 0, 107, 
	0, 108, 0, 109, 0, 110, 0, 111, 
	0, 197, 112, 113, 0, 197, 0, 197, 
	112, 113, 116, 114, 0, 197, 112, 115, 
	114, 0, 197, 112, 115, 116, 0, 117, 
	0, 118, 0, 119, 0, 120, 0, 121, 
	0, 122, 0, 197, 112, 123, 0, 197, 
	112, 123, 0, 125, 0, 126, 0, 127, 
	131, 0, 0, 127, 0, 128, 197, 129, 
	130, 0, 197, 0, 0, 197, 129, 130, 
	128, 127, 0, 133, 0, 134, 0, 135, 
	0, 71, 0, 137, 0, 138, 0, 139, 
	0, 140, 0, 141, 0, 142, 0, 27, 
	0, 144, 0, 145, 0, 146, 0, 197, 
	147, 0, 197, 0, 149, 0, 150, 0, 
	151, 0, 152, 0, 153, 0, 154, 0, 
	155, 0, 0, 155, 0, 156, 157, 0, 
	157, 158, 0, 159, 158, 0, 159, 160, 
	0, 161, 160, 0, 161, 162, 0, 197, 
	163, 164, 162, 0, 197, 0, 164, 165, 
	0, 166, 0, 167, 0, 168, 0, 169, 
	0, 170, 0, 171, 0, 197, 163, 0, 
	173, 192, 0, 174, 0, 175, 0, 0, 
	175, 0, 176, 177, 0, 177, 178, 0, 
	179, 178, 0, 179, 180, 0, 181, 180, 
	0, 181, 182, 0, 197, 183, 184, 182, 
	0, 197, 0, 184, 185, 0, 186, 0, 
	187, 0, 188, 0, 189, 0, 190, 0, 
	191, 0, 197, 183, 0, 193, 0, 194, 
	0, 195, 0, 197, 196, 0, 197, 0, 
	0, 0
};

static const unsigned char _memcached_trans_actions[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 3, 0, 0, 
	0, 1, 0, 7, 0, 0, 0, 1, 
	0, 5, 0, 0, 0, 1, 0, 116, 
	9, 9, 0, 0, 75, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 146, 17, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	19, 0, 0, 0, 0, 3, 0, 0, 
	0, 1, 0, 7, 0, 0, 0, 1, 
	0, 5, 0, 0, 0, 1, 0, 128, 
	9, 9, 0, 0, 90, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 164, 17, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 3, 0, 0, 0, 1, 0, 7, 
	0, 0, 0, 1, 0, 5, 0, 0, 
	0, 1, 0, 9, 0, 0, 0, 1, 
	0, 134, 11, 11, 0, 0, 85, 0, 
	85, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 158, 17, 17, 0, 85, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	29, 0, 0, 0, 0, 3, 0, 0, 
	0, 1, 0, 60, 13, 13, 0, 0, 
	31, 0, 31, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 95, 17, 17, 0, 31, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 3, 35, 
	0, 0, 0, 35, 0, 35, 0, 0, 
	0, 1, 0, 55, 5, 5, 0, 0, 
	35, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 100, 17, 17, 0, 35, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 43, 0, 0, 0, 43, 0, 43, 
	0, 0, 0, 1, 0, 65, 15, 15, 
	0, 0, 43, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 105, 17, 17, 0, 43, 
	0, 0, 0, 0, 0, 0, 0, 23, 
	0, 0, 0, 0, 0, 3, 39, 0, 
	0, 0, 39, 0, 0, 39, 0, 0, 
	3, 25, 0, 0, 0, 0, 0, 0, 
	0, 27, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 21, 
	0, 0, 0, 0, 0, 0, 0, 51, 
	0, 0, 51, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 3, 0, 0, 
	0, 1, 0, 7, 0, 0, 0, 1, 
	0, 5, 0, 0, 0, 1, 0, 122, 
	9, 9, 0, 0, 80, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 152, 17, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 3, 0, 0, 0, 1, 0, 
	7, 0, 0, 0, 1, 0, 5, 0, 
	0, 0, 1, 0, 110, 9, 9, 0, 
	0, 70, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 140, 17, 0, 0, 0, 0, 
	0, 0, 0, 47, 0, 0, 47, 0, 
	0, 0
};

static const int memcached_start = 1;
static const int memcached_first_final = 197;
static const int memcached_error = 0;

static const int memcached_en_main = 1;


#line 71 "memcached.rl"



static u64
natoq(const u8 *start, const u8 *end)
{
	u64 num = 0;
	while (start < end)
		num = num * 10 + (*start++ - '0');
	return num;
}

static int
store(struct box_txn *txn, void *key, u32 exptime, u32 flags, u32 bytes, u8 *data)
{
	u32 box_flags = BOX_QUIET, cardinality = 4;
	static u64 cas = 42;
	struct meta m;

	struct tbuf *req = tbuf_alloc(fiber->pool);

	tbuf_append(req, &cfg.memcached_namespace, sizeof(u32));
	tbuf_append(req, &box_flags, sizeof(box_flags));
	tbuf_append(req, &cardinality, sizeof(cardinality));

	tbuf_append_field(req, key);

	m.exptime = exptime;
	m.flags = flags;
	m.cas = cas++;
	write_varint32(req, sizeof(m));
	tbuf_append(req, &m, sizeof(m));

	char b[43];
	sprintf(b, " %"PRIu32" %"PRIu32"\r\n", flags, bytes);
	write_varint32(req, strlen(b));
	tbuf_append(req, b, strlen(b));

	write_varint32(req, bytes);
	tbuf_append(req, data, bytes);

	int key_len = load_varint32(&key);
	say_debug("memcached/store key:(%i)'%.*s' exptime:%"PRIu32" flags:%"PRIu32" cas:%"PRIu64,
		  key_len, key_len, (u8 *)key, exptime, flags, cas);
	return box_process(txn, INSERT, req); /* FIXME: handle RW/RO */
}

static int
delete(struct box_txn *txn, void *key)
{
	u32 key_len = 1;
	struct tbuf *req = tbuf_alloc(fiber->pool);

	tbuf_append(req, &cfg.memcached_namespace, sizeof(u32));
	tbuf_append(req, &key_len, sizeof(key_len));
	tbuf_append_field(req, key);

	return box_process(txn, DELETE, req);
}

static struct box_tuple *
find(void *key)
{
	return memcached_index->find(memcached_index, key);
}

static struct meta *
meta(struct box_tuple *tuple)
{
	void *field = tuple_field(tuple, 1);
	return field + 1;
}

static bool
expired(struct box_tuple *tuple)
{
	struct meta *m = meta(tuple);
 	return m->exptime == 0 ? 0 : m->exptime < ev_now();
}

static bool
is_numeric(void *field, u32 value_len)
{
	for (int i = 0; i < value_len; i++)
		if (*((u8 *)field + i) < '0' || '9' < *((u8 *)field + i))
			return false;
	return true;
}

static struct stats {
	u64 total_items;
	u32 curr_connections;
	u32 total_connections;
	u64 cmd_get;
	u64 cmd_set;
	u64 get_hits;
	u64 get_misses;
	u64 evictions;
	u64 bytes_read;
	u64 bytes_written;
} stats;

static void
print_stats()
{
	u64 bytes_used, items;
	struct tbuf *out = tbuf_alloc(fiber->pool);
	slab_stat2(&bytes_used, &items);

	tbuf_printf(out, "STAT pid %"PRIu32"\r\n", (u32)getpid());
	tbuf_printf(out, "STAT uptime %"PRIu32"\r\n", (u32)tarantool_uptime());
	tbuf_printf(out, "STAT time %"PRIu32"\r\n", (u32)ev_now());
	tbuf_printf(out, "STAT version 1.2.5 (tarantool/box)\r\n");
	tbuf_printf(out, "STAT pointer_size %"PRI_SZ"\r\n", sizeof(void *)*8);
	tbuf_printf(out, "STAT curr_items %"PRIu64"\r\n", items);
	tbuf_printf(out, "STAT total_items %"PRIu64"\r\n", stats.total_items);
	tbuf_printf(out, "STAT bytes %"PRIu64"\r\n", bytes_used);
	tbuf_printf(out, "STAT curr_connections %"PRIu32"\r\n", stats.curr_connections);
	tbuf_printf(out, "STAT total_connections %"PRIu32"\r\n", stats.total_connections);
	tbuf_printf(out, "STAT connection_structures %"PRIu32"\r\n", stats.curr_connections); /* lie a bit */
	tbuf_printf(out, "STAT cmd_get %"PRIu64"\r\n", stats.cmd_get);
	tbuf_printf(out, "STAT cmd_set %"PRIu64"\r\n", stats.cmd_set);
	tbuf_printf(out, "STAT get_hits %"PRIu64"\r\n", stats.get_hits);
	tbuf_printf(out, "STAT get_misses %"PRIu64"\r\n", stats.get_misses);
	tbuf_printf(out, "STAT evictions %"PRIu64"\r\n", stats.evictions);
	tbuf_printf(out, "STAT bytes_read %"PRIu64"\r\n", stats.bytes_read);
	tbuf_printf(out, "STAT bytes_written %"PRIu64"\r\n", stats.bytes_written);
	tbuf_printf(out, "STAT limit_maxbytes %"PRIu64"\r\n", (u64)(cfg.slab_alloc_arena * (1 << 30)));
	tbuf_printf(out, "STAT threads 1\r\n");
	tbuf_printf(out, "END\r\n");
	add_iov(out->data, out->len);
}

static void
flush_all(void *data)
{
	uintptr_t delay = (uintptr_t)data;
	fiber_sleep(delay - ev_now());
	khash_t(lstr_ptr_map) *map = memcached_index->idx.str_hash;
	for (khiter_t i = kh_begin(map); i != kh_end(map); i++) {
		if (kh_exist(map, i)) {
			struct box_tuple *tuple = kh_value(map, i);
			meta(tuple)->exptime = 1;
		}
	}
}


static int __attribute__((noinline))
memcached_dispatch(struct box_txn *txn)
{
	int cs;
	u8 *p, *pe;
	u8 *fstart;
	struct tbuf *keys = tbuf_alloc(fiber->pool);
	void *key;
	bool append, show_cas;
	int incr_sign;
	u64 cas, incr;
	u32 flags, exptime, bytes;
	bool noreply = false;
	u8 *data = NULL;
	bool done = false;
	int r;
	size_t saved_iov_cnt = fiber->iov_cnt;
	uintptr_t flush_delay = 0;
	size_t keys_count = 0;

	p = fiber->rbuf->data;
	pe = fiber->rbuf->data + fiber->rbuf->len;

	say_debug("memcached_dispatch '%.*s'", MIN((int)(pe - p), 40) , p);

#define STORE ({									\
	stats.cmd_set++;								\
	if (bytes > (1<<20)) {								\
		add_iov("SERVER_ERROR object too large for cache\r\n", 41);		\
	} else {									\
		u32 ret_code;								\
		if ((ret_code = store(txn, key, exptime, flags, bytes, data)) == 0) {	\
			stats.total_items++;						\
			add_iov("STORED\r\n", 8);					\
		} else {								\
			add_iov("SERVER_ERROR ", 13);					\
			add_iov(ERRCODE_DESC(error_codes, ret_code),			\
				strlen(ERRCODE_DESC(error_codes, ret_code)));		\
			add_iov("\r\n", 2);						\
		}									\
	}										\
})

	
#line 601 "memcached.c"
	{
	cs = memcached_start;
	}

#line 606 "memcached.c"
	{
	int _klen;
	unsigned int _trans;
	const char *_acts;
	unsigned int _nacts;
	const char *_keys;

	if ( p == pe )
		goto _test_eof;
	if ( cs == 0 )
		goto _out;
_resume:
	_keys = _memcached_trans_keys + _memcached_key_offsets[cs];
	_trans = _memcached_index_offsets[cs];

	_klen = _memcached_single_lengths[cs];
	if ( _klen > 0 ) {
		const char *_lower = _keys;
		const char *_mid;
		const char *_upper = _keys + _klen - 1;
		while (1) {
			if ( _upper < _lower )
				break;

			_mid = _lower + ((_upper-_lower) >> 1);
			if ( (*p) < *_mid )
				_upper = _mid - 1;
			else if ( (*p) > *_mid )
				_lower = _mid + 1;
			else {
				_trans += (_mid - _keys);
				goto _match;
			}
		}
		_keys += _klen;
		_trans += _klen;
	}

	_klen = _memcached_range_lengths[cs];
	if ( _klen > 0 ) {
		const char *_lower = _keys;
		const char *_mid;
		const char *_upper = _keys + (_klen<<1) - 2;
		while (1) {
			if ( _upper < _lower )
				break;

			_mid = _lower + (((_upper-_lower) >> 1) & ~1);
			if ( (*p) < _mid[0] )
				_upper = _mid - 2;
			else if ( (*p) > _mid[1] )
				_lower = _mid + 2;
			else {
				_trans += ((_mid - _keys)>>1);
				goto _match;
			}
		}
		_trans += _klen;
	}

_match:
	cs = _memcached_trans_targs[_trans];

	if ( _memcached_trans_actions[_trans] == 0 )
		goto _again;

	_acts = _memcached_actions + _memcached_trans_actions[_trans];
	_nacts = (unsigned int) *_acts++;
	while ( _nacts-- > 0 )
	{
		switch ( *_acts++ )
		{
	case 0:
#line 263 "memcached.rl"
	{
			key = read_field(keys);
			STORE;
		}
	break;
	case 1:
#line 268 "memcached.rl"
	{
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple != NULL && !expired(tuple))
				add_iov("NOT_STORED\r\n", 12);
			else
				STORE;
		}
	break;
	case 2:
#line 277 "memcached.rl"
	{
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				add_iov("NOT_STORED\r\n", 12);
			else
				STORE;
		}
	break;
	case 3:
#line 286 "memcached.rl"
	{
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				add_iov("NOT_FOUND\r\n", 11);
			else if (meta(tuple)->cas != cas)
				add_iov("EXISTS\r\n", 8);
			else
				STORE;
		}
	break;
	case 4:
#line 297 "memcached.rl"
	{
			struct tbuf *b;
			void *value;
			u32 value_len;

			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || tuple->flags & GHOST) {
				add_iov("NOT_STORED\r\n", 12);
			} else {
				value = tuple_field(tuple, 3);
				value_len = load_varint32(&value);
				b = tbuf_alloc(fiber->pool);
				if (append) {
					tbuf_append(b, value, value_len);
					tbuf_append(b, data, bytes);
				} else {
					tbuf_append(b, data, bytes);
					tbuf_append(b, value, value_len);
				}

				bytes += value_len;
				data = b->data;
				STORE;
			}
		}
	break;
	case 5:
#line 324 "memcached.rl"
	{
			struct meta *m;
			struct tbuf *b;
			void *field;
			u32 value_len;
			u64 value;

			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || tuple->flags & GHOST || expired(tuple)) {
				add_iov("NOT_FOUND\r\n", 11);
			} else {
				m = meta(tuple);
				field = tuple_field(tuple, 3);
				value_len = load_varint32(&field);

				if (is_numeric(field, value_len)) {
					value = natoq(field, field + value_len);

					if (incr_sign > 0) {
						value += incr;
					} else {
						if (incr > value)
							value = 0;
						else
							value -= incr;
					}

					exptime = m->exptime;
					flags = m->flags;

					b = tbuf_alloc(fiber->pool);
					tbuf_printf(b, "%"PRIu64, value);
					data = b->data;
					bytes = b->len;

					stats.cmd_set++;
					if (store(txn, key, exptime, flags, bytes, data) == 0) {
						stats.total_items++;
						add_iov(b->data, b->len);
						add_iov("\r\n", 2);
					} else {
						add_iov("SERVER_ERROR\r\n", 14);
					}
				} else {
					add_iov("CLIENT_ERROR cannot increment or decrement non-numeric value\r\n", 62);
				}
			}

		}
	break;
	case 6:
#line 375 "memcached.rl"
	{
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || tuple->flags & GHOST || expired(tuple)) {
				add_iov("NOT_FOUND\r\n", 11);
			} else {
				u32 ret_code;
				if ((ret_code = delete(txn, key)) == 0)
					add_iov("DELETED\r\n", 9);
				else {
					add_iov("SERVER_ERROR ", 13);
					add_iov(ERRCODE_DESC(error_codes, ret_code),
						strlen(ERRCODE_DESC(error_codes,ret_code)));
					add_iov("\r\n", 2);
				}
			}
		}
	break;
	case 7:
#line 393 "memcached.rl"
	{
			txn->op = SELECT;
			fiber_register_cleanup((void *)txn_cleanup, txn);
			stat_collect(stat_base, MEMC_GET, 1);
			stats.cmd_get++;
			say_debug("ensuring space for %"PRI_SZ" keys", keys_count);
			iov_ensure(keys_count * 5 + 1);
			while (keys_count-- > 0) {
				struct box_tuple *tuple;
				struct meta *m;
				void *field;
				void *value;
				void *suffix;
				u32 key_len;
				u32 value_len;
				u32 suffix_len;
				u32 _l;

				key = read_field(keys);
				tuple = find(key);
				key_len = load_varint32(&key);

				if (tuple == NULL || tuple->flags & GHOST) {
					stat_collect(stat_base, MEMC_GET_MISS, 1);
					stats.get_misses++;
					continue;
				}

				field = tuple->data;

				/* skip key */
				_l = load_varint32(&field);
				field += _l;

				/* metainfo */
				_l = load_varint32(&field);
				m = field;
				field += _l;

				/* suffix */
				suffix_len = load_varint32(&field);
				suffix = field;
				field += suffix_len;

				/* value */
				value_len = load_varint32(&field);
				value = field;

				if (m->exptime > 0 && m->exptime < ev_now()) {
					stats.get_misses++;
					stat_collect(stat_base, MEMC_GET_MISS, 1);
					continue;
				} else {
					stats.get_hits++;
					stat_collect(stat_base, MEMC_GET_HIT, 1);
				}

				tuple_txn_ref(txn, tuple);

				if (show_cas) {
					struct tbuf *b = tbuf_alloc(fiber->pool);
					tbuf_printf(b, "VALUE %.*s %"PRIu32" %"PRIu32" %"PRIu64"\r\n", key_len, (u8 *)key, m->flags, value_len, m->cas);
					add_iov_unsafe(b->data, b->len);
					stats.bytes_written += b->len;
				} else {
					add_iov_unsafe("VALUE ", 6);
					add_iov_unsafe(key, key_len);
					add_iov_unsafe(suffix, suffix_len);
				}
				add_iov_unsafe(value, value_len);
				add_iov_unsafe("\r\n", 2);
				stats.bytes_written += value_len + 2;
			}
			add_iov_unsafe("END\r\n", 5);
			stats.bytes_written += 5;
		}
	break;
	case 8:
#line 470 "memcached.rl"
	{
			if (flush_delay > 0) {
				struct fiber *f = fiber_create("flush_all", -1, -1, flush_all, (void *)flush_delay);
				if (f)
					fiber_call(f);
			} else
				flush_all((void *)0);
			add_iov("OK\r\n", 4);
		}
	break;
	case 9:
#line 480 "memcached.rl"
	{
			print_stats();
		}
	break;
	case 10:
#line 484 "memcached.rl"
	{
			return 0;
		}
	break;
	case 11:
#line 488 "memcached.rl"
	{ fstart = p; }
	break;
	case 12:
#line 489 "memcached.rl"
	{
			fstart = p;
			for (; p < pe && *p != ' ' && *p != '\r' && *p != '\n'; p++);
			if ( *p == ' ' || *p == '\r' || *p == '\n') {
				write_varint32(keys, p - fstart);
				tbuf_append(keys, fstart, p - fstart);
				keys_count++;
				p--;
			} else
				p = fstart;
 		}
	break;
	case 13:
#line 505 "memcached.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	break;
	case 14:
#line 512 "memcached.rl"
	{flags = natoq(fstart, p);}
	break;
	case 15:
#line 513 "memcached.rl"
	{bytes = natoq(fstart, p);}
	break;
	case 16:
#line 514 "memcached.rl"
	{cas = natoq(fstart, p);}
	break;
	case 17:
#line 515 "memcached.rl"
	{incr = natoq(fstart, p);}
	break;
	case 18:
#line 516 "memcached.rl"
	{flush_delay = natoq(fstart, p);}
	break;
	case 19:
#line 518 "memcached.rl"
	{
			size_t parsed = p - (u8 *)fiber->rbuf->data;
			while (fiber->rbuf->len - parsed < bytes + 2) {
				if ((r = fiber_bread(fiber->rbuf, bytes + 2 - (pe - p))) <= 0) {
					say_debug("read returned %i, closing connection", r);
					return 0;
				}
			}

			p = fiber->rbuf->data + parsed;
			pe = fiber->rbuf->data + fiber->rbuf->len;

			data = p;

			if (strncmp((char *)(p + bytes), "\r\n", 2) == 0) {
				p += bytes + 2;
			} else {
				goto exit;
			}
		}
	break;
	case 20:
#line 539 "memcached.rl"
	{
			done = true;
			stats.bytes_read += p - (u8 *)fiber->rbuf->data;
			tbuf_peek(fiber->rbuf, p - (u8 *)fiber->rbuf->data);
		}
	break;
	case 21:
#line 545 "memcached.rl"
	{ p++; }
	break;
	case 22:
#line 547 "memcached.rl"
	{ noreply = true; }
	break;
	case 23:
#line 553 "memcached.rl"
	{append = true; }
	break;
	case 24:
#line 554 "memcached.rl"
	{append = false;}
	break;
	case 25:
#line 558 "memcached.rl"
	{show_cas = false;}
	break;
	case 26:
#line 559 "memcached.rl"
	{show_cas = true;}
	break;
	case 27:
#line 561 "memcached.rl"
	{incr_sign = 1; }
	break;
	case 28:
#line 562 "memcached.rl"
	{incr_sign = -1;}
	break;
#line 1035 "memcached.c"
		}
	}

_again:
	if ( cs == 0 )
		goto _out;
	if ( ++p != pe )
		goto _resume;
	_test_eof: {}
	_out: {}
	}

#line 572 "memcached.rl"


	if (!done) {
		say_debug("parse failed after: `%.*s'", (int)(pe - p), p);
		if (pe - p > (1 << 20)) {
		exit:
			say_warn("memcached proto error");
			add_iov("ERROR\r\n", 7);
			stats.bytes_written += 7;
			return -1;
		}
		char *r;
		if ((r = memmem(p, pe - p, "\r\n", 2)) != NULL) {
			tbuf_peek(fiber->rbuf, r + 2 - (char *)fiber->rbuf->data);
			add_iov("CLIENT_ERROR bad command line format\r\n", 38);
			return 1;
		}
		return 0;
	}

	if (noreply) {
		fiber->iov_cnt = saved_iov_cnt;
		fiber->iov->len = saved_iov_cnt * sizeof(struct iovec);
	}

	return 1;
}

void
memcached_handler(void *_data __attribute__((unused)))
{
	struct box_txn *txn;
	stats.total_connections++;
	stats.curr_connections++;
	int r, p;
	int batch_count;

	for (;;) {
		batch_count = 0;
		if ((r = fiber_bread(fiber->rbuf, 1)) <= 0) {
			say_debug("read returned %i, closing connection", r);
			goto exit;
		}

	dispatch:
		txn = txn_alloc(BOX_QUIET);
		p = memcached_dispatch(txn);
		if (p < 0) {
			say_debug("negative dispatch, closing connection");
			goto exit;
		}

		if (p == 0 && batch_count == 0) /* we havn't successfully parsed any requests */
			continue;

		if (p == 1) {
			batch_count++;
			/* some unparsed commands remain and batch count less than 20 */
			if (fiber->rbuf->len > 0 && batch_count < 20)
				goto dispatch;
		}

		r = fiber_flush_output();
		if (r < 0) {
			say_debug("flush_output failed, closing connection");
			goto exit;
		}

		stats.bytes_written += r;
		fiber_gc();

		if (p == 1 && fiber->rbuf->len > 0) {
			batch_count = 0;
			goto dispatch;
		}
	}
exit:
        fiber_flush_output();
	fiber_sleep(0.01);
	say_debug("exit");
	stats.curr_connections--; /* FIXME: nonlocal exit via exception will leak this counter */
}

void
memcached_init(void)
{
	stat_base = stat_register(memcached_stat_strs, memcached_stat_MAX);
}

void
memcached_expire(void *data __attribute__((unused)))
{
	static khiter_t i;
	khash_t(lstr_ptr_map) *map = memcached_index->idx.str_hash;

	say_info("memcached expire fiber started");
	for (;;) {
		if (i > kh_end(map))
			i = kh_begin(map);

		struct tbuf *keys_to_delete = tbuf_alloc(fiber->pool);
		int expired_keys = 0;

		for (int j = 0; j < cfg.memcached_expire_per_loop; j++, i++) {
			if (i == kh_end(map)) {
				i = kh_begin(map);
				break;
			}

			if (!kh_exist(map, i))
				continue;

			struct box_tuple *tuple = kh_value(map, i);

			if (!expired(tuple))
				continue;

			say_debug("expire tuple %p", tuple);
			tbuf_append_field(keys_to_delete, tuple->data);
		}

		while (keys_to_delete->len > 0) {
			struct box_txn *txn = txn_alloc(BOX_QUIET);
			delete(txn, read_field(keys_to_delete));
			expired_keys++;
		}
		stat_collect(stat_base, MEMC_EXPIRED_KEYS, expired_keys);

		fiber_gc();

		double delay = (double)cfg.memcached_expire_per_loop * cfg.memcached_expire_full_sweep / (map->size + 1);
		if (delay > 1)
			delay = 1;
		fiber_sleep(delay);
	}
}

/*
 * Local Variables:
 * mode: c
 * End:
 * vim: syntax=c
 */
