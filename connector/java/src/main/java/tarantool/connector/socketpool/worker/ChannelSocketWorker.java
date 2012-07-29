package tarantool.connector.socketpool.worker;

import java.io.EOFException;
import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.channels.SocketChannel;

import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.Log;

import tarantool.connector.socketpool.AbstractSocketPool;

class ChannelSocketWorker extends SocketWorkerInternal {

    private static final Log LOG = LogFactory.getLog(ChannelSocketWorker.class);

    private SocketChannel socketChannel;

    ChannelSocketWorker(InetAddress address, int port, int soTimeout,
            AbstractSocketPool pool) throws IOException {
        super(pool, address, port, soTimeout);
        connect();
    }

    @Override
    public void close() {
        try {
            socketChannel.close();
        } catch (final IOException e) {
            LOG.error("Can't close socket channel which associated with the socket");
        }

        disconnected();
    }

    @Override
    public void connect() throws IOException {
        socketChannel = SocketChannel
                .open(new InetSocketAddress(address, port));
        final Socket socket = socketChannel.socket();

        socket.setKeepAlive(true);
        socket.setTcpNoDelay(true);
        socket.setSoTimeout(soTimeout); // set but NIO not supported socket
                                        // timeout

        connected();
    }

    @Override
    public int readData(byte[] buffer, int length) throws IOException {
        try {
            final ByteBuffer byteBuffer = ByteBuffer.wrap(buffer);
            while (byteBuffer.hasRemaining()) {
                if (socketChannel.read(byteBuffer) == -1) {
                    throw new EOFException("Unexpected end of stream");
                }
            }
            return byteBuffer.position();
        } catch (final IOException e) {
            LOG.error("Error occurred in read channel operation", e);
            disconnected();
            throw e;
        }
    }

    @Override
    public void writeData(byte[] buffer) throws IOException {
        try {
            final ByteBuffer byteBuffer = ByteBuffer.wrap(buffer);
            while (byteBuffer.hasRemaining()) {
                socketChannel.write(byteBuffer);
            }
        } catch (final IOException e) {
            LOG.error("Error occurred in write channel operation", e);
            disconnected();
            throw e;
        }
    }
}
