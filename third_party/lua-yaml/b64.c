#include <lua.h>
#include <lauxlib.h>

#include "b64.h"

int frombase64(lua_State *L, const unsigned char *str, unsigned int len) {
    int d = 0, dlast = 0, phase = 0;
    unsigned char c;
    static int table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
    };
   luaL_Buffer b;

   luaL_buffinit(L, &b);
   for (; len--; ++str) {
      d = table[(int)*str];
      if (d == -1) continue;
      switch(phase) {
         case 0:
            ++phase;
            break;
         case 1:
            c = ((dlast << 2) | ((d & 0x30) >> 4));
            luaL_addchar(&b, c);
            ++phase;
            break;
         case 2:
            c = (((dlast & 0xf) << 4) | ((d & 0x3c) >> 2));
            luaL_addchar(&b, c);
            ++phase;
            break;
         case 3:
            c = (((dlast & 0x03 ) << 6) | d);
            luaL_addchar(&b, c);
            phase = 0;
            break;
      }
      dlast = d;
   }
   luaL_pushresult(&b);
   return 1;
}

static void b64_encode(luaL_Buffer *b, unsigned int c1, unsigned int c2, unsigned int c3, int n) {
   static const char code[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   unsigned long tuple = c3 + 256UL * (c2 + 256UL * c1);
   int i;
   char s[4];

   for (i = 0; i < 4; i++) {
      s[3-i] = code[tuple % 64];
      tuple /= 64;
   }
   for (i = n+1; i < 4; i++) s[i] = '=';
   luaL_addlstring(b, s, 4);
}

int tobase64(lua_State *L, int pos) {
   size_t l;
   const unsigned char *s = (const unsigned char*)luaL_checklstring(L, pos, &l);
   luaL_Buffer b;
   int n;

   luaL_buffinit(L, &b);
   for (n = l / 3; n--; s += 3)
      b64_encode(&b, s[0], s[1], s[2], 3);

   switch (l % 3) {
      case 1:
         b64_encode(&b, s[0], 0, 0, 1);
         break;
      case 2:
         b64_encode(&b, s[0], s[1], 0, 2);
         break;
   }
   luaL_pushresult(&b);
   return 1;
}
