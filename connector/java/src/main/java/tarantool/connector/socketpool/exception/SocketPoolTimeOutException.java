package tarantool.connector.socketpool.exception;

public class SocketPoolTimeOutException extends SocketPoolException {

    /**
     * 
     */
    private static final long serialVersionUID = 297816494033303314L;

    public SocketPoolTimeOutException(String message) {
        super(message);
    }

    public SocketPoolTimeOutException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
