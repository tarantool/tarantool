package tarantool.connector.exception;


public class TarantoolConnectorException extends Exception {

    public TarantoolConnectorException(String message) {
        super(message);
    }
    
    public TarantoolConnectorException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
