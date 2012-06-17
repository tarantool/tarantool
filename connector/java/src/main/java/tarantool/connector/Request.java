package tarantool.connector;

import java.util.concurrent.atomic.AtomicInteger;

public abstract class Request{
	static AtomicInteger reqId = new AtomicInteger();
    public abstract byte[] toByte();

    int _reqId;
    public Request() {
    	_reqId = reqId.getAndIncrement();
	}

	public int getReqId() {
		return _reqId;
	}
}