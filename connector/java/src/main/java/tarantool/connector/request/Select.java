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
import tarantool.connector.Constans;
import tarantool.connector.Request;

public class Select extends Request {
    int _index;
    int _limit;
    int _offset;
    int _space;
    byte[][][] _tuples;

    public Select(int space, int index, int offset, int limit,
            byte[][]... tuples) {
        super();
        _space = space;
        _index = index;
        _offset = offset;
        _limit = limit;
        _tuples = tuples;
    }

    @Override
    public byte[] toByte() {
        int length = Constans.SELECT_REQUEST_BODY + Constans.HEADER_LENGTH;
        length += 4 * _tuples.length;
        for (final byte[][] i : _tuples)
            for (final byte[] j : i)
                length += ByteUtil.sizeOfInVarInt32(j.length) + j.length;

        final byte[] body = new byte[length];
        int offset = 0;

        offset = ByteUtil.writeInteger(body, offset, Constans.REQ_TYPE_SELECT);
        offset += ByteUtil.writeInteger(body, offset, length
                - Constans.HEADER_LENGTH);
        offset += ByteUtil.writeInteger(body, offset, _reqId);

        offset += ByteUtil.writeInteger(body, offset, _space);
        offset += ByteUtil.writeInteger(body, offset, _index);
        offset += ByteUtil.writeInteger(body, offset, _offset);
        offset += ByteUtil.writeInteger(body, offset, _limit);
        offset += ByteUtil.writeInteger(body, offset, _tuples.length);
        for (final byte[][] i : _tuples) {
            offset += ByteUtil.writeInteger(body, offset, i.length);
            for (final byte[] j : i) {
                offset += ByteUtil.encodeLengthInVar32Int(body, offset,
                        j.length);
                offset += ByteUtil.writeBytes(body, offset, j, 0, j.length);
            }
        }
        return body;
    }
}