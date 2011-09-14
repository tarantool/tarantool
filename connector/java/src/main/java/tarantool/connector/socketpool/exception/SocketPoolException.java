package tarantool.connector.socketpool.exception;


public class SocketPoolException extends Exception {

    public SocketPoolException(String message) {
        super(message);
    }

    public SocketPoolException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
