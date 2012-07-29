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
import tarantool.common.Constants;
import tarantool.connector.Operation;
import tarantool.connector.Request;

public class Update extends Request {
    int _count;
    int _flags;
    Operation[] _operation;
    byte[][] _ops;

    int _space;
    byte[][] _tuple;

    Update(int space, int flags, byte[][] tuple, Operation... ops) {
        super();
        _space = space;
        _flags = flags;
        _tuple = tuple;
        _count = ops.length;
        _operation = ops;

        _ops = new byte[_operation.length][];
        for (int i = 0; i < _operation.length; ++i) {
            _ops[i] = _operation[i].toByte();
            if (_ops[i].length == 0)
                _count--;
        }
    }

    @Override
    public byte[] toByte() {
        int length = Constants.UPDATE_REQUEST_BODY + Constants.HEADER_LENGTH;
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
        offset += ByteUtil.writeInteger(body, offset, _count);
        for (final byte[] op : _ops)
            offset += ByteUtil.writeBytes(body, offset, op, 0, op.length);
        return body;
    }
}