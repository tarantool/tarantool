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
import tarantool.connector.Constants;
import tarantool.connector.Request;

public class Call extends Request {
    int _flags;
    byte[] _proc;
    byte[][] _tuple;

    public Call(int flags, byte[] proc, byte[][] tuple) {
        super();
        _flags = flags;
        _proc = proc;
        _tuple = tuple;
    }

    public Call(int flags, String proc, byte[][] tuple) {
        super();
        _flags = flags;
        _proc = proc.getBytes();
        _tuple = tuple;
    }

    @Override
    public byte[] toByte() {
        int length = Constants.CALL_REQUEST_BODY + Constants.HEADER_LENGTH;
        length += _proc.length + ByteUtil.sizeOfInVarInt32(_proc.length);
        length += 4;
        for (final byte[] i : _tuple)
            length += i.length + ByteUtil.sizeOfInVarInt32(i.length);

        final byte[] body = new byte[length];
        int offset = 0;

        offset = ByteUtil.writeInteger(body, offset, Constants.REQ_TYPE_CALL);
        offset += ByteUtil.writeInteger(body, offset, length
                - Constants.HEADER_LENGTH);
        offset += ByteUtil.writeInteger(body, offset, _reqId);

        offset += ByteUtil.writeInteger(body, offset, _flags);
        offset += ByteUtil.encodeLengthInVar32Int(body, offset, _proc.length);
        offset += ByteUtil.writeBytes(body, offset, _proc, 0, _proc.length);
        offset += ByteUtil.writeInteger(body, offset, _tuple.length);
        for (final byte[] i : _tuple) {
            offset += ByteUtil.encodeLengthInVar32Int(body, offset, i.length);
            offset += ByteUtil.writeBytes(body, offset, i, 0, i.length);
        }
        return body;
    }
}
