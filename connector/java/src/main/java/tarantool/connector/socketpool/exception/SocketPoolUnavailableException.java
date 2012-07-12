package tarantool.connector.socketpool.exception;

public class SocketPoolUnavailableException extends SocketPoolException {

    /**
     * 
     */
    private static final long serialVersionUID = 4803698963438672278L;

    public SocketPoolUnavailableException(String message) {
        super(message);
    }

    public SocketPoolUnavailableException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
