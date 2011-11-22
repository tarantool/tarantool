package tarantool.connector.socketpool.worker;

import java.io.IOException;


public interface SocketWorker {
    public void writeData(byte[] buffer) throws IOException;
    public int readData(byte[] buffer, int length) throws IOException;
    public void release();
}
