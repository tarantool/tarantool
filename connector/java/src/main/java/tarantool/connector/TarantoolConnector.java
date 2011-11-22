package tarantool.connector;

import tarantool.connector.exception.TarantoolConnectorException;


public interface TarantoolConnector {
    void insertData(long id, byte[] data) throws InterruptedException, TarantoolConnectorException;

    void insertData(byte[] id, byte[] data) throws InterruptedException, TarantoolConnectorException;

    byte[] getData(long id) throws InterruptedException, TarantoolConnectorException;

    byte[] getData(byte[] id) throws InterruptedException, TarantoolConnectorException;

    boolean deleteById(long id) throws InterruptedException, TarantoolConnectorException;

    boolean deleteById(byte[] id) throws InterruptedException, TarantoolConnectorException;

    /**
     * Attention! This method can use only with tarantool from trunk feature-get-all-keys.
     * This method locks all activity in tarantool.
     * @return array of byte array with keys
     * @throws TarantoolConnectorException
     * @throws InterruptedException
     */
    @Deprecated
    byte[][] getKeySetAsByteArray() throws TarantoolConnectorException, InterruptedException;

    /**
     * Attention! This method can use only with tarantool from trunk feature-get-all-keys.
     * This method locks all activity in tarantool.
     * @return long array with keys
     * @throws TarantoolConnectorException
     * @throws InterruptedException
     */
    @Deprecated
    long[] getKeySetAsLongArray() throws TarantoolConnectorException, InterruptedException;

    /**
     * Don't lock activity but need LUA script support and server side command
     * @return
     * @throws TarantoolConnectorException
     * @throws InterruptedException
     */
    long[] getScriptKeySetAsLongArray(int batchSize) throws TarantoolConnectorException, InterruptedException;

    /**
     * Don't lock activity but need LUA script support and server side command
     * @return
     * @throws TarantoolConnectorException
     * @throws InterruptedException
     */
    byte[][] getScriptKeySetAsByteArray(int batchSize) throws TarantoolConnectorException, InterruptedException;

    void truncate() throws InterruptedException, TarantoolConnectorException;

    void close();
}
