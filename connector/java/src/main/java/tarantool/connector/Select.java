package tarantool.connector;

import tarantool.common.ByteUtil;
import tarantool.connector.Constans;

public class Select extends Request{
    int _space;
    int _index;
    int _offset;
    int _limit;
    byte[][][] _tuples;
    
    public Select(int space, int index, int offset, int limit, byte[][] ... tuples){
    	super();
    	if (space < 0 || space > 255){
    		_space = -1;
    		return;
    	}
    	_space = space;
    	_index = index;
    	_offset = offset;
    	_limit = limit;
    	_tuples = tuples;
    }
    
    @Override
    public byte[] toByte(){
    	
    	if (_space == -1){
    		return new byte[0];
    	}
    	
    	int length = Constans.SELECT_REQUEST_BODY + Constans.HEADER_LENGTH;
    	length += 4 * _tuples.length; //cardinality of all tuples
    	for (byte[][] i: _tuples) 
    		for (byte[] j: i)
    			length += ByteUtil.sizeOfInVarInt32(j.length) + j.length; // VarInt+Field
    	
    	byte[] body = new byte[length];
    	int offset = 0;
    	
    	//Make Header
    	offset = ByteUtil.writeInteger(body, offset, Constans.REQ_TYPE_SELECT);
    	offset += ByteUtil.writeInteger(body, offset, length - Constans.HEADER_LENGTH);
    	offset += ByteUtil.writeInteger(body, offset, _reqId);
    	
    	//Make Body
    	offset += ByteUtil.writeInteger(body, offset, _space);
    	offset += ByteUtil.writeInteger(body, offset, _index);
    	offset += ByteUtil.writeInteger(body, offset, _offset);
    	offset += ByteUtil.writeInteger(body, offset, _limit);
    	offset += ByteUtil.writeInteger(body, offset, _tuples.length);
    	for (byte[][] i: _tuples){
    		offset += ByteUtil.writeInteger(body, offset, i.length);
    		for (byte[] j: i){
        		offset += ByteUtil.encodeLengthInVar32Int(body, offset, j.length);
    			offset += ByteUtil.writeBytes(body, offset, j, 0, j.length);
    		}
    	}
    	return body;
    }
}