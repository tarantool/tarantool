package tarantool.connector.socketpool.exception;


public class SocketPoolUnavailableException extends SocketPoolException {

    public SocketPoolUnavailableException(String message) {
        super(message);
    }

    public SocketPoolUnavailableException(String message, Throwable throwable) {
        super(message, throwable);    
    }
}
