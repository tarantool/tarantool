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
/*public class Update implements Request{
    int _space;
    int _flags;
    byte[][] _tuple;
    int _count;
    Operation[] _operation_;

    Update(int space, int flags, byte[][] tuple, int count, Operation[] _operation_);
}*/