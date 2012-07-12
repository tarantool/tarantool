package tarantool.connector.socketpool.worker;

import java.io.IOException;

public interface SocketWorker {
    public int readData(byte[] buffer, int length) throws IOException;

    public void release();

    public void writeData(byte[] buffer) throws IOException;
}
