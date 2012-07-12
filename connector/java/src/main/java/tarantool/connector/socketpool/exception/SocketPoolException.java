package tarantool.connector.socketpool.exception;

public class SocketPoolException extends Exception {

    /**
     * 
     */
    private static final long serialVersionUID = 1261161669528605005L;

    public SocketPoolException(String message) {
        super(message);
    }

    public SocketPoolException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
