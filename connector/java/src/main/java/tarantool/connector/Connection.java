package tarantool.connector;

import java.io.IOException;

import tarantool.common.ByteUtil;
import tarantool.connector.Ping;
import tarantool.connector.Response;
import tarantool.connector.Request;
import tarantool.connector.Configuration;
import tarantool.connector.Constans;

import tarantool.connector.exception.TarantoolConnectorException;
import tarantool.connector.socketpool.SocketPool;
import tarantool.connector.socketpool.SocketPoolFactory;
import tarantool.connector.socketpool.SocketPoolType;
import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;
import tarantool.connector.socketpool.worker.FactoryType;
import tarantool.connector.socketpool.worker.SocketWorker;

import static tarantool.connector.socketpool.AbstractSocketPool.DISCONNECT_BOUND;
import static tarantool.connector.socketpool.AbstractSocketPool.INITIALIZE_SOCKET_POOL_TIMEOUT;
import static tarantool.connector.socketpool.AbstractSocketPool.RECONNECT_SOCKET_TIMEOUT;
import static tarantool.connector.socketpool.AbstractSocketPool.WAITING_SOCKET_POOL_TIMEOUT;

public class Connection{
    private SocketPool pool;

    long latencyPeriod = 60000L;
    int socketReadTimeout = 5000;
    int minPoolSize = 1;
    int maxPoolSize = 1;
    
    public Connection(){
    }
    
    public Response connect(String host, int port) throws TarantoolConnectorException, InterruptedException, IOException{
        Configuration config = new Configuration(host, port, socketReadTimeout, minPoolSize,
            maxPoolSize, WAITING_SOCKET_POOL_TIMEOUT, RECONNECT_SOCKET_TIMEOUT, INITIALIZE_SOCKET_POOL_TIMEOUT,
            DISCONNECT_BOUND, FactoryType.PLAIN_SOCKET, SocketPoolType.STATIC_POOL, latencyPeriod);
        return connect(config);
    }

    public Response connect(Configuration config) throws TarantoolConnectorException, InterruptedException, IOException{
        try {
			this.pool = SocketPoolFactory.createSocketPool(config.getSocketPoolConfig());
		} catch (SocketPoolTimeOutException e) {
            throw new TarantoolConnectorException("Socket pool unavailable", e);
		}
		return execute(new Ping());
    }

    public Response execute(Request req) throws TarantoolConnectorException, InterruptedException, IOException{
        SocketWorker worker = getSocketWorker();
        worker.writeData(req.toByte());
        
        int length;
        byte[] header = new byte[Constans.HEADER_LENGTH];
        
        worker.readData(header, Constans.HEADER_LENGTH);
        
        length = ByteUtil.readInteger(header, 4);
        byte[] body = new byte[length];
        worker.readData(body, ByteUtil.readInteger(header, 4));
        
        Response resp = new Response(header, body);
        try{
	        if (resp.get_reqId() != req.getReqId())
	        	throw new TarantoolConnectorException("Wrong Packet ID");
        }
        finally{
        	worker.release();
        }
        
        return resp;
    }
    
    private SocketWorker getSocketWorker() throws InterruptedException, TarantoolConnectorException{
        try {
            return pool.borrowSocketWorker();
        } catch (SocketPoolTimeOutException e) {
            throw new TarantoolConnectorException("There are no free sockets", e);
        } catch (SocketPoolException e) {
            throw new TarantoolConnectorException("Socket pool unavailable", e);
        }
    }
    
    public void disconnect(){
    	pool.close();
    }
}

