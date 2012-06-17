package tarantool.connector;

import tarantool.connector.Constans;
import tarantool.common.ByteUtil;

//TODO: Добавить 8 классов для каждой операции.
public class Operation {
	int	_field_no;
	int _op_code;
	byte[] _offset;
	byte[] _length;
	byte[] _field;
	
	public Operation(int field_no, byte[] offset, byte[] length, byte[] field){
		_field_no = field_no;
		_op_code = Constans.OP_SPLICE;
		
		_offset = offset;
		_length = length;
		_field = field;
		
	}
	
	public Operation(int field_no, int op_code, byte[] field){
		_field_no = field_no;
		if (op_code == 5){
			_op_code = -1;
			return;
		}
		_op_code = op_code;
		
		_offset = new byte[0];
		_length = new byte[0];
		_field = field;
	}
	
	public byte[] toByte(){
		
		if (_op_code == -1)
			return new byte[0];
		
		int length = 2 * 4;
		length += _offset.length + _length.length + _field.length;
		length += ( _offset.length == 0 ? 0 : ByteUtil.sizeOfInVarInt32(_offset.length));
		length += ( _length.length == 0 ? 0 : ByteUtil.sizeOfInVarInt32(_length.length));
		length += ByteUtil.sizeOfInVarInt32(_field.length);
		
		byte[] body = new byte[length];
		
		int offset = 0;
		offset += ByteUtil.writeInteger(body, offset, _field_no);
		body[offset] = (byte ) _op_code; offset++;
		if ( _offset.length != 0 ){
			offset += ByteUtil.encodeLengthInVar32Int(body, offset, _offset.length);
			offset += ByteUtil.writeBytes(body, offset, _offset, 0, _offset.length);
		}
		if ( _length.length != 0 ){
			offset += ByteUtil.encodeLengthInVar32Int(body, offset, _length.length);
			offset += ByteUtil.writeBytes(body, offset, _length, 0, _length.length);
		}
		offset += ByteUtil.encodeLengthInVar32Int(body, offset, _field.length);
		offset += ByteUtil.writeBytes(body, offset, _field, 0, _field.length);
		
		return body;
	}
	
}
