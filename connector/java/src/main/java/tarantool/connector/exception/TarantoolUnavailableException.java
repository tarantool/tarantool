package tarantool.connector.exception;

public class TarantoolUnavailableException extends TarantoolConnectorException {
    private static final long serialVersionUID = -6817165807402090741L;

    public TarantoolUnavailableException(String message) {
        super(message);
    }

    public TarantoolUnavailableException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
