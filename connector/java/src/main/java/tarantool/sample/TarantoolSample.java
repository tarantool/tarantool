package tarantool.sample;

import java.net.UnknownHostException;
import java.util.Arrays;

import tarantool.connector.TarantoolConnector;
import tarantool.connector.TarantoolConnectorConfig;
import tarantool.connector.TarantoolConnectorImpl;
import tarantool.connector.exception.TarantoolConnectorException;
import tarantool.connector.socketpool.SocketPoolType;
import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;
import tarantool.connector.socketpool.worker.FactoryType;

import static tarantool.connector.socketpool.AbstractSocketPool.DISCONNECT_BOUND;
import static tarantool.connector.socketpool.AbstractSocketPool.INITIALIZE_SOCKET_POOL_TIMEOUT;
import static tarantool.connector.socketpool.AbstractSocketPool.RECONNECT_SOCKET_TIMEOUT;
import static tarantool.connector.socketpool.AbstractSocketPool.WAITING_SOCKET_POOL_TIMEOUT;

public class TarantoolSample {
	
	public static void main(String[] args) {

        int nameSpace = 0;
        long latencyPeriod = 60000L;
        String host = "127.0.0.1";
        int port = 33333;
        int socketReadTimeout = 5000;
        int minPoolSize = 1;
        int maxPoolSize = 1;

        byte[] buffer = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

		TarantoolConnectorConfig config = new TarantoolConnectorConfig(host, port, socketReadTimeout, minPoolSize,
                maxPoolSize, WAITING_SOCKET_POOL_TIMEOUT, RECONNECT_SOCKET_TIMEOUT, INITIALIZE_SOCKET_POOL_TIMEOUT,
                DISCONNECT_BOUND, FactoryType.PLAIN_SOCKET, SocketPoolType.STATIC_POOL, latencyPeriod, nameSpace);

        TarantoolConnector tarantoolConnector = null;
        try {
            tarantoolConnector = new TarantoolConnectorImpl(config);

            tarantoolConnector.insertData(1L, buffer);
            byte[] incomingBuffer = tarantoolConnector.getData(1L);

            if (Arrays.equals(incomingBuffer, buffer)) {
                System.out.println("Test is successful! Byte buffers are equals");
            } else {
                System.out.println("Attention! Byte buffers are not equals. Result: " + Arrays.toString(incomingBuffer));
            }
        } catch (UnknownHostException e) {
            e.printStackTrace();
        } catch (InterruptedException e) {
            e.printStackTrace();
        } catch (TarantoolConnectorException e) {
            e.printStackTrace();
        } catch (SocketPoolTimeOutException e) {
            e.printStackTrace();
        } finally {
            if (tarantoolConnector != null) {
                tarantoolConnector.close();
            }
        }
	}

}
