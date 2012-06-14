package tarantool.connector;

import tarantool.common.ByteUtil;
import tarantool.connector.Constans;

public class Delete extends Request{
    int _space;
    int _flags;
    byte[][] _tuple;
    
    public Delete(int space, int flags, byte[][] tuple){
    	super();
    	if (space < 0 || space > 255){
    		_space = -1;
    		return;
    	}
    	_space = space;
    	_flags = flags;
    	_tuple = tuple; 
    }
    
    @Override
	public byte[] toByte(){
    	
    	if (_space == -1){
    		return new byte[0];
    	}
    	
    	int length = Constans.DELETE_REQUEST_BODY + Constans.HEADER_LENGTH;
    	length += 4; // Cardinality
    	for (byte[] i: _tuple)
    		length += i.length + ByteUtil.sizeOfInVarInt32(i.length); // VarInt+Field
    	
    	byte[] body = new byte[length];
    	int offset = 0;
    	
    	//Make Header
    	offset = ByteUtil.writeInteger(body, offset, Constans.REQ_TYPE_DELETE);
    	offset += ByteUtil.writeInteger(body, offset, length - Constans.HEADER_LENGTH);
    	offset += ByteUtil.writeInteger(body, offset, _reqId);
    	
    	//Make Body
    	offset += ByteUtil.writeInteger(body, offset, _space);
    	offset += ByteUtil.writeInteger(body, offset, _flags);
    	offset += ByteUtil.writeInteger(body, offset, _tuple.length);
    	for (byte[] i: _tuple){
    		offset += ByteUtil.encodeLengthInVar32Int(body, offset, i.length);
    		offset += ByteUtil.writeBytes(body, offset, i, 0, i.length);
    	}
    	return body;
    }
    
}