package tarantool.connector.socketpool.exception;


public class SocketPoolClosedException extends SocketPoolException {

    public SocketPoolClosedException(String message) {
        super(message);
    }

    public SocketPoolClosedException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
