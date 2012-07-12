package tarantool.connector.socketpool.exception;

public class SocketPoolClosedException extends SocketPoolException {

    /**
     * 
     */
    private static final long serialVersionUID = 8900867895783257561L;

    public SocketPoolClosedException(String message) {
        super(message);
    }

    public SocketPoolClosedException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
