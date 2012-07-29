package tarantool.connector;

import static tarantool.connector.socketpool.AbstractSocketPool.DISCONNECT_BOUND;
import static tarantool.connector.socketpool.AbstractSocketPool.INITIALIZE_SOCKET_POOL_TIMEOUT;
import static tarantool.connector.socketpool.AbstractSocketPool.RECONNECT_SOCKET_TIMEOUT;
import static tarantool.connector.socketpool.AbstractSocketPool.WAITING_SOCKET_POOL_TIMEOUT;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Iterator;

import tarantool.common.ByteUtil;
import tarantool.connector.exception.TarantoolConnectorException;
import tarantool.connector.request.Ping;
import tarantool.connector.socketpool.SocketPool;
import tarantool.connector.socketpool.SocketPoolFactory;
import tarantool.connector.socketpool.SocketPoolType;
import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;
import tarantool.connector.socketpool.worker.FactoryType;
import tarantool.connector.socketpool.worker.SocketWorker;

public class ConnectionImpl implements Connection{
	long latencyPeriod = 60000L;

	int maxPoolSize = 1;
	int minPoolSize = 1;
	private SocketPool pool;
	int socketReadTimeout = 5000;

	public ConnectionImpl() {
	}

	/**
	 * internal use function to concatenate two arrays
	 * 
	 * @param f
	 *            - first array
	 * @param s
	 *            - second array
	 * @return concatenated array
	 */
	private byte[] addAll(byte[] f, byte[] s) {
		if (f == null && s == null)
			return new byte[0];
		if (f == null && s != null)
			return Arrays.copyOf(s, s.length);
		if (s == null && f != null)
			return Arrays.copyOf(f, f.length);
		final byte[] p = new byte[f.length + s.length];
		System.arraycopy(f, 0, p, 0, f.length);
		System.arraycopy(s, 0, p, f.length, s.length);
		return p;
	}

	public Response connect(Configuration config)
			throws TarantoolConnectorException, InterruptedException,
			IOException {
		try {
			this.pool = SocketPoolFactory.createSocketPool(config
					.getSocketPoolConfig());
		} catch (SocketPoolTimeOutException e) {
			throw new TarantoolConnectorException("Socket pool unavailable", e);
		}
		return execute(new Ping());
	}

	public Response connect(String host, int port)
			throws TarantoolConnectorException, InterruptedException,
			IOException {
		final Configuration config = new Configuration(host, port,
				socketReadTimeout, minPoolSize, maxPoolSize,
				WAITING_SOCKET_POOL_TIMEOUT, RECONNECT_SOCKET_TIMEOUT,
				INITIALIZE_SOCKET_POOL_TIMEOUT, DISCONNECT_BOUND,
				FactoryType.PLAIN_SOCKET, SocketPoolType.STATIC_POOL,
				latencyPeriod);
		return connect(config);
	}

	public void disconnect() {
		pool.close();
	}

	/**
	 * This is not safe to work function. Slow and Angry. Use single request
	 * execute, please.
	 */
	public ArrayList<Response> execute(Collection<Request> req)
			throws TarantoolConnectorException, InterruptedException,
			IOException {
		final SocketWorker worker = getSocketWorker();
		try {
			byte[] request = null;
			for (final Request _req : req)
				request = addAll(request, _req.toByte());
			send(worker, request);
			final ArrayList<byte[]> _resp = recv(worker, req.size());
			final ArrayList<Response> resp = new ArrayList<Response>(req.size());
			final Iterator<Request> f = req.iterator();
			for (int i = 0; i < _resp.size() / 2; ++i) {
				Response __resp = new Response(_resp.get(2 * i),
						_resp.get(2 * i + 1));
				if (__resp.get_reqId() != f.next().getReqId()) {
					throw new TarantoolConnectorException("Error in "
							+ "queue of Requests.");
				}
				resp.set(i, __resp);
			}
			return resp;
		} finally {
			worker.release();
		}
	}

	public Response execute(Request req) throws TarantoolConnectorException,
			InterruptedException, IOException {
		final SocketWorker worker = getSocketWorker();
		try {
			send(worker, req);
			final ArrayList<byte[]> _resp = recv(worker, 1);

			final Response resp = new Response(_resp.get(0), _resp.get(1));
			if (resp.get_reqId() != req.getReqId()) {
				throw new TarantoolConnectorException("Wrong Packet ID");
			}
			return resp;
		} finally {
			worker.release();
		}
	}

	private SocketWorker getSocketWorker() throws InterruptedException,
			TarantoolConnectorException {
		try {
			return pool.borrowSocketWorker();
		} catch (SocketPoolTimeOutException e) {
			throw new TarantoolConnectorException("There are no free sockets",
					e);
		} catch (SocketPoolException e) {
			throw new TarantoolConnectorException("Socket pool unavailable", e);
		}
	}

	/**
	 * @param worker
	 *            - instance of SocketWorker class to connect with tnt
	 * @param n
	 *            - number of sent packages
	 * @return pairs of head/body
	 */
	ArrayList<byte[]> recv(SocketWorker worker, int n) throws IOException {
		int length;
		final ArrayList<byte[]> answer = new ArrayList<byte[]>();
		for (int i = 0; i < n; ++i) {
			final byte[] header = new byte[Constants.HEADER_LENGTH];
			answer.add(header);
			worker.readData(header, Constants.HEADER_LENGTH);
			length = ByteUtil.readInteger(header, 4);
			final byte[] body = new byte[length];
			worker.readData(body, ByteUtil.readInteger(header, 4));
			answer.add(body);
		}
		return answer;
	}

	void send(SocketWorker worker, byte[] req) throws IOException {
		worker.writeData(req);
	}

	void send(SocketWorker worker, Request req) throws IOException {
		worker.writeData(req.toByte());
	}
}