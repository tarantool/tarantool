package tarantool.connector;

import tarantool.connector.Constans;
import tarantool.common.ByteUtil;


public class Update extends Request{
    int _space;
    int _flags;
    byte[][] _tuple;
    int _count;
    
    Operation[] _operation;
    byte[][] _ops;
    
    Update(int space, int flags, byte[][] tuple, Operation ... ops){
    	super();
    	if (space < 0 || space > 255){
    		_space = -1;
    		return;
    	}
    	_flags = flags;
    	_tuple = tuple;
    	_count = ops.length;
    	_operation = ops;

    	_ops = new byte[_operation.length][];
    	for (int i = 0; i < _operation.length; ++i){
    		_ops[i] = _operation[i].toByte();
    		if (_ops[i].length == 0)
    			_count--;
    	}
    }

	@Override
	public byte[] toByte() {

    	if (_space == -1){
    		return new byte[0];
    	}
    	
    	
    	int length = Constans.UPDATE_REQUEST_BODY + Constans.HEADER_LENGTH;
    	length += 4; // Cardinality
    	for (byte[] i: _tuple)
    		length += i.length + ByteUtil.sizeOfInVarInt32(i.length); // VarInt+Field
    	
    	byte[] body = new byte[length];
    	int offset = 0;
    	
    	//Make Header
    	offset = ByteUtil.writeInteger(body, offset, Constans.REQ_TYPE_INSERT);
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
    	offset += ByteUtil.writeInteger(body, offset, _count);
    	for (byte[] op: _ops)
    		offset += ByteUtil.writeBytes(body, offset, op, 0, op.length);
    	return body;
		
	}
}