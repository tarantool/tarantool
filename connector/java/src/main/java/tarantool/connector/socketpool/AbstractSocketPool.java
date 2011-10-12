package tarantool.connector.socketpool;

import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.worker.FactoryType;
import tarantool.connector.socketpool.worker.SocketFactory;
import tarantool.connector.socketpool.worker.SocketWorker;
import tarantool.connector.socketpool.worker.SocketWorkerInternal;


public abstract class AbstractSocketPool implements SocketPool {

    private static final Log LOG = LogFactory.getLog(AbstractSocketPool.class);

    public static final long WAITING_SOCKET_POOL_TIMEOUT = 1000L; // 1 sec
    public static final long RECONNECT_SOCKET_TIMEOUT = 1000L; // 1 sec
    public static final long INITIALIZE_SOCKET_POOL_TIMEOUT = 20000L; // 20 sec
    public static final int DISCONNECT_BOUND = 10;

    final long waitingTimeout;
    final long reconnectTimeout;
    final long initializeTimeout;
    final int disconnectBound;

    final SocketFactory socketWorkerFactory;

    private final AtomicInteger disconnectCount = new AtomicInteger(0);

    private final ExecutorService reconnectExecutor;
    private final BlockingQueue<SocketWorkerInternal> reconnectQueue = new LinkedBlockingQueue<SocketWorkerInternal>();

    volatile SocketPoolStateMachine stateMachine = new SocketPoolStateMachine();

    public AbstractSocketPool(String host, int port, int socketReadTimeout, long waitingTimeout,
            long reconnectTimeout, long initializeTimeout, int disconnectBound, FactoryType type) throws UnknownHostException {

        if (host == null || "".equals(host.trim())) {
            throw new IllegalArgumentException("Incorrect host:" + host);
        }

        if (port < 0 || port > 0xFFFF) {
            throw new IllegalArgumentException("Port out of range:" + port);
        }

        if (waitingTimeout < 0L) {
            throw new IllegalArgumentException("Incorrect value of waiting timeout");
        }

        if (reconnectTimeout < 0L) {
            throw new IllegalArgumentException("Incorrect value of reconnect timeout");
        }

        if (disconnectBound < 0) {
            throw new IllegalArgumentException("Incorrect value of disconnect bound");
        }
        
        if (socketReadTimeout < 0) {
            throw new IllegalArgumentException("Incorrect value of socket read timeout");
        }

        if (initializeTimeout < 0L) {
            throw new IllegalArgumentException("Incorrect value of initialize timeout");
        }

        this.waitingTimeout = waitingTimeout;
        this.reconnectTimeout = reconnectTimeout;
        this.disconnectBound = disconnectBound;
        this.initializeTimeout = initializeTimeout;

        socketWorkerFactory = type.createFactory(InetAddress.getByName(host), port, socketReadTimeout, this);

        reconnectExecutor = Executors.newSingleThreadExecutor(new ThreadFactory() {
            @Override
            public Thread newThread(Runnable r) {
                Thread thread = new Thread(r, "ReconnectionThread");
                thread.setDaemon(true);
                thread.setPriority(Thread.MIN_PRIORITY);
                return thread;
            }
        });
        reconnectExecutor.execute(new Runnable() {
            @Override
            public void run() {
                Thread thread = Thread.currentThread();
                while (!thread.isInterrupted()) {
                    SocketWorkerInternal worker = null;
                    try {
                        worker = reconnectQueue.take();
                        worker.connect();
                        if (stateMachine.isReconnecting() && reconnectQueue.isEmpty()) {
                            Thread.sleep(AbstractSocketPool.this.reconnectTimeout);
                            if (reconnectQueue.isEmpty()) { // wait reconnectTimeout for check unstable net
                                stateMachine.connect();
                            }
                        }
                        LOG.info("Reconnect completed successfully");
                        internalReturnSocketWorker(worker);
                    } catch (InterruptedException e) {
                        LOG.info("Reconnecting thread is stopped");
                        thread.interrupt(); // thread was stopped, propagate interruption
                    } catch (IOException e) {
                        LOG.info("Reconnect completed failed");
                        try {
                            reconnectQueue.put(worker);
                            Thread.sleep(AbstractSocketPool.this.reconnectTimeout);
                        } catch (InterruptedException e1) {
                            LOG.info("Reconnecting thread is stopped");
                            thread.interrupt(); // thread was stopped, propagate interruption
                        }
                    }
                }
            }
        });
    }

    public void close() {
        stateMachine.close();
        reconnectExecutor.shutdownNow();
        internalClose();
    }

    public void returnSocketWorker(SocketWorkerInternal socketWorker) {
        if (socketWorker.isConnected()) {
            internalReturnSocketWorker(socketWorker);
        } else {
            pushToReconnect(socketWorker);
            if (stateMachine.isRunning() && disconnectCount.incrementAndGet() >= disconnectBound) {
                stateMachine.disconnect();
                disconnectCount.getAndSet(0);
                feedReconnect();
            }
        }
    }

    void pushToReconnect(SocketWorkerInternal socketWorker) {
        socketWorker.close();
        boolean added = reconnectQueue.offer(socketWorker);
        assert added: "Queue can't add wrapper, too many socket worker for queue size";
    }

    public abstract SocketWorker borrowSocketWorker() throws InterruptedException, SocketPoolException;
    abstract void internalReturnSocketWorker(SocketWorkerInternal socketWorker);
    abstract void feedReconnect();
    abstract void internalClose();
}