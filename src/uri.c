
#line 1 "src/uri.rl"
/*
* Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "uri.h"
#include <trivia/util.h> /* SNPRINT */
#include <string.h>
#include <stdio.h> /* snprintf */
int
uri_parse(struct uri *uri, const char *p)
{
	const char *pe = p + strlen(p);
	const char *eof = pe;
	int cs;
	memset(uri, 0, sizeof(*uri));
	
	if (p == pe)
	return -1;
	
	const char *s = NULL, *login = NULL, *scheme = NULL;
	size_t login_len = 0, scheme_len = 0;
	
	
	static const int uri_start = 144;
	static const int uri_first_final = 144;
	static const int uri_error = 0;
	
	static const int uri_en_main = 144;
	
	
	{
		cs = (int)uri_start;
	}
	
	{
		if ( p == pe )
		goto _test_eof;
		switch ( cs )
		{
			case 144:
			goto st_case_144;
			case 0:
			goto st_case_0;
			case 145:
			goto st_case_145;
			case 146:
			goto st_case_146;
			case 147:
			goto st_case_147;
			case 1:
			goto st_case_1;
			case 2:
			goto st_case_2;
			case 3:
			goto st_case_3;
			case 4:
			goto st_case_4;
			case 5:
			goto st_case_5;
			case 6:
			goto st_case_6;
			case 7:
			goto st_case_7;
			case 8:
			goto st_case_8;
			case 9:
			goto st_case_9;
			case 10:
			goto st_case_10;
			case 148:
			goto st_case_148;
			case 11:
			goto st_case_11;
			case 12:
			goto st_case_12;
			case 13:
			goto st_case_13;
			case 14:
			goto st_case_14;
			case 15:
			goto st_case_15;
			case 149:
			goto st_case_149;
			case 150:
			goto st_case_150;
			case 16:
			goto st_case_16;
			case 17:
			goto st_case_17;
			case 18:
			goto st_case_18;
			case 19:
			goto st_case_19;
			case 20:
			goto st_case_20;
			case 151:
			goto st_case_151;
			case 21:
			goto st_case_21;
			case 22:
			goto st_case_22;
			case 23:
			goto st_case_23;
			case 24:
			goto st_case_24;
			case 25:
			goto st_case_25;
			case 26:
			goto st_case_26;
			case 27:
			goto st_case_27;
			case 152:
			goto st_case_152;
			case 28:
			goto st_case_28;
			case 29:
			goto st_case_29;
			case 30:
			goto st_case_30;
			case 31:
			goto st_case_31;
			case 32:
			goto st_case_32;
			case 153:
			goto st_case_153;
			case 154:
			goto st_case_154;
			case 155:
			goto st_case_155;
			case 156:
			goto st_case_156;
			case 157:
			goto st_case_157;
			case 33:
			goto st_case_33;
			case 34:
			goto st_case_34;
			case 35:
			goto st_case_35;
			case 36:
			goto st_case_36;
			case 37:
			goto st_case_37;
			case 158:
			goto st_case_158;
			case 159:
			goto st_case_159;
			case 160:
			goto st_case_160;
			case 161:
			goto st_case_161;
			case 162:
			goto st_case_162;
			case 163:
			goto st_case_163;
			case 164:
			goto st_case_164;
			case 165:
			goto st_case_165;
			case 166:
			goto st_case_166;
			case 167:
			goto st_case_167;
			case 168:
			goto st_case_168;
			case 169:
			goto st_case_169;
			case 170:
			goto st_case_170;
			case 171:
			goto st_case_171;
			case 172:
			goto st_case_172;
			case 38:
			goto st_case_38;
			case 39:
			goto st_case_39;
			case 40:
			goto st_case_40;
			case 41:
			goto st_case_41;
			case 42:
			goto st_case_42;
			case 43:
			goto st_case_43;
			case 44:
			goto st_case_44;
			case 45:
			goto st_case_45;
			case 46:
			goto st_case_46;
			case 47:
			goto st_case_47;
			case 48:
			goto st_case_48;
			case 49:
			goto st_case_49;
			case 50:
			goto st_case_50;
			case 51:
			goto st_case_51;
			case 52:
			goto st_case_52;
			case 53:
			goto st_case_53;
			case 54:
			goto st_case_54;
			case 55:
			goto st_case_55;
			case 56:
			goto st_case_56;
			case 57:
			goto st_case_57;
			case 58:
			goto st_case_58;
			case 59:
			goto st_case_59;
			case 60:
			goto st_case_60;
			case 61:
			goto st_case_61;
			case 62:
			goto st_case_62;
			case 63:
			goto st_case_63;
			case 64:
			goto st_case_64;
			case 65:
			goto st_case_65;
			case 66:
			goto st_case_66;
			case 67:
			goto st_case_67;
			case 68:
			goto st_case_68;
			case 69:
			goto st_case_69;
			case 70:
			goto st_case_70;
			case 71:
			goto st_case_71;
			case 72:
			goto st_case_72;
			case 73:
			goto st_case_73;
			case 74:
			goto st_case_74;
			case 75:
			goto st_case_75;
			case 76:
			goto st_case_76;
			case 77:
			goto st_case_77;
			case 78:
			goto st_case_78;
			case 79:
			goto st_case_79;
			case 80:
			goto st_case_80;
			case 81:
			goto st_case_81;
			case 82:
			goto st_case_82;
			case 173:
			goto st_case_173;
			case 83:
			goto st_case_83;
			case 84:
			goto st_case_84;
			case 85:
			goto st_case_85;
			case 86:
			goto st_case_86;
			case 87:
			goto st_case_87;
			case 88:
			goto st_case_88;
			case 89:
			goto st_case_89;
			case 90:
			goto st_case_90;
			case 91:
			goto st_case_91;
			case 92:
			goto st_case_92;
			case 93:
			goto st_case_93;
			case 94:
			goto st_case_94;
			case 95:
			goto st_case_95;
			case 96:
			goto st_case_96;
			case 97:
			goto st_case_97;
			case 98:
			goto st_case_98;
			case 99:
			goto st_case_99;
			case 100:
			goto st_case_100;
			case 101:
			goto st_case_101;
			case 102:
			goto st_case_102;
			case 103:
			goto st_case_103;
			case 174:
			goto st_case_174;
			case 175:
			goto st_case_175;
			case 176:
			goto st_case_176;
			case 177:
			goto st_case_177;
			case 178:
			goto st_case_178;
			case 179:
			goto st_case_179;
			case 180:
			goto st_case_180;
			case 104:
			goto st_case_104;
			case 105:
			goto st_case_105;
			case 106:
			goto st_case_106;
			case 107:
			goto st_case_107;
			case 108:
			goto st_case_108;
			case 181:
			goto st_case_181;
			case 109:
			goto st_case_109;
			case 110:
			goto st_case_110;
			case 111:
			goto st_case_111;
			case 112:
			goto st_case_112;
			case 113:
			goto st_case_113;
			case 182:
			goto st_case_182;
			case 183:
			goto st_case_183;
			case 184:
			goto st_case_184;
			case 185:
			goto st_case_185;
			case 186:
			goto st_case_186;
			case 187:
			goto st_case_187;
			case 114:
			goto st_case_114;
			case 115:
			goto st_case_115;
			case 116:
			goto st_case_116;
			case 117:
			goto st_case_117;
			case 118:
			goto st_case_118;
			case 188:
			goto st_case_188;
			case 189:
			goto st_case_189;
			case 190:
			goto st_case_190;
			case 191:
			goto st_case_191;
			case 192:
			goto st_case_192;
			case 193:
			goto st_case_193;
			case 194:
			goto st_case_194;
			case 195:
			goto st_case_195;
			case 196:
			goto st_case_196;
			case 197:
			goto st_case_197;
			case 198:
			goto st_case_198;
			case 199:
			goto st_case_199;
			case 200:
			goto st_case_200;
			case 201:
			goto st_case_201;
			case 202:
			goto st_case_202;
			case 203:
			goto st_case_203;
			case 204:
			goto st_case_204;
			case 205:
			goto st_case_205;
			case 206:
			goto st_case_206;
			case 207:
			goto st_case_207;
			case 208:
			goto st_case_208;
			case 209:
			goto st_case_209;
			case 119:
			goto st_case_119;
			case 120:
			goto st_case_120;
			case 121:
			goto st_case_121;
			case 122:
			goto st_case_122;
			case 123:
			goto st_case_123;
			case 210:
			goto st_case_210;
			case 211:
			goto st_case_211;
			case 124:
			goto st_case_124;
			case 125:
			goto st_case_125;
			case 126:
			goto st_case_126;
			case 127:
			goto st_case_127;
			case 128:
			goto st_case_128;
			case 212:
			goto st_case_212;
			case 213:
			goto st_case_213;
			case 129:
			goto st_case_129;
			case 130:
			goto st_case_130;
			case 131:
			goto st_case_131;
			case 132:
			goto st_case_132;
			case 133:
			goto st_case_133;
			case 214:
			goto st_case_214;
			case 215:
			goto st_case_215;
			case 216:
			goto st_case_216;
			case 217:
			goto st_case_217;
			case 218:
			goto st_case_218;
			case 219:
			goto st_case_219;
			case 220:
			goto st_case_220;
			case 221:
			goto st_case_221;
			case 222:
			goto st_case_222;
			case 223:
			goto st_case_223;
			case 224:
			goto st_case_224;
			case 225:
			goto st_case_225;
			case 226:
			goto st_case_226;
			case 227:
			goto st_case_227;
			case 228:
			goto st_case_228;
			case 229:
			goto st_case_229;
			case 230:
			goto st_case_230;
			case 231:
			goto st_case_231;
			case 232:
			goto st_case_232;
			case 233:
			goto st_case_233;
			case 234:
			goto st_case_234;
			case 235:
			goto st_case_235;
			case 236:
			goto st_case_236;
			case 237:
			goto st_case_237;
			case 238:
			goto st_case_238;
			case 239:
			goto st_case_239;
			case 240:
			goto st_case_240;
			case 241:
			goto st_case_241;
			case 242:
			goto st_case_242;
			case 243:
			goto st_case_243;
			case 244:
			goto st_case_244;
			case 245:
			goto st_case_245;
			case 246:
			goto st_case_246;
			case 247:
			goto st_case_247;
			case 248:
			goto st_case_248;
			case 249:
			goto st_case_249;
			case 250:
			goto st_case_250;
			case 251:
			goto st_case_251;
			case 252:
			goto st_case_252;
			case 253:
			goto st_case_253;
			case 254:
			goto st_case_254;
			case 255:
			goto st_case_255;
			case 256:
			goto st_case_256;
			case 257:
			goto st_case_257;
			case 258:
			goto st_case_258;
			case 259:
			goto st_case_259;
			case 134:
			goto st_case_134;
			case 135:
			goto st_case_135;
			case 136:
			goto st_case_136;
			case 137:
			goto st_case_137;
			case 138:
			goto st_case_138;
			case 260:
			goto st_case_260;
			case 139:
			goto st_case_139;
			case 140:
			goto st_case_140;
			case 141:
			goto st_case_141;
			case 142:
			goto st_case_142;
			case 143:
			goto st_case_143;
			case 261:
			goto st_case_261;
			case 262:
			goto st_case_262;
			case 263:
			goto st_case_263;
			case 264:
			goto st_case_264;
			case 265:
			goto st_case_265;
		}
		goto st_out;
		st_case_144:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr150;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto ctr152;
			}
			case 47: {
				goto ctr153;
			}
			case 59: {
				goto ctr150;
			}
			case 61: {
				goto ctr150;
			}
			case 63: {
				goto ctr155;
			}
			case 64: {
				goto st204;
			}
			case 91: {
				goto st38;
			}
			case 95: {
				goto ctr150;
			}
			case 117: {
				goto ctr158;
			}
			case 126: {
				goto ctr150;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto ctr150;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr157;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr157;
			}
		} else {
			goto ctr154;
		}
		{
			goto st0;
		}
		st_case_0:
		st0:
		cs = 0;
		goto _out;
		ctr150:
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st145;
		st145:
		p+= 1;
		if ( p == pe )
		goto _test_eof145;
		st_case_145:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st145;
			}
		} else {
			goto st145;
		}
		{
			goto st0;
		}
		ctr151:
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr159:
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr170:
		{
			#line 71 "src/uri.rl"
			s = p; }
		{
			#line 72 "src/uri.rl"
			uri->query = s; uri->query_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr172:
		{
			#line 72 "src/uri.rl"
			uri->query = s; uri->query_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr175:
		{
			#line 133 "src/uri.rl"
			s = p; }
		{
			#line 134 "src/uri.rl"
			uri->service = s; uri->service_len = p - s; }
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr186:
		{
			#line 134 "src/uri.rl"
			uri->service = s; uri->service_len = p - s; }
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr201:
		{
			#line 103 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 1; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr210:
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr316:
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 130 "src/uri.rl"
			s = p;}
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr320:
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		ctr325:
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st146;
		st146:
		p+= 1;
		if ( p == pe )
		goto _test_eof146;
		st_case_146:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr165;
			}
			case 37: {
				goto ctr166;
			}
			case 61: {
				goto ctr165;
			}
			case 95: {
				goto ctr165;
			}
			case 124: {
				goto ctr165;
			}
			case 126: {
				goto ctr165;
			}
		}
		if ( ( (*( p))) < 63 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto ctr165;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto ctr165;
			}
		} else {
			goto ctr165;
		}
		{
			goto st0;
		}
		ctr165:
		{
			#line 75 "src/uri.rl"
			s = p; }
		
		goto st147;
		st147:
		p+= 1;
		if ( p == pe )
		goto _test_eof147;
		st_case_147:
		switch( ( (*( p))) ) {
			case 33: {
				goto st147;
			}
			case 37: {
				goto st1;
			}
			case 61: {
				goto st147;
			}
			case 95: {
				goto st147;
			}
			case 124: {
				goto st147;
			}
			case 126: {
				goto st147;
			}
		}
		if ( ( (*( p))) < 63 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st147;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st147;
			}
		} else {
			goto st147;
		}
		{
			goto st0;
		}
		ctr166:
		{
			#line 75 "src/uri.rl"
			s = p; }
		
		goto st1;
		st1:
		p+= 1;
		if ( p == pe )
		goto _test_eof1;
		st_case_1:
		switch( ( (*( p))) ) {
			case 37: {
				goto st147;
			}
			case 117: {
				goto st2;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st147;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st147;
			}
		} else {
			goto st147;
		}
		{
			goto st0;
		}
		st2:
		p+= 1;
		if ( p == pe )
		goto _test_eof2;
		st_case_2:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st3;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st3;
			}
		} else {
			goto st3;
		}
		{
			goto st0;
		}
		st3:
		p+= 1;
		if ( p == pe )
		goto _test_eof3;
		st_case_3:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st4;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st4;
			}
		} else {
			goto st4;
		}
		{
			goto st0;
		}
		st4:
		p+= 1;
		if ( p == pe )
		goto _test_eof4;
		st_case_4:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st5;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st5;
			}
		} else {
			goto st5;
		}
		{
			goto st0;
		}
		st5:
		p+= 1;
		if ( p == pe )
		goto _test_eof5;
		st_case_5:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st147;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st147;
			}
		} else {
			goto st147;
		}
		{
			goto st0;
		}
		ctr152:
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st6;
		st6:
		p+= 1;
		if ( p == pe )
		goto _test_eof6;
		st_case_6:
		switch( ( (*( p))) ) {
			case 37: {
				goto st145;
			}
			case 117: {
				goto st7;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st145;
			}
		} else {
			goto st145;
		}
		{
			goto st0;
		}
		st7:
		p+= 1;
		if ( p == pe )
		goto _test_eof7;
		st_case_7:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st8;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st8;
			}
		} else {
			goto st8;
		}
		{
			goto st0;
		}
		st8:
		p+= 1;
		if ( p == pe )
		goto _test_eof8;
		st_case_8:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st9;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st9;
			}
		} else {
			goto st9;
		}
		{
			goto st0;
		}
		st9:
		p+= 1;
		if ( p == pe )
		goto _test_eof9;
		st_case_9:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st10;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st10;
			}
		} else {
			goto st10;
		}
		{
			goto st0;
		}
		st10:
		p+= 1;
		if ( p == pe )
		goto _test_eof10;
		st_case_10:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st145;
			}
		} else {
			goto st145;
		}
		{
			goto st0;
		}
		ctr161:
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st148;
		ctr177:
		{
			#line 133 "src/uri.rl"
			s = p; }
		{
			#line 134 "src/uri.rl"
			uri->service = s; uri->service_len = p - s; }
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st148;
		ctr187:
		{
			#line 134 "src/uri.rl"
			uri->service = s; uri->service_len = p - s; }
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st148;
		ctr202:
		{
			#line 103 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 1; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st148;
		ctr211:
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st148;
		st148:
		p+= 1;
		if ( p == pe )
		goto _test_eof148;
		st_case_148:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st11;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		st11:
		p+= 1;
		if ( p == pe )
		goto _test_eof11;
		st_case_11:
		switch( ( (*( p))) ) {
			case 37: {
				goto st148;
			}
			case 117: {
				goto st12;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		st12:
		p+= 1;
		if ( p == pe )
		goto _test_eof12;
		st_case_12:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st13;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st13;
			}
		} else {
			goto st13;
		}
		{
			goto st0;
		}
		st13:
		p+= 1;
		if ( p == pe )
		goto _test_eof13;
		st_case_13:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st14;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st14;
			}
		} else {
			goto st14;
		}
		{
			goto st0;
		}
		st14:
		p+= 1;
		if ( p == pe )
		goto _test_eof14;
		st_case_14:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st15;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st15;
			}
		} else {
			goto st15;
		}
		{
			goto st0;
		}
		st15:
		p+= 1;
		if ( p == pe )
		goto _test_eof15;
		st_case_15:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		ctr155:
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr163:
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr179:
		{
			#line 133 "src/uri.rl"
			s = p; }
		{
			#line 134 "src/uri.rl"
			uri->service = s; uri->service_len = p - s; }
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr189:
		{
			#line 134 "src/uri.rl"
			uri->service = s; uri->service_len = p - s; }
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr205:
		{
			#line 103 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 1; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr213:
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr319:
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 130 "src/uri.rl"
			s = p;}
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr322:
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		ctr327:
		{
			#line 163 "src/uri.rl"
			s = p; }
		{
			#line 167 "src/uri.rl"
			uri->path = s; uri->path_len = p - s; }
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		{
			#line 187 "src/uri.rl"
			s = p; }
		
		goto st149;
		st149:
		p+= 1;
		if ( p == pe )
		goto _test_eof149;
		st_case_149:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr169;
			}
			case 35: {
				goto ctr170;
			}
			case 37: {
				goto ctr171;
			}
			case 61: {
				goto ctr169;
			}
			case 95: {
				goto ctr169;
			}
			case 124: {
				goto ctr169;
			}
			case 126: {
				goto ctr169;
			}
		}
		if ( ( (*( p))) < 63 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto ctr169;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto ctr169;
			}
		} else {
			goto ctr169;
		}
		{
			goto st0;
		}
		ctr169:
		{
			#line 71 "src/uri.rl"
			s = p; }
		
		goto st150;
		st150:
		p+= 1;
		if ( p == pe )
		goto _test_eof150;
		st_case_150:
		switch( ( (*( p))) ) {
			case 33: {
				goto st150;
			}
			case 35: {
				goto ctr172;
			}
			case 37: {
				goto st16;
			}
			case 61: {
				goto st150;
			}
			case 95: {
				goto st150;
			}
			case 124: {
				goto st150;
			}
			case 126: {
				goto st150;
			}
		}
		if ( ( (*( p))) < 63 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st150;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st150;
			}
		} else {
			goto st150;
		}
		{
			goto st0;
		}
		ctr171:
		{
			#line 71 "src/uri.rl"
			s = p; }
		
		goto st16;
		st16:
		p+= 1;
		if ( p == pe )
		goto _test_eof16;
		st_case_16:
		switch( ( (*( p))) ) {
			case 37: {
				goto st150;
			}
			case 117: {
				goto st17;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st150;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st150;
			}
		} else {
			goto st150;
		}
		{
			goto st0;
		}
		st17:
		p+= 1;
		if ( p == pe )
		goto _test_eof17;
		st_case_17:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st18;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st18;
			}
		} else {
			goto st18;
		}
		{
			goto st0;
		}
		st18:
		p+= 1;
		if ( p == pe )
		goto _test_eof18;
		st_case_18:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st19;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st19;
			}
		} else {
			goto st19;
		}
		{
			goto st0;
		}
		st19:
		p+= 1;
		if ( p == pe )
		goto _test_eof19;
		st_case_19:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st20;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st20;
			}
		} else {
			goto st20;
		}
		{
			goto st0;
		}
		st20:
		p+= 1;
		if ( p == pe )
		goto _test_eof20;
		st_case_20:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st150;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st150;
			}
		} else {
			goto st150;
		}
		{
			goto st0;
		}
		ctr162:
		{
			#line 140 "src/uri.rl"
			login = s; login_len = p - s; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st151;
		ctr241:
		{
			#line 140 "src/uri.rl"
			login = s; login_len = p - s; }
		{
			#line 103 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 1; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st151;
		st151:
		p+= 1;
		if ( p == pe )
		goto _test_eof151;
		st_case_151:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr174;
			}
			case 35: {
				goto ctr175;
			}
			case 37: {
				goto ctr176;
			}
			case 47: {
				goto ctr177;
			}
			case 59: {
				goto ctr174;
			}
			case 61: {
				goto ctr174;
			}
			case 63: {
				goto ctr179;
			}
			case 64: {
				goto ctr180;
			}
			case 95: {
				goto ctr174;
			}
			case 126: {
				goto ctr174;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto ctr174;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr181;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr181;
			}
		} else {
			goto ctr178;
		}
		{
			goto st0;
		}
		ctr174:
		{
			#line 143 "src/uri.rl"
			s = p; }
		
		goto st21;
		st21:
		p+= 1;
		if ( p == pe )
		goto _test_eof21;
		st_case_21:
		switch( ( (*( p))) ) {
			case 33: {
				goto st21;
			}
			case 37: {
				goto st22;
			}
			case 59: {
				goto st21;
			}
			case 61: {
				goto st21;
			}
			case 64: {
				goto ctr23;
			}
			case 95: {
				goto st21;
			}
			case 126: {
				goto st21;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st21;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st21;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st21;
			}
		} else {
			goto st21;
		}
		{
			goto st0;
		}
		ctr176:
		{
			#line 143 "src/uri.rl"
			s = p; }
		
		goto st22;
		st22:
		p+= 1;
		if ( p == pe )
		goto _test_eof22;
		st_case_22:
		switch( ( (*( p))) ) {
			case 37: {
				goto st21;
			}
			case 117: {
				goto st23;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st21;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st21;
			}
		} else {
			goto st21;
		}
		{
			goto st0;
		}
		st23:
		p+= 1;
		if ( p == pe )
		goto _test_eof23;
		st_case_23:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st24;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st24;
			}
		} else {
			goto st24;
		}
		{
			goto st0;
		}
		st24:
		p+= 1;
		if ( p == pe )
		goto _test_eof24;
		st_case_24:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st25;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st25;
			}
		} else {
			goto st25;
		}
		{
			goto st0;
		}
		st25:
		p+= 1;
		if ( p == pe )
		goto _test_eof25;
		st_case_25:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st26;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st26;
			}
		} else {
			goto st26;
		}
		{
			goto st0;
		}
		st26:
		p+= 1;
		if ( p == pe )
		goto _test_eof26;
		st_case_26:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st21;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st21;
			}
		} else {
			goto st21;
		}
		{
			goto st0;
		}
		ctr23:
		{
			#line 144 "src/uri.rl"
			uri->password = s; uri->password_len = p - s; }
		{
			#line 148 "src/uri.rl"
			uri->login = login; uri->login_len = login_len; }
		
		goto st27;
		ctr164:
		{
			#line 140 "src/uri.rl"
			login = s; login_len = p - s; }
		{
			#line 148 "src/uri.rl"
			uri->login = login; uri->login_len = login_len; }
		
		goto st27;
		ctr180:
		{
			#line 143 "src/uri.rl"
			s = p; }
		{
			#line 144 "src/uri.rl"
			uri->password = s; uri->password_len = p - s; }
		{
			#line 148 "src/uri.rl"
			uri->login = login; uri->login_len = login_len; }
		
		goto st27;
		st27:
		p+= 1;
		if ( p == pe )
		goto _test_eof27;
		st_case_27:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr28;
			}
			case 37: {
				goto ctr29;
			}
			case 47: {
				goto ctr30;
			}
			case 59: {
				goto ctr28;
			}
			case 61: {
				goto ctr28;
			}
			case 91: {
				goto st38;
			}
			case 95: {
				goto ctr28;
			}
			case 117: {
				goto ctr33;
			}
			case 126: {
				goto ctr28;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto ctr28;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr28;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr28;
			}
		} else {
			goto ctr31;
		}
		{
			goto st0;
		}
		ctr28:
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st152;
		st152:
		p+= 1;
		if ( p == pe )
		goto _test_eof152;
		st_case_152:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		ctr29:
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st28;
		st28:
		p+= 1;
		if ( p == pe )
		goto _test_eof28;
		st_case_28:
		switch( ( (*( p))) ) {
			case 37: {
				goto st152;
			}
			case 117: {
				goto st29;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st29:
		p+= 1;
		if ( p == pe )
		goto _test_eof29;
		st_case_29:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st30;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st30;
			}
		} else {
			goto st30;
		}
		{
			goto st0;
		}
		st30:
		p+= 1;
		if ( p == pe )
		goto _test_eof30;
		st_case_30:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st31;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st31;
			}
		} else {
			goto st31;
		}
		{
			goto st0;
		}
		st31:
		p+= 1;
		if ( p == pe )
		goto _test_eof31;
		st_case_31:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st32;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st32;
			}
		} else {
			goto st32;
		}
		{
			goto st0;
		}
		st32:
		p+= 1;
		if ( p == pe )
		goto _test_eof32;
		st_case_32:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		ctr183:
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st153;
		ctr204:
		{
			#line 103 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 1; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st153;
		st153:
		p+= 1;
		if ( p == pe )
		goto _test_eof153;
		st_case_153:
		switch( ( (*( p))) ) {
			case 35: {
				goto ctr175;
			}
			case 47: {
				goto ctr177;
			}
			case 63: {
				goto ctr179;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto ctr184;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto ctr185;
			}
		} else {
			goto ctr185;
		}
		{
			goto st0;
		}
		ctr184:
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st154;
		st154:
		p+= 1;
		if ( p == pe )
		goto _test_eof154;
		st_case_154:
		switch( ( (*( p))) ) {
			case 35: {
				goto ctr186;
			}
			case 47: {
				goto ctr187;
			}
			case 63: {
				goto ctr189;
			}
		}
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st154;
		}
		{
			goto st0;
		}
		ctr185:
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st155;
		st155:
		p+= 1;
		if ( p == pe )
		goto _test_eof155;
		st_case_155:
		switch( ( (*( p))) ) {
			case 35: {
				goto ctr186;
			}
			case 47: {
				goto ctr187;
			}
			case 63: {
				goto ctr189;
			}
		}
		if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st155;
			}
		} else if ( ( (*( p))) >= 65 ) {
			goto st155;
		}
		{
			goto st0;
		}
		ctr30:
		{
			#line 184 "src/uri.rl"
			s = p; }
		
		goto st156;
		st156:
		p+= 1;
		if ( p == pe )
		goto _test_eof156;
		st_case_156:
		switch( ( (*( p))) ) {
			case 33: {
				goto st157;
			}
			case 37: {
				goto st33;
			}
			case 61: {
				goto st157;
			}
			case 95: {
				goto st157;
			}
			case 124: {
				goto st157;
			}
			case 126: {
				goto st157;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st157;
			}
		} else if ( ( (*( p))) > 59 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st157;
				}
			} else if ( ( (*( p))) >= 64 ) {
				goto st157;
			}
		} else {
			goto st157;
		}
		{
			goto st0;
		}
		st157:
		p+= 1;
		if ( p == pe )
		goto _test_eof157;
		st_case_157:
		switch( ( (*( p))) ) {
			case 33: {
				goto st157;
			}
			case 37: {
				goto st33;
			}
			case 61: {
				goto st157;
			}
			case 95: {
				goto st157;
			}
			case 124: {
				goto st157;
			}
			case 126: {
				goto st157;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st157;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st157;
			}
		} else {
			goto st157;
		}
		{
			goto st0;
		}
		st33:
		p+= 1;
		if ( p == pe )
		goto _test_eof33;
		st_case_33:
		switch( ( (*( p))) ) {
			case 37: {
				goto st157;
			}
			case 117: {
				goto st34;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st157;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st157;
			}
		} else {
			goto st157;
		}
		{
			goto st0;
		}
		st34:
		p+= 1;
		if ( p == pe )
		goto _test_eof34;
		st_case_34:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st35;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st35;
			}
		} else {
			goto st35;
		}
		{
			goto st0;
		}
		st35:
		p+= 1;
		if ( p == pe )
		goto _test_eof35;
		st_case_35:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st36;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st36;
			}
		} else {
			goto st36;
		}
		{
			goto st0;
		}
		st36:
		p+= 1;
		if ( p == pe )
		goto _test_eof36;
		st_case_36:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st37;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st37;
			}
		} else {
			goto st37;
		}
		{
			goto st0;
		}
		st37:
		p+= 1;
		if ( p == pe )
		goto _test_eof37;
		st_case_37:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st157;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st157;
			}
		} else {
			goto st157;
		}
		{
			goto st0;
		}
		ctr31:
		{
			#line 102 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st158;
		st158:
		p+= 1;
		if ( p == pe )
		goto _test_eof158;
		st_case_158:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st159;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st171;
		}
		{
			goto st0;
		}
		st159:
		p+= 1;
		if ( p == pe )
		goto _test_eof159;
		st_case_159:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st160;
		}
		{
			goto st0;
		}
		st160:
		p+= 1;
		if ( p == pe )
		goto _test_eof160;
		st_case_160:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st161;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st169;
		}
		{
			goto st0;
		}
		st161:
		p+= 1;
		if ( p == pe )
		goto _test_eof161;
		st_case_161:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st162;
		}
		{
			goto st0;
		}
		st162:
		p+= 1;
		if ( p == pe )
		goto _test_eof162;
		st_case_162:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st163;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st167;
		}
		{
			goto st0;
		}
		st163:
		p+= 1;
		if ( p == pe )
		goto _test_eof163;
		st_case_163:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st164;
		}
		{
			goto st0;
		}
		st164:
		p+= 1;
		if ( p == pe )
		goto _test_eof164;
		st_case_164:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr204;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr205;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st165;
		}
		{
			goto st0;
		}
		st165:
		p+= 1;
		if ( p == pe )
		goto _test_eof165;
		st_case_165:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr204;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr205;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st166;
		}
		{
			goto st0;
		}
		st166:
		p+= 1;
		if ( p == pe )
		goto _test_eof166;
		st_case_166:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr204;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr205;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st167:
		p+= 1;
		if ( p == pe )
		goto _test_eof167;
		st_case_167:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st163;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st168;
		}
		{
			goto st0;
		}
		st168:
		p+= 1;
		if ( p == pe )
		goto _test_eof168;
		st_case_168:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st163;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st169:
		p+= 1;
		if ( p == pe )
		goto _test_eof169;
		st_case_169:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st161;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st170;
		}
		{
			goto st0;
		}
		st170:
		p+= 1;
		if ( p == pe )
		goto _test_eof170;
		st_case_170:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st161;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st171:
		p+= 1;
		if ( p == pe )
		goto _test_eof171;
		st_case_171:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st159;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 59: {
				goto st152;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st152;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st152;
			}
		} else {
			goto st172;
		}
		{
			goto st0;
		}
		st172:
		p+= 1;
		if ( p == pe )
		goto _test_eof172;
		st_case_172:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 46: {
				goto st159;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st38:
		p+= 1;
		if ( p == pe )
		goto _test_eof38;
		st_case_38:
		if ( ( (*( p))) == 58 ) {
			goto ctr45;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto ctr44;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto ctr44;
		}
		{
			goto st0;
		}
		ctr44:
		{
			#line 109 "src/uri.rl"
			s = p; }
		
		goto st39;
		st39:
		p+= 1;
		if ( p == pe )
		goto _test_eof39;
		st_case_39:
		if ( ( (*( p))) == 58 ) {
			goto st43;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st40;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st40;
		}
		{
			goto st0;
		}
		st40:
		p+= 1;
		if ( p == pe )
		goto _test_eof40;
		st_case_40:
		if ( ( (*( p))) == 58 ) {
			goto st43;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st41;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st41;
		}
		{
			goto st0;
		}
		st41:
		p+= 1;
		if ( p == pe )
		goto _test_eof41;
		st_case_41:
		if ( ( (*( p))) == 58 ) {
			goto st43;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st42;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st42;
		}
		{
			goto st0;
		}
		st42:
		p+= 1;
		if ( p == pe )
		goto _test_eof42;
		st_case_42:
		if ( ( (*( p))) == 58 ) {
			goto st43;
		}
		{
			goto st0;
		}
		st43:
		p+= 1;
		if ( p == pe )
		goto _test_eof43;
		st_case_43:
		switch( ( (*( p))) ) {
			case 58: {
				goto st48;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st44;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st44;
		}
		{
			goto st0;
		}
		st44:
		p+= 1;
		if ( p == pe )
		goto _test_eof44;
		st_case_44:
		switch( ( (*( p))) ) {
			case 58: {
				goto st48;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st45;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st45;
		}
		{
			goto st0;
		}
		st45:
		p+= 1;
		if ( p == pe )
		goto _test_eof45;
		st_case_45:
		switch( ( (*( p))) ) {
			case 58: {
				goto st48;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st46;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st46;
		}
		{
			goto st0;
		}
		st46:
		p+= 1;
		if ( p == pe )
		goto _test_eof46;
		st_case_46:
		switch( ( (*( p))) ) {
			case 58: {
				goto st48;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st47;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st47;
		}
		{
			goto st0;
		}
		st47:
		p+= 1;
		if ( p == pe )
		goto _test_eof47;
		st_case_47:
		switch( ( (*( p))) ) {
			case 58: {
				goto st48;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st48:
		p+= 1;
		if ( p == pe )
		goto _test_eof48;
		st_case_48:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st49;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st49;
		}
		{
			goto st0;
		}
		st49:
		p+= 1;
		if ( p == pe )
		goto _test_eof49;
		st_case_49:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st50;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st50;
		}
		{
			goto st0;
		}
		st50:
		p+= 1;
		if ( p == pe )
		goto _test_eof50;
		st_case_50:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st51;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st51;
		}
		{
			goto st0;
		}
		st51:
		p+= 1;
		if ( p == pe )
		goto _test_eof51;
		st_case_51:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st52;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st52;
		}
		{
			goto st0;
		}
		st52:
		p+= 1;
		if ( p == pe )
		goto _test_eof52;
		st_case_52:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st53:
		p+= 1;
		if ( p == pe )
		goto _test_eof53;
		st_case_53:
		switch( ( (*( p))) ) {
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st54;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st54;
		}
		{
			goto st0;
		}
		st54:
		p+= 1;
		if ( p == pe )
		goto _test_eof54;
		st_case_54:
		switch( ( (*( p))) ) {
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st55;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st55;
		}
		{
			goto st0;
		}
		st55:
		p+= 1;
		if ( p == pe )
		goto _test_eof55;
		st_case_55:
		switch( ( (*( p))) ) {
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st56;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st56;
		}
		{
			goto st0;
		}
		st56:
		p+= 1;
		if ( p == pe )
		goto _test_eof56;
		st_case_56:
		switch( ( (*( p))) ) {
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st57;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st57;
		}
		{
			goto st0;
		}
		st57:
		p+= 1;
		if ( p == pe )
		goto _test_eof57;
		st_case_57:
		switch( ( (*( p))) ) {
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st58:
		p+= 1;
		if ( p == pe )
		goto _test_eof58;
		st_case_58:
		switch( ( (*( p))) ) {
			case 58: {
				goto st63;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st59;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st59;
		}
		{
			goto st0;
		}
		st59:
		p+= 1;
		if ( p == pe )
		goto _test_eof59;
		st_case_59:
		switch( ( (*( p))) ) {
			case 58: {
				goto st63;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st60;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st60;
		}
		{
			goto st0;
		}
		st60:
		p+= 1;
		if ( p == pe )
		goto _test_eof60;
		st_case_60:
		switch( ( (*( p))) ) {
			case 58: {
				goto st63;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st61;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st61;
		}
		{
			goto st0;
		}
		st61:
		p+= 1;
		if ( p == pe )
		goto _test_eof61;
		st_case_61:
		switch( ( (*( p))) ) {
			case 58: {
				goto st63;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st62;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st62;
		}
		{
			goto st0;
		}
		st62:
		p+= 1;
		if ( p == pe )
		goto _test_eof62;
		st_case_62:
		switch( ( (*( p))) ) {
			case 58: {
				goto st63;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st63:
		p+= 1;
		if ( p == pe )
		goto _test_eof63;
		st_case_63:
		switch( ( (*( p))) ) {
			case 58: {
				goto st68;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st64;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st64;
		}
		{
			goto st0;
		}
		st64:
		p+= 1;
		if ( p == pe )
		goto _test_eof64;
		st_case_64:
		switch( ( (*( p))) ) {
			case 58: {
				goto st68;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st65;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st65;
		}
		{
			goto st0;
		}
		st65:
		p+= 1;
		if ( p == pe )
		goto _test_eof65;
		st_case_65:
		switch( ( (*( p))) ) {
			case 58: {
				goto st68;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st66;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st66;
		}
		{
			goto st0;
		}
		st66:
		p+= 1;
		if ( p == pe )
		goto _test_eof66;
		st_case_66:
		switch( ( (*( p))) ) {
			case 58: {
				goto st68;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st67;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st67;
		}
		{
			goto st0;
		}
		st67:
		p+= 1;
		if ( p == pe )
		goto _test_eof67;
		st_case_67:
		switch( ( (*( p))) ) {
			case 58: {
				goto st68;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st68:
		p+= 1;
		if ( p == pe )
		goto _test_eof68;
		st_case_68:
		switch( ( (*( p))) ) {
			case 58: {
				goto st73;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st69;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st69;
		}
		{
			goto st0;
		}
		st69:
		p+= 1;
		if ( p == pe )
		goto _test_eof69;
		st_case_69:
		switch( ( (*( p))) ) {
			case 58: {
				goto st73;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st70;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st70;
		}
		{
			goto st0;
		}
		st70:
		p+= 1;
		if ( p == pe )
		goto _test_eof70;
		st_case_70:
		switch( ( (*( p))) ) {
			case 58: {
				goto st73;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st71;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st71;
		}
		{
			goto st0;
		}
		st71:
		p+= 1;
		if ( p == pe )
		goto _test_eof71;
		st_case_71:
		switch( ( (*( p))) ) {
			case 58: {
				goto st73;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st72;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st72;
		}
		{
			goto st0;
		}
		st72:
		p+= 1;
		if ( p == pe )
		goto _test_eof72;
		st_case_72:
		switch( ( (*( p))) ) {
			case 58: {
				goto st73;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st73:
		p+= 1;
		if ( p == pe )
		goto _test_eof73;
		st_case_73:
		switch( ( (*( p))) ) {
			case 58: {
				goto st78;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st74;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st74;
		}
		{
			goto st0;
		}
		st74:
		p+= 1;
		if ( p == pe )
		goto _test_eof74;
		st_case_74:
		switch( ( (*( p))) ) {
			case 58: {
				goto st78;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st75;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st75;
		}
		{
			goto st0;
		}
		st75:
		p+= 1;
		if ( p == pe )
		goto _test_eof75;
		st_case_75:
		switch( ( (*( p))) ) {
			case 58: {
				goto st78;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st76;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st76;
		}
		{
			goto st0;
		}
		st76:
		p+= 1;
		if ( p == pe )
		goto _test_eof76;
		st_case_76:
		switch( ( (*( p))) ) {
			case 58: {
				goto st78;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st77;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st77;
		}
		{
			goto st0;
		}
		st77:
		p+= 1;
		if ( p == pe )
		goto _test_eof77;
		st_case_77:
		switch( ( (*( p))) ) {
			case 58: {
				goto st78;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st78:
		p+= 1;
		if ( p == pe )
		goto _test_eof78;
		st_case_78:
		if ( ( (*( p))) == 93 ) {
			goto ctr52;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st79;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st79;
		}
		{
			goto st0;
		}
		st79:
		p+= 1;
		if ( p == pe )
		goto _test_eof79;
		st_case_79:
		if ( ( (*( p))) == 93 ) {
			goto ctr52;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st80;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st80;
		}
		{
			goto st0;
		}
		st80:
		p+= 1;
		if ( p == pe )
		goto _test_eof80;
		st_case_80:
		if ( ( (*( p))) == 93 ) {
			goto ctr52;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st81;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st81;
		}
		{
			goto st0;
		}
		st81:
		p+= 1;
		if ( p == pe )
		goto _test_eof81;
		st_case_81:
		if ( ( (*( p))) == 93 ) {
			goto ctr52;
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st82;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st82;
		}
		{
			goto st0;
		}
		st82:
		p+= 1;
		if ( p == pe )
		goto _test_eof82;
		st_case_82:
		if ( ( (*( p))) == 93 ) {
			goto ctr52;
		}
		{
			goto st0;
		}
		ctr52:
		{
			#line 110 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 2; }
		
		goto st173;
		st173:
		p+= 1;
		if ( p == pe )
		goto _test_eof173;
		st_case_173:
		switch( ( (*( p))) ) {
			case 35: {
				goto ctr210;
			}
			case 47: {
				goto ctr211;
			}
			case 58: {
				goto st153;
			}
			case 63: {
				goto ctr213;
			}
		}
		{
			goto st0;
		}
		ctr45:
		{
			#line 109 "src/uri.rl"
			s = p; }
		
		goto st83;
		st83:
		p+= 1;
		if ( p == pe )
		goto _test_eof83;
		st_case_83:
		switch( ( (*( p))) ) {
			case 58: {
				goto st84;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st44;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st44;
		}
		{
			goto st0;
		}
		st84:
		p+= 1;
		if ( p == pe )
		goto _test_eof84;
		st_case_84:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
			case 102: {
				goto st85;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 101 ) {
				goto st49;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st49;
		}
		{
			goto st0;
		}
		st85:
		p+= 1;
		if ( p == pe )
		goto _test_eof85;
		st_case_85:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
			case 102: {
				goto st86;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 101 ) {
				goto st50;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st50;
		}
		{
			goto st0;
		}
		st86:
		p+= 1;
		if ( p == pe )
		goto _test_eof86;
		st_case_86:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
			case 102: {
				goto st87;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 101 ) {
				goto st51;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st51;
		}
		{
			goto st0;
		}
		st87:
		p+= 1;
		if ( p == pe )
		goto _test_eof87;
		st_case_87:
		switch( ( (*( p))) ) {
			case 58: {
				goto st53;
			}
			case 93: {
				goto ctr52;
			}
			case 102: {
				goto st88;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 101 ) {
				goto st52;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st52;
		}
		{
			goto st0;
		}
		st88:
		p+= 1;
		if ( p == pe )
		goto _test_eof88;
		st_case_88:
		switch( ( (*( p))) ) {
			case 58: {
				goto st89;
			}
			case 93: {
				goto ctr52;
			}
		}
		{
			goto st0;
		}
		st89:
		p+= 1;
		if ( p == pe )
		goto _test_eof89;
		st_case_89:
		switch( ( (*( p))) ) {
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st54;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st90;
		}
		{
			goto st0;
		}
		st90:
		p+= 1;
		if ( p == pe )
		goto _test_eof90;
		st_case_90:
		switch( ( (*( p))) ) {
			case 46: {
				goto st91;
			}
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st55;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st102;
		}
		{
			goto st0;
		}
		st91:
		p+= 1;
		if ( p == pe )
		goto _test_eof91;
		st_case_91:
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st92;
		}
		{
			goto st0;
		}
		st92:
		p+= 1;
		if ( p == pe )
		goto _test_eof92;
		st_case_92:
		if ( ( (*( p))) == 46 ) {
			goto st93;
		}
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st100;
		}
		{
			goto st0;
		}
		st93:
		p+= 1;
		if ( p == pe )
		goto _test_eof93;
		st_case_93:
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st94;
		}
		{
			goto st0;
		}
		st94:
		p+= 1;
		if ( p == pe )
		goto _test_eof94;
		st_case_94:
		if ( ( (*( p))) == 46 ) {
			goto st95;
		}
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st98;
		}
		{
			goto st0;
		}
		st95:
		p+= 1;
		if ( p == pe )
		goto _test_eof95;
		st_case_95:
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st96;
		}
		{
			goto st0;
		}
		st96:
		p+= 1;
		if ( p == pe )
		goto _test_eof96;
		st_case_96:
		if ( ( (*( p))) == 93 ) {
			goto ctr52;
		}
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st97;
		}
		{
			goto st0;
		}
		st97:
		p+= 1;
		if ( p == pe )
		goto _test_eof97;
		st_case_97:
		if ( ( (*( p))) == 93 ) {
			goto ctr52;
		}
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st82;
		}
		{
			goto st0;
		}
		st98:
		p+= 1;
		if ( p == pe )
		goto _test_eof98;
		st_case_98:
		if ( ( (*( p))) == 46 ) {
			goto st95;
		}
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st99;
		}
		{
			goto st0;
		}
		st99:
		p+= 1;
		if ( p == pe )
		goto _test_eof99;
		st_case_99:
		if ( ( (*( p))) == 46 ) {
			goto st95;
		}
		{
			goto st0;
		}
		st100:
		p+= 1;
		if ( p == pe )
		goto _test_eof100;
		st_case_100:
		if ( ( (*( p))) == 46 ) {
			goto st93;
		}
		if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
			goto st101;
		}
		{
			goto st0;
		}
		st101:
		p+= 1;
		if ( p == pe )
		goto _test_eof101;
		st_case_101:
		if ( ( (*( p))) == 46 ) {
			goto st93;
		}
		{
			goto st0;
		}
		st102:
		p+= 1;
		if ( p == pe )
		goto _test_eof102;
		st_case_102:
		switch( ( (*( p))) ) {
			case 46: {
				goto st91;
			}
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st56;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st103;
		}
		{
			goto st0;
		}
		st103:
		p+= 1;
		if ( p == pe )
		goto _test_eof103;
		st_case_103:
		switch( ( (*( p))) ) {
			case 46: {
				goto st91;
			}
			case 58: {
				goto st58;
			}
			case 93: {
				goto ctr52;
			}
		}
		if ( ( (*( p))) > 57 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st57;
			}
		} else if ( ( (*( p))) >= 48 ) {
			goto st57;
		}
		{
			goto st0;
		}
		ctr33:
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st174;
		st174:
		p+= 1;
		if ( p == pe )
		goto _test_eof174;
		st_case_174:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 110: {
				goto st175;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st175:
		p+= 1;
		if ( p == pe )
		goto _test_eof175;
		st_case_175:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 105: {
				goto st176;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st176:
		p+= 1;
		if ( p == pe )
		goto _test_eof176;
		st_case_176:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 120: {
				goto st177;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		st177:
		p+= 1;
		if ( p == pe )
		goto _test_eof177;
		st_case_177:
		switch( ( (*( p))) ) {
			case 33: {
				goto st152;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st28;
			}
			case 47: {
				goto ctr217;
			}
			case 58: {
				goto ctr183;
			}
			case 61: {
				goto st152;
			}
			case 63: {
				goto ctr163;
			}
			case 95: {
				goto st152;
			}
			case 126: {
				goto st152;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st152;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st152;
			}
		} else {
			goto st152;
		}
		{
			goto st0;
		}
		ctr217:
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st178;
		st178:
		p+= 1;
		if ( p == pe )
		goto _test_eof178;
		st_case_178:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st11;
			}
			case 58: {
				goto st179;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		st179:
		p+= 1;
		if ( p == pe )
		goto _test_eof179;
		st_case_179:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr219;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto ctr220;
			}
			case 47: {
				goto ctr221;
			}
			case 58: {
				goto ctr222;
			}
			case 61: {
				goto ctr219;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto ctr219;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto ctr219;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto ctr219;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto ctr219;
			}
		} else {
			goto ctr219;
		}
		{
			goto st0;
		}
		ctr219:
		{
			#line 130 "src/uri.rl"
			s = p;}
		
		goto st180;
		st180:
		p+= 1;
		if ( p == pe )
		goto _test_eof180;
		st_case_180:
		switch( ( (*( p))) ) {
			case 33: {
				goto st180;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st104;
			}
			case 47: {
				goto st181;
			}
			case 58: {
				goto ctr224;
			}
			case 61: {
				goto st180;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st180;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st180;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st180;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st180;
			}
		} else {
			goto st180;
		}
		{
			goto st0;
		}
		ctr220:
		{
			#line 130 "src/uri.rl"
			s = p;}
		
		goto st104;
		st104:
		p+= 1;
		if ( p == pe )
		goto _test_eof104;
		st_case_104:
		switch( ( (*( p))) ) {
			case 37: {
				goto st180;
			}
			case 117: {
				goto st105;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st180;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st180;
			}
		} else {
			goto st180;
		}
		{
			goto st0;
		}
		st105:
		p+= 1;
		if ( p == pe )
		goto _test_eof105;
		st_case_105:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st106;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st106;
			}
		} else {
			goto st106;
		}
		{
			goto st0;
		}
		st106:
		p+= 1;
		if ( p == pe )
		goto _test_eof106;
		st_case_106:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st107;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st107;
			}
		} else {
			goto st107;
		}
		{
			goto st0;
		}
		st107:
		p+= 1;
		if ( p == pe )
		goto _test_eof107;
		st_case_107:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st108;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st108;
			}
		} else {
			goto st108;
		}
		{
			goto st0;
		}
		st108:
		p+= 1;
		if ( p == pe )
		goto _test_eof108;
		st_case_108:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st180;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st180;
			}
		} else {
			goto st180;
		}
		{
			goto st0;
		}
		ctr227:
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st181;
		ctr221:
		{
			#line 130 "src/uri.rl"
			s = p;}
		
		goto st181;
		st181:
		p+= 1;
		if ( p == pe )
		goto _test_eof181;
		st_case_181:
		switch( ( (*( p))) ) {
			case 33: {
				goto st181;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st109;
			}
			case 58: {
				goto ctr226;
			}
			case 61: {
				goto st181;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st181;
			}
			case 124: {
				goto st181;
			}
			case 126: {
				goto st181;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st181;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st181;
			}
		} else {
			goto st181;
		}
		{
			goto st0;
		}
		st109:
		p+= 1;
		if ( p == pe )
		goto _test_eof109;
		st_case_109:
		switch( ( (*( p))) ) {
			case 37: {
				goto st181;
			}
			case 117: {
				goto st110;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st181;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st181;
			}
		} else {
			goto st181;
		}
		{
			goto st0;
		}
		st110:
		p+= 1;
		if ( p == pe )
		goto _test_eof110;
		st_case_110:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st111;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st111;
			}
		} else {
			goto st111;
		}
		{
			goto st0;
		}
		st111:
		p+= 1;
		if ( p == pe )
		goto _test_eof111;
		st_case_111:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st112;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st112;
			}
		} else {
			goto st112;
		}
		{
			goto st0;
		}
		st112:
		p+= 1;
		if ( p == pe )
		goto _test_eof112;
		st_case_112:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st113;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st113;
			}
		} else {
			goto st113;
		}
		{
			goto st0;
		}
		st113:
		p+= 1;
		if ( p == pe )
		goto _test_eof113;
		st_case_113:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st181;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st181;
			}
		} else {
			goto st181;
		}
		{
			goto st0;
		}
		ctr226:
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		
		goto st182;
		st182:
		p+= 1;
		if ( p == pe )
		goto _test_eof182;
		st_case_182:
		switch( ( (*( p))) ) {
			case 33: {
				goto st181;
			}
			case 35: {
				goto ctr210;
			}
			case 37: {
				goto st109;
			}
			case 47: {
				goto ctr227;
			}
			case 58: {
				goto ctr226;
			}
			case 61: {
				goto st181;
			}
			case 63: {
				goto ctr213;
			}
			case 95: {
				goto st181;
			}
			case 124: {
				goto st181;
			}
			case 126: {
				goto st181;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st181;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st181;
			}
		} else {
			goto st181;
		}
		{
			goto st0;
		}
		ctr224:
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		
		goto st183;
		ctr222:
		{
			#line 130 "src/uri.rl"
			s = p;}
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		
		goto st183;
		st183:
		p+= 1;
		if ( p == pe )
		goto _test_eof183;
		st_case_183:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr210;
			}
			case 37: {
				goto st11;
			}
			case 47: {
				goto ctr211;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr213;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		ctr178:
		{
			#line 143 "src/uri.rl"
			s = p; }
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st184;
		st184:
		p+= 1;
		if ( p == pe )
		goto _test_eof184;
		st_case_184:
		switch( ( (*( p))) ) {
			case 33: {
				goto st21;
			}
			case 35: {
				goto ctr186;
			}
			case 37: {
				goto st22;
			}
			case 47: {
				goto ctr187;
			}
			case 59: {
				goto st21;
			}
			case 61: {
				goto st21;
			}
			case 63: {
				goto ctr189;
			}
			case 64: {
				goto ctr23;
			}
			case 95: {
				goto st21;
			}
			case 126: {
				goto st21;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st21;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st21;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st21;
			}
		} else {
			goto st184;
		}
		{
			goto st0;
		}
		ctr181:
		{
			#line 143 "src/uri.rl"
			s = p; }
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st185;
		st185:
		p+= 1;
		if ( p == pe )
		goto _test_eof185;
		st_case_185:
		switch( ( (*( p))) ) {
			case 33: {
				goto st21;
			}
			case 35: {
				goto ctr186;
			}
			case 37: {
				goto st22;
			}
			case 47: {
				goto ctr187;
			}
			case 59: {
				goto st21;
			}
			case 61: {
				goto st21;
			}
			case 63: {
				goto ctr189;
			}
			case 64: {
				goto ctr23;
			}
			case 95: {
				goto st21;
			}
			case 126: {
				goto st21;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st21;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st185;
			}
		} else {
			goto st185;
		}
		{
			goto st0;
		}
		ctr153:
		{
			#line 184 "src/uri.rl"
			s = p; }
		
		goto st186;
		st186:
		p+= 1;
		if ( p == pe )
		goto _test_eof186;
		st_case_186:
		switch( ( (*( p))) ) {
			case 33: {
				goto st187;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st114;
			}
			case 61: {
				goto st187;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st187;
			}
			case 124: {
				goto st187;
			}
			case 126: {
				goto st187;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st187;
			}
		} else if ( ( (*( p))) > 59 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st187;
				}
			} else if ( ( (*( p))) >= 64 ) {
				goto st187;
			}
		} else {
			goto st187;
		}
		{
			goto st0;
		}
		st187:
		p+= 1;
		if ( p == pe )
		goto _test_eof187;
		st_case_187:
		switch( ( (*( p))) ) {
			case 33: {
				goto st187;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st114;
			}
			case 61: {
				goto st187;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st187;
			}
			case 124: {
				goto st187;
			}
			case 126: {
				goto st187;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st187;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st187;
			}
		} else {
			goto st187;
		}
		{
			goto st0;
		}
		st114:
		p+= 1;
		if ( p == pe )
		goto _test_eof114;
		st_case_114:
		switch( ( (*( p))) ) {
			case 37: {
				goto st187;
			}
			case 117: {
				goto st115;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st187;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st187;
			}
		} else {
			goto st187;
		}
		{
			goto st0;
		}
		st115:
		p+= 1;
		if ( p == pe )
		goto _test_eof115;
		st_case_115:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st116;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st116;
			}
		} else {
			goto st116;
		}
		{
			goto st0;
		}
		st116:
		p+= 1;
		if ( p == pe )
		goto _test_eof116;
		st_case_116:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st117;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st117;
			}
		} else {
			goto st117;
		}
		{
			goto st0;
		}
		st117:
		p+= 1;
		if ( p == pe )
		goto _test_eof117;
		st_case_117:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st118;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st118;
			}
		} else {
			goto st118;
		}
		{
			goto st0;
		}
		st118:
		p+= 1;
		if ( p == pe )
		goto _test_eof118;
		st_case_118:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st187;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st187;
			}
		} else {
			goto st187;
		}
		{
			goto st0;
		}
		ctr154:
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 102 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		{
			#line 180 "src/uri.rl"
			uri->service = p; }
		
		goto st188;
		st188:
		p+= 1;
		if ( p == pe )
		goto _test_eof188;
		st_case_188:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st189;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st201;
		}
		{
			goto st0;
		}
		st189:
		p+= 1;
		if ( p == pe )
		goto _test_eof189;
		st_case_189:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st190;
		}
		{
			goto st0;
		}
		st190:
		p+= 1;
		if ( p == pe )
		goto _test_eof190;
		st_case_190:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st191;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st199;
		}
		{
			goto st0;
		}
		st191:
		p+= 1;
		if ( p == pe )
		goto _test_eof191;
		st_case_191:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st192;
		}
		{
			goto st0;
		}
		st192:
		p+= 1;
		if ( p == pe )
		goto _test_eof192;
		st_case_192:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st193;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st197;
		}
		{
			goto st0;
		}
		st193:
		p+= 1;
		if ( p == pe )
		goto _test_eof193;
		st_case_193:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st194;
		}
		{
			goto st0;
		}
		st194:
		p+= 1;
		if ( p == pe )
		goto _test_eof194;
		st_case_194:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr241;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st195;
		}
		{
			goto st0;
		}
		st195:
		p+= 1;
		if ( p == pe )
		goto _test_eof195;
		st_case_195:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr241;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st196;
		}
		{
			goto st0;
		}
		st196:
		p+= 1;
		if ( p == pe )
		goto _test_eof196;
		st_case_196:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr241;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st145;
			}
		} else {
			goto st145;
		}
		{
			goto st0;
		}
		st197:
		p+= 1;
		if ( p == pe )
		goto _test_eof197;
		st_case_197:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st193;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st198;
		}
		{
			goto st0;
		}
		st198:
		p+= 1;
		if ( p == pe )
		goto _test_eof198;
		st_case_198:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st193;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st145;
			}
		} else {
			goto st145;
		}
		{
			goto st0;
		}
		st199:
		p+= 1;
		if ( p == pe )
		goto _test_eof199;
		st_case_199:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st191;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st200;
		}
		{
			goto st0;
		}
		st200:
		p+= 1;
		if ( p == pe )
		goto _test_eof200;
		st_case_200:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st191;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st145;
			}
		} else {
			goto st145;
		}
		{
			goto st0;
		}
		st201:
		p+= 1;
		if ( p == pe )
		goto _test_eof201;
		st_case_201:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st189;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st202;
		}
		{
			goto st0;
		}
		st202:
		p+= 1;
		if ( p == pe )
		goto _test_eof202;
		st_case_202:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 46: {
				goto st189;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st203;
		}
		{
			goto st0;
		}
		st203:
		p+= 1;
		if ( p == pe )
		goto _test_eof203;
		st_case_203:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr162;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st145;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st145;
			}
		} else {
			goto st203;
		}
		{
			goto st0;
		}
		st204:
		p+= 1;
		if ( p == pe )
		goto _test_eof204;
		st_case_204:
		switch( ( (*( p))) ) {
			case 35: {
				goto ctr151;
			}
			case 47: {
				goto st148;
			}
			case 63: {
				goto ctr155;
			}
		}
		{
			goto st0;
		}
		ctr157:
		{
			#line 153 "src/uri.rl"
			s = p; }
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st205;
		st205:
		p+= 1;
		if ( p == pe )
		goto _test_eof205;
		st_case_205:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 43: {
				goto st205;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr248;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 45 ) {
			if ( 36 <= ( (*( p))) ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st205;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st205;
			}
		} else {
			goto st205;
		}
		{
			goto st0;
		}
		ctr248:
		{
			#line 155 "src/uri.rl"
			scheme = s; scheme_len = p - s; }
		{
			#line 140 "src/uri.rl"
			login = s; login_len = p - s; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st206;
		st206:
		p+= 1;
		if ( p == pe )
		goto _test_eof206;
		st_case_206:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr174;
			}
			case 35: {
				goto ctr175;
			}
			case 37: {
				goto ctr176;
			}
			case 47: {
				goto ctr249;
			}
			case 59: {
				goto ctr174;
			}
			case 61: {
				goto ctr174;
			}
			case 63: {
				goto ctr179;
			}
			case 64: {
				goto ctr180;
			}
			case 95: {
				goto ctr174;
			}
			case 126: {
				goto ctr174;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto ctr174;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr181;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr181;
			}
		} else {
			goto ctr178;
		}
		{
			goto st0;
		}
		ctr249:
		{
			#line 171 "src/uri.rl"
			uri->scheme = scheme; uri->scheme_len = scheme_len;}
		{
			#line 133 "src/uri.rl"
			s = p; }
		{
			#line 134 "src/uri.rl"
			uri->service = s; uri->service_len = p - s; }
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st207;
		st207:
		p+= 1;
		if ( p == pe )
		goto _test_eof207;
		st_case_207:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st11;
			}
			case 47: {
				goto st208;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		st208:
		p+= 1;
		if ( p == pe )
		goto _test_eof208;
		st_case_208:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr251;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto ctr252;
			}
			case 47: {
				goto st148;
			}
			case 58: {
				goto st148;
			}
			case 59: {
				goto ctr251;
			}
			case 61: {
				goto ctr251;
			}
			case 63: {
				goto ctr155;
			}
			case 64: {
				goto st148;
			}
			case 91: {
				goto st38;
			}
			case 95: {
				goto ctr251;
			}
			case 117: {
				goto ctr254;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto ctr251;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto ctr251;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr251;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr251;
			}
		} else {
			goto ctr253;
		}
		{
			goto st0;
		}
		ctr251:
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st209;
		st209:
		p+= 1;
		if ( p == pe )
		goto _test_eof209;
		st_case_209:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		ctr252:
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st119;
		st119:
		p+= 1;
		if ( p == pe )
		goto _test_eof119;
		st_case_119:
		switch( ( (*( p))) ) {
			case 37: {
				goto st209;
			}
			case 117: {
				goto st120;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		st120:
		p+= 1;
		if ( p == pe )
		goto _test_eof120;
		st_case_120:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st121;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st121;
			}
		} else {
			goto st121;
		}
		{
			goto st0;
		}
		st121:
		p+= 1;
		if ( p == pe )
		goto _test_eof121;
		st_case_121:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st122;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st122;
			}
		} else {
			goto st122;
		}
		{
			goto st0;
		}
		st122:
		p+= 1;
		if ( p == pe )
		goto _test_eof122;
		st_case_122:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st123;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st123;
			}
		} else {
			goto st123;
		}
		{
			goto st0;
		}
		st123:
		p+= 1;
		if ( p == pe )
		goto _test_eof123;
		st_case_123:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		ctr256:
		{
			#line 140 "src/uri.rl"
			login = s; login_len = p - s; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st210;
		ctr305:
		{
			#line 140 "src/uri.rl"
			login = s; login_len = p - s; }
		{
			#line 103 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 1; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st210;
		st210:
		p+= 1;
		if ( p == pe )
		goto _test_eof210;
		st_case_210:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr258;
			}
			case 35: {
				goto ctr175;
			}
			case 37: {
				goto ctr259;
			}
			case 47: {
				goto ctr177;
			}
			case 58: {
				goto st148;
			}
			case 59: {
				goto ctr258;
			}
			case 61: {
				goto ctr258;
			}
			case 63: {
				goto ctr179;
			}
			case 64: {
				goto ctr261;
			}
			case 95: {
				goto ctr258;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto ctr258;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto ctr258;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr262;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr262;
			}
		} else {
			goto ctr260;
		}
		{
			goto st0;
		}
		ctr258:
		{
			#line 143 "src/uri.rl"
			s = p; }
		
		goto st211;
		st211:
		p+= 1;
		if ( p == pe )
		goto _test_eof211;
		st_case_211:
		switch( ( (*( p))) ) {
			case 33: {
				goto st211;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st124;
			}
			case 47: {
				goto st148;
			}
			case 58: {
				goto st148;
			}
			case 61: {
				goto st211;
			}
			case 63: {
				goto ctr155;
			}
			case 64: {
				goto ctr264;
			}
			case 95: {
				goto st211;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st211;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st211;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st211;
			}
		} else {
			goto st211;
		}
		{
			goto st0;
		}
		ctr259:
		{
			#line 143 "src/uri.rl"
			s = p; }
		
		goto st124;
		st124:
		p+= 1;
		if ( p == pe )
		goto _test_eof124;
		st_case_124:
		switch( ( (*( p))) ) {
			case 37: {
				goto st211;
			}
			case 117: {
				goto st125;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st211;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st211;
			}
		} else {
			goto st211;
		}
		{
			goto st0;
		}
		st125:
		p+= 1;
		if ( p == pe )
		goto _test_eof125;
		st_case_125:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st126;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st126;
			}
		} else {
			goto st126;
		}
		{
			goto st0;
		}
		st126:
		p+= 1;
		if ( p == pe )
		goto _test_eof126;
		st_case_126:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st127;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st127;
			}
		} else {
			goto st127;
		}
		{
			goto st0;
		}
		st127:
		p+= 1;
		if ( p == pe )
		goto _test_eof127;
		st_case_127:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st128;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st128;
			}
		} else {
			goto st128;
		}
		{
			goto st0;
		}
		st128:
		p+= 1;
		if ( p == pe )
		goto _test_eof128;
		st_case_128:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st211;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st211;
			}
		} else {
			goto st211;
		}
		{
			goto st0;
		}
		ctr264:
		{
			#line 144 "src/uri.rl"
			uri->password = s; uri->password_len = p - s; }
		{
			#line 148 "src/uri.rl"
			uri->login = login; uri->login_len = login_len; }
		
		goto st212;
		ctr257:
		{
			#line 140 "src/uri.rl"
			login = s; login_len = p - s; }
		{
			#line 148 "src/uri.rl"
			uri->login = login; uri->login_len = login_len; }
		
		goto st212;
		ctr261:
		{
			#line 143 "src/uri.rl"
			s = p; }
		{
			#line 144 "src/uri.rl"
			uri->password = s; uri->password_len = p - s; }
		{
			#line 148 "src/uri.rl"
			uri->login = login; uri->login_len = login_len; }
		
		goto st212;
		st212:
		p+= 1;
		if ( p == pe )
		goto _test_eof212;
		st_case_212:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr265;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto ctr266;
			}
			case 47: {
				goto st148;
			}
			case 58: {
				goto st148;
			}
			case 59: {
				goto ctr265;
			}
			case 61: {
				goto ctr265;
			}
			case 63: {
				goto ctr155;
			}
			case 64: {
				goto st148;
			}
			case 91: {
				goto st38;
			}
			case 95: {
				goto ctr265;
			}
			case 117: {
				goto ctr268;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto ctr265;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto ctr265;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr265;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr265;
			}
		} else {
			goto ctr267;
		}
		{
			goto st0;
		}
		ctr265:
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st213;
		st213:
		p+= 1;
		if ( p == pe )
		goto _test_eof213;
		st_case_213:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		ctr266:
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st129;
		st129:
		p+= 1;
		if ( p == pe )
		goto _test_eof129;
		st_case_129:
		switch( ( (*( p))) ) {
			case 37: {
				goto st213;
			}
			case 117: {
				goto st130;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		st130:
		p+= 1;
		if ( p == pe )
		goto _test_eof130;
		st_case_130:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st131;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st131;
			}
		} else {
			goto st131;
		}
		{
			goto st0;
		}
		st131:
		p+= 1;
		if ( p == pe )
		goto _test_eof131;
		st_case_131:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st132;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st132;
			}
		} else {
			goto st132;
		}
		{
			goto st0;
		}
		st132:
		p+= 1;
		if ( p == pe )
		goto _test_eof132;
		st_case_132:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st133;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st133;
			}
		} else {
			goto st133;
		}
		{
			goto st0;
		}
		st133:
		p+= 1;
		if ( p == pe )
		goto _test_eof133;
		st_case_133:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		ctr270:
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st214;
		ctr285:
		{
			#line 103 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;
			uri->host_hint = 1; }
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		
		goto st214;
		st214:
		p+= 1;
		if ( p == pe )
		goto _test_eof214;
		st_case_214:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr175;
			}
			case 37: {
				goto st11;
			}
			case 47: {
				goto ctr177;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr179;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 58 ) {
			if ( ( (*( p))) > 46 ) {
				if ( 48 <= ( (*( p))) ) {
					goto ctr271;
				}
			} else if ( ( (*( p))) >= 36 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 59 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto ctr272;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto ctr272;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		ctr271:
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st215;
		st215:
		p+= 1;
		if ( p == pe )
		goto _test_eof215;
		st_case_215:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr186;
			}
			case 37: {
				goto st11;
			}
			case 47: {
				goto ctr187;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr189;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 58 ) {
			if ( ( (*( p))) > 46 ) {
				if ( 48 <= ( (*( p))) ) {
					goto st215;
				}
			} else if ( ( (*( p))) >= 36 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 59 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st148;
				}
			} else if ( ( (*( p))) >= 64 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		ctr272:
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st216;
		st216:
		p+= 1;
		if ( p == pe )
		goto _test_eof216;
		st_case_216:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr186;
			}
			case 37: {
				goto st11;
			}
			case 47: {
				goto ctr187;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr189;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st216;
			}
		} else {
			goto st216;
		}
		{
			goto st0;
		}
		ctr267:
		{
			#line 102 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st217;
		st217:
		p+= 1;
		if ( p == pe )
		goto _test_eof217;
		st_case_217:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st218;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st230;
		}
		{
			goto st0;
		}
		st218:
		p+= 1;
		if ( p == pe )
		goto _test_eof218;
		st_case_218:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st219;
		}
		{
			goto st0;
		}
		st219:
		p+= 1;
		if ( p == pe )
		goto _test_eof219;
		st_case_219:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st220;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st228;
		}
		{
			goto st0;
		}
		st220:
		p+= 1;
		if ( p == pe )
		goto _test_eof220;
		st_case_220:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st221;
		}
		{
			goto st0;
		}
		st221:
		p+= 1;
		if ( p == pe )
		goto _test_eof221;
		st_case_221:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st222;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st226;
		}
		{
			goto st0;
		}
		st222:
		p+= 1;
		if ( p == pe )
		goto _test_eof222;
		st_case_222:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st223;
		}
		{
			goto st0;
		}
		st223:
		p+= 1;
		if ( p == pe )
		goto _test_eof223;
		st_case_223:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr285;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st224;
		}
		{
			goto st0;
		}
		st224:
		p+= 1;
		if ( p == pe )
		goto _test_eof224;
		st_case_224:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr285;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st225;
		}
		{
			goto st0;
		}
		st225:
		p+= 1;
		if ( p == pe )
		goto _test_eof225;
		st_case_225:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr285;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		st226:
		p+= 1;
		if ( p == pe )
		goto _test_eof226;
		st_case_226:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st222;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st227;
		}
		{
			goto st0;
		}
		st227:
		p+= 1;
		if ( p == pe )
		goto _test_eof227;
		st_case_227:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st222;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		st228:
		p+= 1;
		if ( p == pe )
		goto _test_eof228;
		st_case_228:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st220;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st229;
		}
		{
			goto st0;
		}
		st229:
		p+= 1;
		if ( p == pe )
		goto _test_eof229;
		st_case_229:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st220;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		st230:
		p+= 1;
		if ( p == pe )
		goto _test_eof230;
		st_case_230:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st218;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 59: {
				goto st213;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st213;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st213;
			}
		} else {
			goto st231;
		}
		{
			goto st0;
		}
		st231:
		p+= 1;
		if ( p == pe )
		goto _test_eof231;
		st_case_231:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 46: {
				goto st218;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		ctr268:
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st232;
		st232:
		p+= 1;
		if ( p == pe )
		goto _test_eof232;
		st_case_232:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 110: {
				goto st233;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		st233:
		p+= 1;
		if ( p == pe )
		goto _test_eof233;
		st_case_233:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 105: {
				goto st234;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		st234:
		p+= 1;
		if ( p == pe )
		goto _test_eof234;
		st_case_234:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 120: {
				goto st235;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		st235:
		p+= 1;
		if ( p == pe )
		goto _test_eof235;
		st_case_235:
		switch( ( (*( p))) ) {
			case 33: {
				goto st213;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st129;
			}
			case 47: {
				goto ctr217;
			}
			case 58: {
				goto ctr270;
			}
			case 61: {
				goto st213;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto st148;
			}
			case 95: {
				goto st213;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st213;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st213;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st213;
			}
		} else {
			goto st213;
		}
		{
			goto st0;
		}
		ctr260:
		{
			#line 143 "src/uri.rl"
			s = p; }
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st236;
		st236:
		p+= 1;
		if ( p == pe )
		goto _test_eof236;
		st_case_236:
		switch( ( (*( p))) ) {
			case 33: {
				goto st211;
			}
			case 35: {
				goto ctr186;
			}
			case 37: {
				goto st124;
			}
			case 47: {
				goto ctr187;
			}
			case 58: {
				goto st148;
			}
			case 59: {
				goto st211;
			}
			case 61: {
				goto st211;
			}
			case 63: {
				goto ctr189;
			}
			case 64: {
				goto ctr264;
			}
			case 95: {
				goto st211;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st211;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st211;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st211;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st211;
			}
		} else {
			goto st236;
		}
		{
			goto st0;
		}
		ctr262:
		{
			#line 143 "src/uri.rl"
			s = p; }
		{
			#line 133 "src/uri.rl"
			s = p; }
		
		goto st237;
		st237:
		p+= 1;
		if ( p == pe )
		goto _test_eof237;
		st_case_237:
		switch( ( (*( p))) ) {
			case 33: {
				goto st211;
			}
			case 35: {
				goto ctr186;
			}
			case 37: {
				goto st124;
			}
			case 47: {
				goto ctr187;
			}
			case 58: {
				goto st148;
			}
			case 61: {
				goto st211;
			}
			case 63: {
				goto ctr189;
			}
			case 64: {
				goto ctr264;
			}
			case 95: {
				goto st211;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st211;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st211;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st237;
			}
		} else {
			goto st237;
		}
		{
			goto st0;
		}
		ctr253:
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 102 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st238;
		st238:
		p+= 1;
		if ( p == pe )
		goto _test_eof238;
		st_case_238:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st239;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st251;
		}
		{
			goto st0;
		}
		st239:
		p+= 1;
		if ( p == pe )
		goto _test_eof239;
		st_case_239:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st240;
		}
		{
			goto st0;
		}
		st240:
		p+= 1;
		if ( p == pe )
		goto _test_eof240;
		st_case_240:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st241;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st249;
		}
		{
			goto st0;
		}
		st241:
		p+= 1;
		if ( p == pe )
		goto _test_eof241;
		st_case_241:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st242;
		}
		{
			goto st0;
		}
		st242:
		p+= 1;
		if ( p == pe )
		goto _test_eof242;
		st_case_242:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st243;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st247;
		}
		{
			goto st0;
		}
		st243:
		p+= 1;
		if ( p == pe )
		goto _test_eof243;
		st_case_243:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st244;
		}
		{
			goto st0;
		}
		st244:
		p+= 1;
		if ( p == pe )
		goto _test_eof244;
		st_case_244:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr305;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st245;
		}
		{
			goto st0;
		}
		st245:
		p+= 1;
		if ( p == pe )
		goto _test_eof245;
		st_case_245:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr305;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 46 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st246;
		}
		{
			goto st0;
		}
		st246:
		p+= 1;
		if ( p == pe )
		goto _test_eof246;
		st_case_246:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr201;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr202;
			}
			case 58: {
				goto ctr305;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr205;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		st247:
		p+= 1;
		if ( p == pe )
		goto _test_eof247;
		st_case_247:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st243;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st248;
		}
		{
			goto st0;
		}
		st248:
		p+= 1;
		if ( p == pe )
		goto _test_eof248;
		st_case_248:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st243;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		st249:
		p+= 1;
		if ( p == pe )
		goto _test_eof249;
		st_case_249:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st241;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st250;
		}
		{
			goto st0;
		}
		st250:
		p+= 1;
		if ( p == pe )
		goto _test_eof250;
		st_case_250:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st241;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		st251:
		p+= 1;
		if ( p == pe )
		goto _test_eof251;
		st_case_251:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st239;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 59: {
				goto st209;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 48 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 45 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st209;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st209;
			}
		} else {
			goto st252;
		}
		{
			goto st0;
		}
		st252:
		p+= 1;
		if ( p == pe )
		goto _test_eof252;
		st_case_252:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 46: {
				goto st239;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		ctr254:
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st253;
		st253:
		p+= 1;
		if ( p == pe )
		goto _test_eof253;
		st_case_253:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 110: {
				goto st254;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		st254:
		p+= 1;
		if ( p == pe )
		goto _test_eof254;
		st_case_254:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 105: {
				goto st255;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		st255:
		p+= 1;
		if ( p == pe )
		goto _test_eof255;
		st_case_255:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 120: {
				goto st256;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		st256:
		p+= 1;
		if ( p == pe )
		goto _test_eof256;
		st_case_256:
		switch( ( (*( p))) ) {
			case 33: {
				goto st209;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st119;
			}
			case 47: {
				goto ctr313;
			}
			case 58: {
				goto ctr256;
			}
			case 61: {
				goto st209;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr257;
			}
			case 95: {
				goto st209;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st209;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st209;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st209;
			}
		} else {
			goto st209;
		}
		{
			goto st0;
		}
		ctr313:
		{
			#line 96 "src/uri.rl"
			uri->host = s; uri->host_len = p - s;}
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st257;
		st257:
		p+= 1;
		if ( p == pe )
		goto _test_eof257;
		st_case_257:
		switch( ( (*( p))) ) {
			case 33: {
				goto st148;
			}
			case 35: {
				goto ctr151;
			}
			case 37: {
				goto st11;
			}
			case 58: {
				goto st258;
			}
			case 61: {
				goto st148;
			}
			case 63: {
				goto ctr155;
			}
			case 95: {
				goto st148;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st148;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st148;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st148;
			}
		} else {
			goto st148;
		}
		{
			goto st0;
		}
		st258:
		p+= 1;
		if ( p == pe )
		goto _test_eof258;
		st_case_258:
		switch( ( (*( p))) ) {
			case 33: {
				goto ctr315;
			}
			case 35: {
				goto ctr316;
			}
			case 37: {
				goto ctr317;
			}
			case 47: {
				goto ctr318;
			}
			case 58: {
				goto ctr222;
			}
			case 61: {
				goto ctr315;
			}
			case 63: {
				goto ctr319;
			}
			case 95: {
				goto ctr315;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto ctr315;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto ctr315;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto ctr315;
			}
		} else {
			goto ctr315;
		}
		{
			goto st0;
		}
		ctr315:
		{
			#line 130 "src/uri.rl"
			s = p;}
		
		goto st259;
		st259:
		p+= 1;
		if ( p == pe )
		goto _test_eof259;
		st_case_259:
		switch( ( (*( p))) ) {
			case 33: {
				goto st259;
			}
			case 35: {
				goto ctr320;
			}
			case 37: {
				goto st134;
			}
			case 47: {
				goto st260;
			}
			case 58: {
				goto ctr224;
			}
			case 61: {
				goto st259;
			}
			case 63: {
				goto ctr322;
			}
			case 95: {
				goto st259;
			}
			case 124: {
				goto st148;
			}
			case 126: {
				goto st259;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st259;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st259;
			}
		} else {
			goto st259;
		}
		{
			goto st0;
		}
		ctr317:
		{
			#line 130 "src/uri.rl"
			s = p;}
		
		goto st134;
		st134:
		p+= 1;
		if ( p == pe )
		goto _test_eof134;
		st_case_134:
		switch( ( (*( p))) ) {
			case 37: {
				goto st259;
			}
			case 117: {
				goto st135;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st259;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st259;
			}
		} else {
			goto st259;
		}
		{
			goto st0;
		}
		st135:
		p+= 1;
		if ( p == pe )
		goto _test_eof135;
		st_case_135:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st136;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st136;
			}
		} else {
			goto st136;
		}
		{
			goto st0;
		}
		st136:
		p+= 1;
		if ( p == pe )
		goto _test_eof136;
		st_case_136:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st137;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st137;
			}
		} else {
			goto st137;
		}
		{
			goto st0;
		}
		st137:
		p+= 1;
		if ( p == pe )
		goto _test_eof137;
		st_case_137:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st138;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st138;
			}
		} else {
			goto st138;
		}
		{
			goto st0;
		}
		st138:
		p+= 1;
		if ( p == pe )
		goto _test_eof138;
		st_case_138:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st259;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st259;
			}
		} else {
			goto st259;
		}
		{
			goto st0;
		}
		ctr326:
		{
			#line 163 "src/uri.rl"
			s = p; }
		
		goto st260;
		ctr318:
		{
			#line 130 "src/uri.rl"
			s = p;}
		
		goto st260;
		st260:
		p+= 1;
		if ( p == pe )
		goto _test_eof260;
		st_case_260:
		switch( ( (*( p))) ) {
			case 33: {
				goto st260;
			}
			case 35: {
				goto ctr320;
			}
			case 37: {
				goto st139;
			}
			case 58: {
				goto ctr324;
			}
			case 61: {
				goto st260;
			}
			case 63: {
				goto ctr322;
			}
			case 95: {
				goto st260;
			}
			case 124: {
				goto st260;
			}
			case 126: {
				goto st260;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st260;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st260;
			}
		} else {
			goto st260;
		}
		{
			goto st0;
		}
		st139:
		p+= 1;
		if ( p == pe )
		goto _test_eof139;
		st_case_139:
		switch( ( (*( p))) ) {
			case 37: {
				goto st260;
			}
			case 117: {
				goto st140;
			}
		}
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st260;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st260;
			}
		} else {
			goto st260;
		}
		{
			goto st0;
		}
		st140:
		p+= 1;
		if ( p == pe )
		goto _test_eof140;
		st_case_140:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st141;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st141;
			}
		} else {
			goto st141;
		}
		{
			goto st0;
		}
		st141:
		p+= 1;
		if ( p == pe )
		goto _test_eof141;
		st_case_141:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st142;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st142;
			}
		} else {
			goto st142;
		}
		{
			goto st0;
		}
		st142:
		p+= 1;
		if ( p == pe )
		goto _test_eof142;
		st_case_142:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st143;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st143;
			}
		} else {
			goto st143;
		}
		{
			goto st0;
		}
		st143:
		p+= 1;
		if ( p == pe )
		goto _test_eof143;
		st_case_143:
		if ( ( (*( p))) < 65 ) {
			if ( 48 <= ( (*( p))) && ( (*( p))) <= 57 ) {
				goto st260;
			}
		} else if ( ( (*( p))) > 70 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 102 ) {
				goto st260;
			}
		} else {
			goto st260;
		}
		{
			goto st0;
		}
		ctr324:
		{
			#line 114 "src/uri.rl"
			
			/*
			* This action is also called for path_* terms.
			* I absolutely have no idea why.
			*/
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		
		goto st261;
		st261:
		p+= 1;
		if ( p == pe )
		goto _test_eof261;
		st_case_261:
		switch( ( (*( p))) ) {
			case 33: {
				goto st260;
			}
			case 35: {
				goto ctr325;
			}
			case 37: {
				goto st139;
			}
			case 47: {
				goto ctr326;
			}
			case 58: {
				goto ctr324;
			}
			case 61: {
				goto st260;
			}
			case 63: {
				goto ctr327;
			}
			case 95: {
				goto st260;
			}
			case 124: {
				goto st260;
			}
			case 126: {
				goto st260;
			}
		}
		if ( ( (*( p))) < 64 ) {
			if ( 36 <= ( (*( p))) && ( (*( p))) <= 59 ) {
				goto st260;
			}
		} else if ( ( (*( p))) > 90 ) {
			if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
				goto st260;
			}
		} else {
			goto st260;
		}
		{
			goto st0;
		}
		ctr158:
		{
			#line 153 "src/uri.rl"
			s = p; }
		{
			#line 139 "src/uri.rl"
			s = p; }
		{
			#line 95 "src/uri.rl"
			s = p; }
		
		goto st262;
		st262:
		p+= 1;
		if ( p == pe )
		goto _test_eof262;
		st_case_262:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 43: {
				goto st205;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr248;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 110: {
				goto st263;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 45 ) {
			if ( 36 <= ( (*( p))) ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st205;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st205;
			}
		} else {
			goto st205;
		}
		{
			goto st0;
		}
		st263:
		p+= 1;
		if ( p == pe )
		goto _test_eof263;
		st_case_263:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 43: {
				goto st205;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr248;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 105: {
				goto st264;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 45 ) {
			if ( 36 <= ( (*( p))) ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st205;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st205;
			}
		} else {
			goto st205;
		}
		{
			goto st0;
		}
		st264:
		p+= 1;
		if ( p == pe )
		goto _test_eof264;
		st_case_264:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 43: {
				goto st205;
			}
			case 47: {
				goto ctr161;
			}
			case 58: {
				goto ctr248;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 120: {
				goto st265;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 45 ) {
			if ( 36 <= ( (*( p))) ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st205;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st205;
			}
		} else {
			goto st205;
		}
		{
			goto st0;
		}
		st265:
		p+= 1;
		if ( p == pe )
		goto _test_eof265;
		st_case_265:
		switch( ( (*( p))) ) {
			case 33: {
				goto st145;
			}
			case 35: {
				goto ctr159;
			}
			case 37: {
				goto st6;
			}
			case 43: {
				goto st205;
			}
			case 47: {
				goto ctr313;
			}
			case 58: {
				goto ctr248;
			}
			case 59: {
				goto st145;
			}
			case 61: {
				goto st145;
			}
			case 63: {
				goto ctr163;
			}
			case 64: {
				goto ctr164;
			}
			case 95: {
				goto st145;
			}
			case 126: {
				goto st145;
			}
		}
		if ( ( (*( p))) < 45 ) {
			if ( 36 <= ( (*( p))) ) {
				goto st145;
			}
		} else if ( ( (*( p))) > 57 ) {
			if ( ( (*( p))) > 90 ) {
				if ( 97 <= ( (*( p))) && ( (*( p))) <= 122 ) {
					goto st205;
				}
			} else if ( ( (*( p))) >= 65 ) {
				goto st205;
			}
		} else {
			goto st205;
		}
		{
			goto st0;
		}
		st_out:
		_test_eof145: cs = 145; goto _test_eof; 
		_test_eof146: cs = 146; goto _test_eof; 
		_test_eof147: cs = 147; goto _test_eof; 
		_test_eof1: cs = 1; goto _test_eof; 
		_test_eof2: cs = 2; goto _test_eof; 
		_test_eof3: cs = 3; goto _test_eof; 
		_test_eof4: cs = 4; goto _test_eof; 
		_test_eof5: cs = 5; goto _test_eof; 
		_test_eof6: cs = 6; goto _test_eof; 
		_test_eof7: cs = 7; goto _test_eof; 
		_test_eof8: cs = 8; goto _test_eof; 
		_test_eof9: cs = 9; goto _test_eof; 
		_test_eof10: cs = 10; goto _test_eof; 
		_test_eof148: cs = 148; goto _test_eof; 
		_test_eof11: cs = 11; goto _test_eof; 
		_test_eof12: cs = 12; goto _test_eof; 
		_test_eof13: cs = 13; goto _test_eof; 
		_test_eof14: cs = 14; goto _test_eof; 
		_test_eof15: cs = 15; goto _test_eof; 
		_test_eof149: cs = 149; goto _test_eof; 
		_test_eof150: cs = 150; goto _test_eof; 
		_test_eof16: cs = 16; goto _test_eof; 
		_test_eof17: cs = 17; goto _test_eof; 
		_test_eof18: cs = 18; goto _test_eof; 
		_test_eof19: cs = 19; goto _test_eof; 
		_test_eof20: cs = 20; goto _test_eof; 
		_test_eof151: cs = 151; goto _test_eof; 
		_test_eof21: cs = 21; goto _test_eof; 
		_test_eof22: cs = 22; goto _test_eof; 
		_test_eof23: cs = 23; goto _test_eof; 
		_test_eof24: cs = 24; goto _test_eof; 
		_test_eof25: cs = 25; goto _test_eof; 
		_test_eof26: cs = 26; goto _test_eof; 
		_test_eof27: cs = 27; goto _test_eof; 
		_test_eof152: cs = 152; goto _test_eof; 
		_test_eof28: cs = 28; goto _test_eof; 
		_test_eof29: cs = 29; goto _test_eof; 
		_test_eof30: cs = 30; goto _test_eof; 
		_test_eof31: cs = 31; goto _test_eof; 
		_test_eof32: cs = 32; goto _test_eof; 
		_test_eof153: cs = 153; goto _test_eof; 
		_test_eof154: cs = 154; goto _test_eof; 
		_test_eof155: cs = 155; goto _test_eof; 
		_test_eof156: cs = 156; goto _test_eof; 
		_test_eof157: cs = 157; goto _test_eof; 
		_test_eof33: cs = 33; goto _test_eof; 
		_test_eof34: cs = 34; goto _test_eof; 
		_test_eof35: cs = 35; goto _test_eof; 
		_test_eof36: cs = 36; goto _test_eof; 
		_test_eof37: cs = 37; goto _test_eof; 
		_test_eof158: cs = 158; goto _test_eof; 
		_test_eof159: cs = 159; goto _test_eof; 
		_test_eof160: cs = 160; goto _test_eof; 
		_test_eof161: cs = 161; goto _test_eof; 
		_test_eof162: cs = 162; goto _test_eof; 
		_test_eof163: cs = 163; goto _test_eof; 
		_test_eof164: cs = 164; goto _test_eof; 
		_test_eof165: cs = 165; goto _test_eof; 
		_test_eof166: cs = 166; goto _test_eof; 
		_test_eof167: cs = 167; goto _test_eof; 
		_test_eof168: cs = 168; goto _test_eof; 
		_test_eof169: cs = 169; goto _test_eof; 
		_test_eof170: cs = 170; goto _test_eof; 
		_test_eof171: cs = 171; goto _test_eof; 
		_test_eof172: cs = 172; goto _test_eof; 
		_test_eof38: cs = 38; goto _test_eof; 
		_test_eof39: cs = 39; goto _test_eof; 
		_test_eof40: cs = 40; goto _test_eof; 
		_test_eof41: cs = 41; goto _test_eof; 
		_test_eof42: cs = 42; goto _test_eof; 
		_test_eof43: cs = 43; goto _test_eof; 
		_test_eof44: cs = 44; goto _test_eof; 
		_test_eof45: cs = 45; goto _test_eof; 
		_test_eof46: cs = 46; goto _test_eof; 
		_test_eof47: cs = 47; goto _test_eof; 
		_test_eof48: cs = 48; goto _test_eof; 
		_test_eof49: cs = 49; goto _test_eof; 
		_test_eof50: cs = 50; goto _test_eof; 
		_test_eof51: cs = 51; goto _test_eof; 
		_test_eof52: cs = 52; goto _test_eof; 
		_test_eof53: cs = 53; goto _test_eof; 
		_test_eof54: cs = 54; goto _test_eof; 
		_test_eof55: cs = 55; goto _test_eof; 
		_test_eof56: cs = 56; goto _test_eof; 
		_test_eof57: cs = 57; goto _test_eof; 
		_test_eof58: cs = 58; goto _test_eof; 
		_test_eof59: cs = 59; goto _test_eof; 
		_test_eof60: cs = 60; goto _test_eof; 
		_test_eof61: cs = 61; goto _test_eof; 
		_test_eof62: cs = 62; goto _test_eof; 
		_test_eof63: cs = 63; goto _test_eof; 
		_test_eof64: cs = 64; goto _test_eof; 
		_test_eof65: cs = 65; goto _test_eof; 
		_test_eof66: cs = 66; goto _test_eof; 
		_test_eof67: cs = 67; goto _test_eof; 
		_test_eof68: cs = 68; goto _test_eof; 
		_test_eof69: cs = 69; goto _test_eof; 
		_test_eof70: cs = 70; goto _test_eof; 
		_test_eof71: cs = 71; goto _test_eof; 
		_test_eof72: cs = 72; goto _test_eof; 
		_test_eof73: cs = 73; goto _test_eof; 
		_test_eof74: cs = 74; goto _test_eof; 
		_test_eof75: cs = 75; goto _test_eof; 
		_test_eof76: cs = 76; goto _test_eof; 
		_test_eof77: cs = 77; goto _test_eof; 
		_test_eof78: cs = 78; goto _test_eof; 
		_test_eof79: cs = 79; goto _test_eof; 
		_test_eof80: cs = 80; goto _test_eof; 
		_test_eof81: cs = 81; goto _test_eof; 
		_test_eof82: cs = 82; goto _test_eof; 
		_test_eof173: cs = 173; goto _test_eof; 
		_test_eof83: cs = 83; goto _test_eof; 
		_test_eof84: cs = 84; goto _test_eof; 
		_test_eof85: cs = 85; goto _test_eof; 
		_test_eof86: cs = 86; goto _test_eof; 
		_test_eof87: cs = 87; goto _test_eof; 
		_test_eof88: cs = 88; goto _test_eof; 
		_test_eof89: cs = 89; goto _test_eof; 
		_test_eof90: cs = 90; goto _test_eof; 
		_test_eof91: cs = 91; goto _test_eof; 
		_test_eof92: cs = 92; goto _test_eof; 
		_test_eof93: cs = 93; goto _test_eof; 
		_test_eof94: cs = 94; goto _test_eof; 
		_test_eof95: cs = 95; goto _test_eof; 
		_test_eof96: cs = 96; goto _test_eof; 
		_test_eof97: cs = 97; goto _test_eof; 
		_test_eof98: cs = 98; goto _test_eof; 
		_test_eof99: cs = 99; goto _test_eof; 
		_test_eof100: cs = 100; goto _test_eof; 
		_test_eof101: cs = 101; goto _test_eof; 
		_test_eof102: cs = 102; goto _test_eof; 
		_test_eof103: cs = 103; goto _test_eof; 
		_test_eof174: cs = 174; goto _test_eof; 
		_test_eof175: cs = 175; goto _test_eof; 
		_test_eof176: cs = 176; goto _test_eof; 
		_test_eof177: cs = 177; goto _test_eof; 
		_test_eof178: cs = 178; goto _test_eof; 
		_test_eof179: cs = 179; goto _test_eof; 
		_test_eof180: cs = 180; goto _test_eof; 
		_test_eof104: cs = 104; goto _test_eof; 
		_test_eof105: cs = 105; goto _test_eof; 
		_test_eof106: cs = 106; goto _test_eof; 
		_test_eof107: cs = 107; goto _test_eof; 
		_test_eof108: cs = 108; goto _test_eof; 
		_test_eof181: cs = 181; goto _test_eof; 
		_test_eof109: cs = 109; goto _test_eof; 
		_test_eof110: cs = 110; goto _test_eof; 
		_test_eof111: cs = 111; goto _test_eof; 
		_test_eof112: cs = 112; goto _test_eof; 
		_test_eof113: cs = 113; goto _test_eof; 
		_test_eof182: cs = 182; goto _test_eof; 
		_test_eof183: cs = 183; goto _test_eof; 
		_test_eof184: cs = 184; goto _test_eof; 
		_test_eof185: cs = 185; goto _test_eof; 
		_test_eof186: cs = 186; goto _test_eof; 
		_test_eof187: cs = 187; goto _test_eof; 
		_test_eof114: cs = 114; goto _test_eof; 
		_test_eof115: cs = 115; goto _test_eof; 
		_test_eof116: cs = 116; goto _test_eof; 
		_test_eof117: cs = 117; goto _test_eof; 
		_test_eof118: cs = 118; goto _test_eof; 
		_test_eof188: cs = 188; goto _test_eof; 
		_test_eof189: cs = 189; goto _test_eof; 
		_test_eof190: cs = 190; goto _test_eof; 
		_test_eof191: cs = 191; goto _test_eof; 
		_test_eof192: cs = 192; goto _test_eof; 
		_test_eof193: cs = 193; goto _test_eof; 
		_test_eof194: cs = 194; goto _test_eof; 
		_test_eof195: cs = 195; goto _test_eof; 
		_test_eof196: cs = 196; goto _test_eof; 
		_test_eof197: cs = 197; goto _test_eof; 
		_test_eof198: cs = 198; goto _test_eof; 
		_test_eof199: cs = 199; goto _test_eof; 
		_test_eof200: cs = 200; goto _test_eof; 
		_test_eof201: cs = 201; goto _test_eof; 
		_test_eof202: cs = 202; goto _test_eof; 
		_test_eof203: cs = 203; goto _test_eof; 
		_test_eof204: cs = 204; goto _test_eof; 
		_test_eof205: cs = 205; goto _test_eof; 
		_test_eof206: cs = 206; goto _test_eof; 
		_test_eof207: cs = 207; goto _test_eof; 
		_test_eof208: cs = 208; goto _test_eof; 
		_test_eof209: cs = 209; goto _test_eof; 
		_test_eof119: cs = 119; goto _test_eof; 
		_test_eof120: cs = 120; goto _test_eof; 
		_test_eof121: cs = 121; goto _test_eof; 
		_test_eof122: cs = 122; goto _test_eof; 
		_test_eof123: cs = 123; goto _test_eof; 
		_test_eof210: cs = 210; goto _test_eof; 
		_test_eof211: cs = 211; goto _test_eof; 
		_test_eof124: cs = 124; goto _test_eof; 
		_test_eof125: cs = 125; goto _test_eof; 
		_test_eof126: cs = 126; goto _test_eof; 
		_test_eof127: cs = 127; goto _test_eof; 
		_test_eof128: cs = 128; goto _test_eof; 
		_test_eof212: cs = 212; goto _test_eof; 
		_test_eof213: cs = 213; goto _test_eof; 
		_test_eof129: cs = 129; goto _test_eof; 
		_test_eof130: cs = 130; goto _test_eof; 
		_test_eof131: cs = 131; goto _test_eof; 
		_test_eof132: cs = 132; goto _test_eof; 
		_test_eof133: cs = 133; goto _test_eof; 
		_test_eof214: cs = 214; goto _test_eof; 
		_test_eof215: cs = 215; goto _test_eof; 
		_test_eof216: cs = 216; goto _test_eof; 
		_test_eof217: cs = 217; goto _test_eof; 
		_test_eof218: cs = 218; goto _test_eof; 
		_test_eof219: cs = 219; goto _test_eof; 
		_test_eof220: cs = 220; goto _test_eof; 
		_test_eof221: cs = 221; goto _test_eof; 
		_test_eof222: cs = 222; goto _test_eof; 
		_test_eof223: cs = 223; goto _test_eof; 
		_test_eof224: cs = 224; goto _test_eof; 
		_test_eof225: cs = 225; goto _test_eof; 
		_test_eof226: cs = 226; goto _test_eof; 
		_test_eof227: cs = 227; goto _test_eof; 
		_test_eof228: cs = 228; goto _test_eof; 
		_test_eof229: cs = 229; goto _test_eof; 
		_test_eof230: cs = 230; goto _test_eof; 
		_test_eof231: cs = 231; goto _test_eof; 
		_test_eof232: cs = 232; goto _test_eof; 
		_test_eof233: cs = 233; goto _test_eof; 
		_test_eof234: cs = 234; goto _test_eof; 
		_test_eof235: cs = 235; goto _test_eof; 
		_test_eof236: cs = 236; goto _test_eof; 
		_test_eof237: cs = 237; goto _test_eof; 
		_test_eof238: cs = 238; goto _test_eof; 
		_test_eof239: cs = 239; goto _test_eof; 
		_test_eof240: cs = 240; goto _test_eof; 
		_test_eof241: cs = 241; goto _test_eof; 
		_test_eof242: cs = 242; goto _test_eof; 
		_test_eof243: cs = 243; goto _test_eof; 
		_test_eof244: cs = 244; goto _test_eof; 
		_test_eof245: cs = 245; goto _test_eof; 
		_test_eof246: cs = 246; goto _test_eof; 
		_test_eof247: cs = 247; goto _test_eof; 
		_test_eof248: cs = 248; goto _test_eof; 
		_test_eof249: cs = 249; goto _test_eof; 
		_test_eof250: cs = 250; goto _test_eof; 
		_test_eof251: cs = 251; goto _test_eof; 
		_test_eof252: cs = 252; goto _test_eof; 
		_test_eof253: cs = 253; goto _test_eof; 
		_test_eof254: cs = 254; goto _test_eof; 
		_test_eof255: cs = 255; goto _test_eof; 
		_test_eof256: cs = 256; goto _test_eof; 
		_test_eof257: cs = 257; goto _test_eof; 
		_test_eof258: cs = 258; goto _test_eof; 
		_test_eof259: cs = 259; goto _test_eof; 
		_test_eof134: cs = 134; goto _test_eof; 
		_test_eof135: cs = 135; goto _test_eof; 
		_test_eof136: cs = 136; goto _test_eof; 
		_test_eof137: cs = 137; goto _test_eof; 
		_test_eof138: cs = 138; goto _test_eof; 
		_test_eof260: cs = 260; goto _test_eof; 
		_test_eof139: cs = 139; goto _test_eof; 
		_test_eof140: cs = 140; goto _test_eof; 
		_test_eof141: cs = 141; goto _test_eof; 
		_test_eof142: cs = 142; goto _test_eof; 
		_test_eof143: cs = 143; goto _test_eof; 
		_test_eof261: cs = 261; goto _test_eof; 
		_test_eof262: cs = 262; goto _test_eof; 
		_test_eof263: cs = 263; goto _test_eof; 
		_test_eof264: cs = 264; goto _test_eof; 
		_test_eof265: cs = 265; goto _test_eof; 
		
		_test_eof: {}
		if ( p == eof )
		{
			switch ( cs ) {
				case 150: 
				{
					#line 72 "src/uri.rl"
					uri->query = s; uri->query_len = p - s; }
				break;
				case 147: 
				{
					#line 76 "src/uri.rl"
					uri->fragment = s; uri->fragment_len = p - s; }
				break;
				case 156: 
				
				case 157: 
				{
					#line 114 "src/uri.rl"
					
					/*
					* This action is also called for path_* terms.
					* I absolutely have no idea why.
					*/
					if (uri->host_hint != 3) {
						uri->host_hint = 3;
						uri->host = URI_HOST_UNIX;
						uri->host_len = strlen(URI_HOST_UNIX);
						uri->service = s; uri->service_len = p - s;
						/* a workaround for grammar limitations */
						uri->path = NULL;
						uri->path_len = 0;
					};
				}
				break;
				case 144: 
				
				case 148: 
				
				case 178: 
				
				case 179: 
				
				case 180: 
				
				case 181: 
				
				case 204: 
				
				case 207: 
				
				case 208: 
				
				case 211: 
				
				case 212: 
				
				case 257: 
				{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }
				break;
				case 149: 
				{
					#line 71 "src/uri.rl"
					s = p; }	{
					#line 72 "src/uri.rl"
					uri->query = s; uri->query_len = p - s; }
				break;
				case 146: 
				{
					#line 75 "src/uri.rl"
					s = p; }	{
					#line 76 "src/uri.rl"
					uri->fragment = s; uri->fragment_len = p - s; }
				break;
				case 173: 
				
				case 182: 
				
				case 183: 
				{
					#line 163 "src/uri.rl"
					s = p; }	{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }
				break;
				case 186: 
				
				case 187: 
				
				case 259: 
				
				case 260: 
				{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }	{
					#line 114 "src/uri.rl"
					
					/*
					* This action is also called for path_* terms.
					* I absolutely have no idea why.
					*/
					if (uri->host_hint != 3) {
						uri->host_hint = 3;
						uri->host = URI_HOST_UNIX;
						uri->host_len = strlen(URI_HOST_UNIX);
						uri->service = s; uri->service_len = p - s;
						/* a workaround for grammar limitations */
						uri->path = NULL;
						uri->path_len = 0;
					};
				}
				break;
				case 145: 
				
				case 152: 
				
				case 158: 
				
				case 159: 
				
				case 160: 
				
				case 161: 
				
				case 162: 
				
				case 163: 
				
				case 167: 
				
				case 168: 
				
				case 169: 
				
				case 170: 
				
				case 171: 
				
				case 172: 
				
				case 174: 
				
				case 175: 
				
				case 176: 
				
				case 177: 
				
				case 189: 
				
				case 190: 
				
				case 191: 
				
				case 192: 
				
				case 193: 
				
				case 197: 
				
				case 198: 
				
				case 199: 
				
				case 200: 
				
				case 205: 
				
				case 209: 
				
				case 213: 
				
				case 217: 
				
				case 218: 
				
				case 219: 
				
				case 220: 
				
				case 221: 
				
				case 222: 
				
				case 226: 
				
				case 227: 
				
				case 228: 
				
				case 229: 
				
				case 230: 
				
				case 231: 
				
				case 232: 
				
				case 233: 
				
				case 234: 
				
				case 235: 
				
				case 238: 
				
				case 239: 
				
				case 240: 
				
				case 241: 
				
				case 242: 
				
				case 243: 
				
				case 247: 
				
				case 248: 
				
				case 249: 
				
				case 250: 
				
				case 251: 
				
				case 252: 
				
				case 253: 
				
				case 254: 
				
				case 255: 
				
				case 256: 
				
				case 262: 
				
				case 263: 
				
				case 264: 
				
				case 265: 
				{
					#line 96 "src/uri.rl"
					uri->host = s; uri->host_len = p - s;}	{
					#line 163 "src/uri.rl"
					s = p; }	{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }
				break;
				case 154: 
				
				case 155: 
				
				case 184: 
				
				case 185: 
				
				case 215: 
				
				case 216: 
				
				case 236: 
				
				case 237: 
				{
					#line 134 "src/uri.rl"
					uri->service = s; uri->service_len = p - s; }	{
					#line 163 "src/uri.rl"
					s = p; }	{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }
				break;
				case 261: 
				{
					#line 163 "src/uri.rl"
					s = p; }	{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }	{
					#line 114 "src/uri.rl"
					
					/*
					* This action is also called for path_* terms.
					* I absolutely have no idea why.
					*/
					if (uri->host_hint != 3) {
						uri->host_hint = 3;
						uri->host = URI_HOST_UNIX;
						uri->host_len = strlen(URI_HOST_UNIX);
						uri->service = s; uri->service_len = p - s;
						/* a workaround for grammar limitations */
						uri->path = NULL;
						uri->path_len = 0;
					};
				}
				break;
				case 258: 
				{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }	{
					#line 130 "src/uri.rl"
					s = p;}	{
					#line 114 "src/uri.rl"
					
					/*
					* This action is also called for path_* terms.
					* I absolutely have no idea why.
					*/
					if (uri->host_hint != 3) {
						uri->host_hint = 3;
						uri->host = URI_HOST_UNIX;
						uri->host_len = strlen(URI_HOST_UNIX);
						uri->service = s; uri->service_len = p - s;
						/* a workaround for grammar limitations */
						uri->path = NULL;
						uri->path_len = 0;
					};
				}
				break;
				case 188: 
				
				case 201: 
				
				case 202: 
				
				case 203: 
				{
					#line 96 "src/uri.rl"
					uri->host = s; uri->host_len = p - s;}	{
					#line 163 "src/uri.rl"
					s = p; }	{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }	{
					#line 181 "src/uri.rl"
					uri->service_len = p - uri->service;
					uri->host = NULL; uri->host_len = 0; }
				break;
				case 164: 
				
				case 165: 
				
				case 166: 
				
				case 194: 
				
				case 195: 
				
				case 196: 
				
				case 223: 
				
				case 224: 
				
				case 225: 
				
				case 244: 
				
				case 245: 
				
				case 246: 
				{
					#line 103 "src/uri.rl"
					uri->host = s; uri->host_len = p - s;
					uri->host_hint = 1; }	{
					#line 96 "src/uri.rl"
					uri->host = s; uri->host_len = p - s;}	{
					#line 163 "src/uri.rl"
					s = p; }	{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }
				break;
				case 151: 
				
				case 153: 
				
				case 206: 
				
				case 210: 
				
				case 214: 
				{
					#line 133 "src/uri.rl"
					s = p; }	{
					#line 134 "src/uri.rl"
					uri->service = s; uri->service_len = p - s; }	{
					#line 163 "src/uri.rl"
					s = p; }	{
					#line 167 "src/uri.rl"
					uri->path = s; uri->path_len = p - s; }
				break;
			}
		}
		
		_out: {}
	}
	
	#line 194 "src/uri.rl"
	
	
	if (uri->path_len == 0)
	uri->path = NULL;
	if (uri->service_len == 0)
	uri->service = NULL;
	if (uri->service_len >= URI_MAXSERVICE)
	return -1;
	if (uri->host_len >= URI_MAXHOST)
	return -1;
	
	(void)uri_first_final;
	(void)uri_error;
	(void)uri_en_main;
	(void)eof;
	
	return cs >= uri_first_final ? 0 : -1;
}

int
uri_format(char *str, int len, const struct uri *uri, bool write_password)
{
	int total = 0;
	if (uri->scheme_len > 0) {
		SNPRINT(total, snprintf, str, len, "%.*s://",
		(int)uri->scheme_len, uri->scheme);
	}
	if (uri->host_len > 0) {
		if (uri->login_len > 0) {
			SNPRINT(total, snprintf, str, len, "%.*s",
			(int)uri->login_len, uri->login);
			if (uri->password_len > 0 && write_password) {
				SNPRINT(total, snprintf, str, len, ":%.*s",
				(int)uri->password_len,
				uri->password);
			}
			SNPRINT(total, snprintf, str, len, "@");
		}
		SNPRINT(total, snprintf, str, len, "%.*s",
		(int)uri->host_len, uri->host);
		if (uri->service_len > 0) {
			SNPRINT(total, snprintf, str, len, ":%.*s",
			(int)uri->service_len, uri->service);
		}
	}
	if (uri->path_len > 0) {
		SNPRINT(total, snprintf, str, len, "%.*s",
		(int)uri->path_len, uri->path);
	}
	if (uri->query_len > 0) {
		SNPRINT(total, snprintf, str, len, "?%.*s",
		(int)uri->query_len, uri->query);
	}
	if (uri->fragment_len > 0) {
		SNPRINT(total, snprintf, str, len, "#%.*s",
		(int)uri->fragment_len, uri->fragment);
	}
	return total;
}

/* vim: set ft=ragel: */
