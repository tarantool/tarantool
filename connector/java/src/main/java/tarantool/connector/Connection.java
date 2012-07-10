/*
* Copyright (C) 2012 Mail.RU
* Copyright (C) 2012 Eugine Blikh
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*/

package tarantool.connector;

import java.io.IOException;
import java.util.Arrays;
import java.util.Collection;
import java.util.Iterator;
import java.util.Vector;

import tarantool.common.ByteUtil;
import tarantool.connector.request.Ping;
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

public class Connection {
    private SocketPool pool;

    long latencyPeriod = 60000L;
    int socketReadTimeout = 5000;
    int minPoolSize = 1;
    int maxPoolSize = 1;
    
    public Connection() {
    }
    
    public Response connect(String host, int port) throws 
    TarantoolConnectorException, InterruptedException, IOException {
        Configuration config = new Configuration(host, port, socketReadTimeout,
        		minPoolSize, maxPoolSize, WAITING_SOCKET_POOL_TIMEOUT, 
        		RECONNECT_SOCKET_TIMEOUT, INITIALIZE_SOCKET_POOL_TIMEOUT, 
        		DISCONNECT_BOUND, FactoryType.PLAIN_SOCKET, 
        		SocketPoolType.STATIC_POOL, latencyPeriod);
        return connect(config);
    }
    
    public Response connect(Configuration config) throws 
    TarantoolConnectorException, InterruptedException, IOException {
        try {
			this.pool = SocketPoolFactory.createSocketPool(
					config.getSocketPoolConfig());
		} catch (SocketPoolTimeOutException e) {
            throw new TarantoolConnectorException(
            		"Socket pool unavailable", e);
		}
		return execute(new Ping());
    }

    public Response execute(Request req) throws TarantoolConnectorException, 
    InterruptedException, IOException {
        SocketWorker worker = getSocketWorker();
        
        send(worker, req);
        Vector<byte[]> _resp = recv(worker, 1);
        
        Response resp = new Response(_resp.elementAt(0), _resp.elementAt(1));
        try{
	        if (resp.get_reqId() != req.getReqId())
	        	throw new TarantoolConnectorException("Wrong Packet ID");
        }
        finally{
        	worker.release();
        }
        return resp;
    }
    /**
    * This is not safe to work function. Slow and Angry.
    * Use single request execute, please.
    */
    public Vector<Response> execute(Collection<Request> req) throws 
    TarantoolConnectorException, InterruptedException, IOException {
        SocketWorker worker = getSocketWorker();
        byte[] request = null;
        for (Request _req : req)
        	request = addAll(request, _req.toByte());
        send(worker, request);
        Vector<byte[]> _resp = recv(worker, req.size());
        Vector<Response> resp = new Vector<Response>();
        try{
        	Iterator<Request> f = req.iterator();
    	    for (int i = 0; i < _resp.size()/2; ++i){
	        	resp.add(new Response(_resp.elementAt(2*i), 
	        			_resp.elementAt(2*i+1)));
	        	if (resp.lastElement().get_reqId() != f.next().getReqId())
	        		throw new TarantoolConnectorException("Error in " +
	        				"queue of Requests.");
	        }
        }
        finally{
        	worker.release();
        }
        return resp;
    }
    
    private SocketWorker getSocketWorker() throws InterruptedException, 
    TarantoolConnectorException {
        try {
            return pool.borrowSocketWorker();
        } catch (SocketPoolTimeOutException e) {
            throw new TarantoolConnectorException(
            		"There are no free sockets", e);
        } catch (SocketPoolException e) {
            throw new TarantoolConnectorException(
            		"Socket pool unavailable", e);
        }
    }
    
    void send(SocketWorker worker, Request req) throws IOException{
    	worker.writeData(req.toByte());
    }
    
    void send(SocketWorker worker, byte[] req) throws IOException{
    	worker.writeData(req);
    }
    
    /**	
     * @param worker - instance of SocketWorker class to connect with tnt
     * @param n - number of sent packages
     * @return pairs of head/body 
     */ 
    Vector<byte[]> recv(SocketWorker worker, int n) throws IOException{int length;
        Vector <byte[]> answer = new Vector<byte[]>();
    	for (int i = 0; i < n; ++i){
        	byte[] header = new byte[Constans.HEADER_LENGTH];
        	answer.add(header);
        	worker.readData(header, Constans.HEADER_LENGTH);
        	length = ByteUtil.readInteger(header, 4);
        	byte[] body = new byte[length];
        	worker.readData(body, ByteUtil.readInteger(header, 4));
        	answer.add(body);
    	}
        return answer;
    }
    
    public void disconnect() {
    	pool.close();
    }  
    /**
     * internal use function to concatenate two arrays
     * @param f - first array
     * @param s - second array
     * @return concatenated array
     */
    byte[] addAll(byte[] f, byte[] s){
    	if (f == null && s == null)
    		return new byte[0];
    	if (f == null && s != null)
    		return Arrays.copyOf(s, s.length);
    	if (s == null && f != null)
    		return Arrays.copyOf(f, f.length);
    	byte[] p = new byte[f.length + s.length];
    	System.arraycopy(f, 0, p, 0, f.length);
    	System.arraycopy(s, 0, p, f.length, s.length);
    	return p;
    }
}

