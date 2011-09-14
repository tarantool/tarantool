package tarantool.connector.socketpool.worker;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import tarantool.connector.socketpool.AbstractSocketPool;


class PlainSocketWorker extends SocketWorkerInternal {

    private static final Log LOG = LogFactory.getLog(PlainSocketWorker.class);

    private Socket socket;
    
    private OutputStream outputStream;
    private InputStream inputStream;

    PlainSocketWorker(InetAddress address, int port, int soTimeout, AbstractSocketPool pool)
            throws IOException {
        super(pool, address, port, soTimeout);
        connect();
    }

    @Override
    public void connect() throws IOException {
        socket = new Socket(address, port);

        socket.setKeepAlive(true);
        socket.setTcpNoDelay(true);
        socket.setSoTimeout(soTimeout);

        outputStream = socket.getOutputStream();
        inputStream = socket.getInputStream();

        connected();
    }

    @Override
    public void close() {
        try {
            socket.shutdownInput();
        } catch (IOException e) {
            LOG.error("Can't shutdown input which associated with the socket");
        }
        try {
            socket.shutdownOutput();
        } catch (IOException e) {
            LOG.error("Can't shutdown output which associated with the socket");
        }
        try {
            socket.close();
        } catch (IOException e) {
            LOG.error("Can't close socket");
        }

        disconnected();
    }

    @Override
    public void writeData(byte[] buffer) throws IOException {
        try {
            outputStream.write(buffer);
            outputStream.flush();
        } catch (IOException e) {
            LOG.error("Error occurred in write socket operation", e);
            disconnected();
            throw e;
        }
    }

    @Override
    public int readData(byte[] buffer, int length) throws IOException {
        try {
            int loadedBytes = 0, currentReadBytes = 0;
            while((loadedBytes += currentReadBytes) != length) {
                currentReadBytes = inputStream.read(buffer, loadedBytes, length - loadedBytes);
                if (currentReadBytes == -1) {
                    throw new EOFException("Unexpected end of stream");
                }
            }
            return loadedBytes;
        } catch (IOException e) {
            LOG.error("Error occurred in read socket operation", e);
            disconnected();
            throw e;
        } 
    }
}
