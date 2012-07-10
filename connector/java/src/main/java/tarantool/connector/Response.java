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

import java.io.UnsupportedEncodingException;

import tarantool.common.ByteUtil;

public class Response {
    final int _type;
    final int _bodyLen;
    final int _reqId;
    
    final int _count;
    final int _error;
    final String _err_str;

	final byte[][][] _tuple;

	public Response() {
		_type = 0;
		_bodyLen = 0;
		_reqId = 0;
		_count = 0;
		_error = 0;
		_err_str = null;
		_tuple = new byte[0][0][0];
	}
	
    public Response(byte[] header, byte[] body) throws 
    UnsupportedEncodingException {
    	_type = ByteUtil.readInteger(header, 0); 
    	_bodyLen = ByteUtil.readInteger(header, 4); 
        _reqId = ByteUtil.readInteger(header, 8); 
        
        if (body.length == 0){
        	_error = 0;
        	_err_str = null;
        	_count = 0;
        	_tuple = new byte[0][0][0];
        	return;
        }
        
        int offset = 0;
        _error = ByteUtil.readInteger(body, offset); 
        offset += ByteUtil.LENGTH_INTEGER;
        
        if (body.length == 4 | _error != 0){
        	_count = 0;
        	_err_str = new String(body, offset, 
        			body.length - offset, "ISO8859_1");
        	_tuple = new byte[0][0][0];
        	return;
        }
        
        _err_str = null;
        _count = ByteUtil.readInteger(body, offset);
        offset += ByteUtil.LENGTH_INTEGER;
        if (body.length == 8){
        	_tuple = new byte[0][0][0];
        	return;
        }
        
        if (_count != 0)
        	_tuple = new byte[_count][][];
        else
        	_tuple = new byte[0][0][0];
        
        for (int i = 0; i < _count; ++i){
        	int cardinality = ByteUtil.readInteger(body, offset + 4);
        	offset += ByteUtil.LENGTH_INTEGER + 4;
            _tuple[i] = new byte[cardinality][];
            for (int j = 0; j < cardinality; ++j){
            	int length = ByteUtil.decodeLengthInVar32Int(body, offset);
            	offset += ByteUtil.sizeOfInVarInt32(length);
            	_tuple[i][j] = new byte[length];
            	offset += ByteUtil.readBytes(body, offset, 
            			_tuple[i][j], 0, length);
            }
        }
    }
    
    public int get_reqId() {
		return _reqId;
	}

	public int get_count() {
		return _count;
	}

	public int get_error() {
		return _error;
	}

	public byte[][][] get_tuple() {
		return _tuple;
	}
}
