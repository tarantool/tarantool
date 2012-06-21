package tarantool.sample;

import java.io.IOException;
import java.util.Arrays;

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
	public static void main(String[] args) throws TarantoolConnectorException, SocketPoolException, InterruptedException, IOException {
		byte[][] arr = {
				{1, 2, 3, 0},
				{0, 2, 2, 0},
				{3, 2, 1, 0}
		};
		/*Request req[] = new Request[5];
		req[0] = new Select(0, 0, 0, 0, arr);
		req[1] = new Insert(0, 1, arr);
		req[2] = new Delete(0, 0, arr);
		req[3] = new Ping();
		req[4] = new Call(123, "curriculum_vitae", arr);
		for (int i = 0; i < req.length; ++i)
			System.out.println(Arrays.toString(req[i].toByte()));
		Connection conn = new Connection();
		conn.connect("127.0.0.1", 33013);
		Response resp = conn.execute(req[1]);
		System.out.println(Arrays.deepToString(resp.get_tuple()));*/
		
		Tuple tup = new Tuple();
		tup.add(arr[0]);
		tup.add(arr[1]);
		tup.add(arr[2]);
		for (byte[] e: tup){
			System.out.println(Arrays.toString(e));
		}
	}

}
