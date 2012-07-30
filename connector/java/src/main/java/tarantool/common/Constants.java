/*
 * Copyright (C) 2012 Mail.RU
 * Copyright (C) 2012 Eugine Blikh
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

package tarantool.common;

public class Constants {
    //Flags
	static public final int BOX_RETURN_TUPLE = 1;
    static public final int BOX_ADD = 2;
    static public final int BOX_REPLACE = 4;
    static public final int BOX_NOT_STORE = 16;
    
    //Update operations
    static public final int OP_ASSIGN = 0;
    static public final int OP_ADD = 1;
    static public final int OP_AND = 2;
    static public final int OP_XOR = 3;
    static public final int OP_OR = 4;
    static public final int OP_SPLICE = 5;
    static public final int OP_DELETE = 6;
    static public final int OP_INSERT = 7;
    
    //Id of Ops
    static public final int REQ_TYPE_INSERT = 13;
    static public final int REQ_TYPE_SELECT = 17;
    static public final int REQ_TYPE_UPDATE = 19;
    static public final int REQ_TYPE_DELETE = 21;
    static public final int REQ_TYPE_CALL = 22;
    static public final int REQ_TYPE_PING = 65280;
    
    //Length of Ops Bodies and Headers
    static public final int HEADER_LENGTH = 12;
    static public final int INSERT_REQUEST_BODY = 8;
    static public final int SELECT_REQUEST_BODY = 20;
    static public final int UPDATE_REQUEST_BODY = 12;
    static public final int DELETE_REQUEST_BODY = 8;
    static public final int CALL_REQUEST_BODY = 4;
}
