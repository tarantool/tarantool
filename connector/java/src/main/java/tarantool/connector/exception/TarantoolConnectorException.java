package tarantool.connector.exception;


public class TarantoolConnectorException extends Exception {
	private static final long serialVersionUID = -7113630407008652767L;

	public TarantoolConnectorException(String message) {
        super(message);
    }
    
    public TarantoolConnectorException(String message, Throwable throwable) {
        super(message, throwable);
    }
}
