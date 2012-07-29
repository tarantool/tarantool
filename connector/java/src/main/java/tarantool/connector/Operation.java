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

package tarantool.connector;

import tarantool.common.ByteUtil;

public class Operation {
    byte[] _field;
    int _field_no;
    byte[] _length;
    byte[] _offset;
    int _op_code;

    public Operation(int field_no, byte[] offset, byte[] length, byte[] field) {
        _field_no = field_no;
        _op_code = Constants.OP_SPLICE;

        _offset = offset;
        _length = length;
        _field = field;

    }

    public Operation(int field_no, int op_code, byte[] field) {
        _field_no = field_no;
        _op_code = op_code;

        _offset = new byte[0];
        _length = new byte[0];
        _field = field;
    }

    public byte[] toByte() {
        int length = 2 * 4;
        length += _offset.length + _length.length + _field.length;
        length += (_offset.length == 0 ? 0 : ByteUtil
                .sizeOfInVarInt32(_offset.length));
        length += (_length.length == 0 ? 0 : ByteUtil
                .sizeOfInVarInt32(_length.length));
        length += ByteUtil.sizeOfInVarInt32(_field.length);

        final byte[] body = new byte[length];

        int offset = 0;
        offset += ByteUtil.writeInteger(body, offset, _field_no);
        body[offset] = (byte) _op_code;
        offset++;
        if (_offset.length != 0) {
            offset += ByteUtil.encodeLengthInVar32Int(body, offset,
                    _offset.length);
            offset += ByteUtil.writeBytes(body, offset, _offset, 0,
                    _offset.length);
        }
        if (_length.length != 0) {
            offset += ByteUtil.encodeLengthInVar32Int(body, offset,
                    _length.length);
            offset += ByteUtil.writeBytes(body, offset, _length, 0,
                    _length.length);
        }
        offset += ByteUtil.encodeLengthInVar32Int(body, offset, _field.length);
        offset += ByteUtil.writeBytes(body, offset, _field, 0, _field.length);

        return body;
    }
}
