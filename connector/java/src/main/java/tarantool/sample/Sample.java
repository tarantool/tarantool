package tarantool.sample;

import java.io.IOException;
import java.util.Arrays;

import org.testng.Assert;

import tarantool.common.ByteUtil;
import tarantool.common.Tuple;
import tarantool.connector.exception.TarantoolConnectorException;
import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.Call;
import tarantool.connector.Connection;
import tarantool.connector.Delete;
import tarantool.connector.Insert;
import tarantool.connector.Ping;
import tarantool.connector.Request;
import tarantool.connector.Response;
import tarantool.connector.Select;

import tarantool.common.Tuple;



public class Sample{
	public static final int MAX_LIMIT = 0xFFFFFFFF;
	
	public static void main(String[] args) throws TarantoolConnectorException, SocketPoolException, InterruptedException, IOException {
		
		Connection conn = new Connection();
		conn.connect("127.0.0.1", 33013);
		byte[][] Req = new byte[200][];
		Response Resp[] = new Response[200];
		
		Req[0] = "Richard".getBytes();
		Req[1] = "Selma".getBytes();
		Req[2] = "Oprah".getBytes();
		Req[3] = "pom".getBytes();
		Req[4] = "som".getBytes();
		Req[5] = "rum".getBytes();
		Req[6] = ByteUtil.toBytes(1);
		Req[7] = ByteUtil.toBytes(2);
		Req[8] = ByteUtil.toBytes(3);
		Req[9] = ByteUtil.toBytes(4);
		Req[10] = ByteUtil.toBytes(5);
		Req[11] = ByteUtil.toBytes(6);
		
		conn.execute(new Insert(0, 1,  Req[6], Req[0], Req[3]));
		conn.execute(new Insert(0, 1,  Req[7], Req[1], Req[4]));
		conn.execute(new Insert(0, 1,  Req[8], Req[2], Req[3]));
		conn.execute(new Insert(0, 1,  Req[9], Req[0], Req[5]));
		conn.execute(new Insert(0, 1, Req[10], Req[1], Req[4]));
		conn.execute(new Insert(0, 1, Req[11], Req[2], Req[3]));
		
		Tuple tup = new Tuple();
		
		tup.add(Req[6]);
		Resp[0] = conn.execute(new Select(0, 0, 0, MAX_LIMIT, tup.toByte()));
		Assert.assertEquals(Arrays.deepToString(Resp[0].get_tuple()[0]), "[[1, 0, 0, 0, 0, 0, 0, 0], [82, 105, 99, 104, 97, 114, 100], [112, 111, 109]]");
		System.out.println("Test Passed");
		
		tup.clear(); tup.add(Req[9]);
		Resp[1] = conn.execute(new Select(0, 0, 0, MAX_LIMIT, tup.toByte()));
		Assert.assertEquals(Arrays.deepToString(Resp[1].get_tuple()[0]), "[[4, 0, 0, 0, 0, 0, 0, 0], [82, 105, 99, 104, 97, 114, 100], [114, 117, 109]]");
		System.out.println("Test Passed");
		
		tup.clear(); tup.add(Req[1]);
		Resp[2] = conn.execute(new Select(0, 1, 0, MAX_LIMIT, tup.toByte()));
		Assert.assertEquals(Arrays.deepToString(Resp[2].get_tuple()[0]), "[[2, 0, 0, 0, 0, 0, 0, 0], [83, 101, 108, 109, 97], [115, 111, 109]]");
		Assert.assertEquals(Arrays.deepToString(Resp[2].get_tuple()[1]), "[[5, 0, 0, 0, 0, 0, 0, 0], [83, 101, 108, 109, 97], [115, 111, 109]]");
		System.out.println("Test Passed");
		
		tup.clear(); tup.add(Req[10]);
		conn.execute(new Delete(0, 0, tup.toByte()));
		tup.clear(); tup.add(Req[1]);
		Resp[3] = conn.execute(new Select(0, 1, 0, MAX_LIMIT, tup.toByte()));
		Assert.assertEquals(Resp[3].get_tuple().length, 1);
		Assert.assertEquals(Arrays.deepToString(Resp[3].get_tuple()[0]), "[[2, 0, 0, 0, 0, 0, 0, 0], [83, 101, 108, 109, 97], [115, 111, 109]]");
		System.out.println("Test Passed");
		
	}

}
