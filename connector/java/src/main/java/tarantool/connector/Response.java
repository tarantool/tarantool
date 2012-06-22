package tarantool.connector;

import java.util.Arrays;

import tarantool.common.ByteUtil;

public class Response{
    final int _type;
    final int _bodyLen;
    final int _reqId;
    
    final int _count;
    final int _error;

	final byte[][][] _tuple;

	public Response(){
		_type = 0;
		_bodyLen = 0;
		_reqId = 0;
		_count = 0;
		_error = 0;
		_tuple = new byte[0][0][0];
	}
	
    public Response(byte[] header, byte[] body) {
    	
		//System.out.println(Arrays.toString(header));
		//System.out.println(Arrays.toString(body));

    	
        _type = ByteUtil.readInteger(header, 0); 
    	_bodyLen = ByteUtil.readInteger(header, 4); 
        _reqId = ByteUtil.readInteger(header, 8); 
        
        if (body.length == 0){
        	_error = 0;
        	_count = 0;
        	_tuple = new byte[0][0][0];
        	return;
        }
        
        int offset = 0;
        _error = ByteUtil.readInteger(body, offset); 
        offset += ByteUtil.LENGTH_INTEGER;
        
        if (body.length == 4 | _error != 0){
        	_count = 0;
        	_tuple = new byte[0][0][0];
        	return;
        }
        
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
            	offset += ByteUtil.readBytes(body, offset, _tuple[i][j], 0, length);
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
