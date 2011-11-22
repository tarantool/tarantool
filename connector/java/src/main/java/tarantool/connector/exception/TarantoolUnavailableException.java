package tarantool.connector.exception;


public class TarantoolUnavailableException extends TarantoolConnectorException {

    public TarantoolUnavailableException(String message) {
        super(message);
    }

    public TarantoolUnavailableException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
