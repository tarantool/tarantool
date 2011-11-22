package tarantool.connector.socketpool.exception;


public class SocketPoolTimeOutException extends SocketPoolException {

    public SocketPoolTimeOutException(String message) {
        super(message);
    }

    public SocketPoolTimeOutException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
