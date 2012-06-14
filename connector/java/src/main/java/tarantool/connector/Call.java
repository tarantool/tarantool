package tarantool.connector;

import tarantool.common.ByteUtil;
import tarantool.connector.Constans;

public class Call extends Request{
    int _flags;
    byte[] _proc;
    byte[][] _tuple;

    public Call(int flags, String proc, byte[][] tuple){
    	super();
    	_flags = flags;
    	_proc = proc.getBytes();
    	_tuple = tuple; 
    }
    
    public Call(int flags, byte[] proc, byte[][] tuple){
    	super();
    	_flags = flags;
    	_proc = proc;
    	_tuple = tuple; 
    }
    
    @Override
    public byte[] toByte(){
    
    	int length = Constans.CALL_REQUEST_BODY + Constans.HEADER_LENGTH;
    	length += _proc.length + ByteUtil.sizeOfInVarInt32(_proc.length); // VarInt+Field
    	length += 4; // Cardinality
    	for (byte[] i: _tuple)
    		length += i.length + ByteUtil.sizeOfInVarInt32(i.length); // VarInt+Field
    	
    	byte[] body = new byte[length];
    	int offset = 0;
    	
    	//Make Header
    	offset = ByteUtil.writeInteger(body, offset, Constans.REQ_TYPE_CALL);
    	offset += ByteUtil.writeInteger(body, offset, length - Constans.HEADER_LENGTH);
    	offset += ByteUtil.writeInteger(body, offset, _reqId);
    	
    	//Make Body
    	offset += ByteUtil.writeInteger(body, offset, _flags);
		offset += ByteUtil.encodeLengthInVar32Int(body, offset, _proc.length);
    	offset += ByteUtil.writeBytes(body, offset, _proc, 0, _proc.length);
    	offset += ByteUtil.writeInteger(body, offset, _tuple.length);
    	for (byte[] i: _tuple){
    		offset += ByteUtil.encodeLengthInVar32Int(body, offset, i.length);
    		offset += ByteUtil.writeBytes(body, offset, i, 0, i.length);
    	}
    	return body;
    }
}
