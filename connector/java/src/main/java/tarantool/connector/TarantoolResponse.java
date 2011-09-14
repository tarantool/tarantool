package tarantool.connector;


public class TarantoolResponse {
    private final int dataLength;
    private final int requestId;

    public TarantoolResponse(int dataLength, int requestId) {
        this.dataLength = dataLength;
        this.requestId = requestId;
    }

    public int getDataLength() {
        return dataLength;
    }

    public int getRequestId() {
        return requestId;
    }
}
