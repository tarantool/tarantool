package tarantool.connector;

import tarantool.common.ByteUtil;
import tarantool.connector.Constans;

public class Ping extends Request {
	
	public Ping() {
		//super();
		//Request.Request();
		super();
	}

	@Override
	public byte[] toByte() {
    	byte[] body = new byte[Constans.HEADER_LENGTH];
    	int offset = 0;
    	
    	//Make Header
    	offset = ByteUtil.writeInteger(body, offset, Constans.REQ_TYPE_PING);
    	offset += ByteUtil.writeInteger(body, offset, 0);
    	offset += ByteUtil.writeInteger(body, offset, _reqId);

		return body;
	}

}
