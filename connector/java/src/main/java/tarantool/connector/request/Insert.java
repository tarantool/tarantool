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

package tarantool.connector.request;

import tarantool.common.ByteUtil;
import tarantool.common.Tuple;
import tarantool.connector.Constants;
import tarantool.connector.Request;

public class Insert extends Request {
    int _flags;
    int _space;
    byte[][] _tuple;

    public Insert(int space, int flags, byte[]... tuple) {
        super();
        _space = space;
        _flags = flags;
        _tuple = tuple;
    }

    public Insert(int space, int flags, Tuple tuple) {
        super();
        _space = space;
        _flags = flags;
        _tuple = tuple.toByte();
    }

    @Override
    public byte[] toByte() {
        int length = Constants.INSERT_REQUEST_BODY + Constants.HEADER_LENGTH;
        length += 4;
        for (final byte[] i : _tuple)
            length += i.length + ByteUtil.sizeOfInVarInt32(i.length);

        final byte[] body = new byte[length];
        int offset = 0;

        offset = ByteUtil.writeInteger(body, offset, Constants.REQ_TYPE_INSERT);
        offset += ByteUtil.writeInteger(body, offset, length
                - Constants.HEADER_LENGTH);
        offset += ByteUtil.writeInteger(body, offset, _reqId);

        offset += ByteUtil.writeInteger(body, offset, _space);
        offset += ByteUtil.writeInteger(body, offset, _flags);
        offset += ByteUtil.writeInteger(body, offset, _tuple.length);
        for (final byte[] i : _tuple) {
            offset += ByteUtil.encodeLengthInVar32Int(body, offset, i.length);
            offset += ByteUtil.writeBytes(body, offset, i, 0, i.length);
        }
        return body;
    }
}