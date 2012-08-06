package tarantool.connector.testing;

import java.io.UnsupportedEncodingException;
import java.util.Arrays;

import tarantool.common.ByteUtil;
import tarantool.common.Constants;
import tarantool.common.Tuple;
import tarantool.connector.Request;
import tarantool.connector.request.Call;
import tarantool.connector.request.Delete;
import tarantool.connector.request.Insert;
import tarantool.connector.request.Ping;
import tarantool.connector.request.Select;
import junit.framework.Test;
import junit.framework.TestCase;
import junit.framework.TestSuite;
import junit.textui.TestRunner;
import static org.junit.Assert.*;


public class RequestTest extends TestCase {
	byte[][] Req = new byte[3][];
	
	public RequestTest(){
	        Req[0] = "Richard".getBytes();
	        Req[1] = "pom".getBytes();
	        Req[2] = ByteUtil.toBytes(1);
	}
	
	public void testPing(){
		byte[] ping = {0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		Request req = new Ping();
		//System.out.println(Arrays.toString(req.toByte()));
		//System.out.println(req.toByte());
		assertArrayEquals(ping, req.toByte());
		System.out.println("-------------------------------" +
				"Ping test success" +
				"-------------------------------");
	}
	
	public void testInsert(){
		Tuple t = new Tuple();
		t.add(Req[0], Req[1], Req[2]);
		byte[] insert1 = {13,0,0,0,33,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,7,
				82,105,99,104,97,114,100,3,112,111,109,8,1,0,0,0,0,0,0,0};
		Request req = new Insert(0, 0, t);
	        assertArrayEquals(req.toByte(), insert1);
	        byte[] insert2 = {13,0,0,0,33,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,3,0,0,0,7,
				82,105,99,104,97,114,100,3,112,111,109,8,1,0,0,0,0,0,0,0};
		req = new Insert(0, Constants.BOX_ADD, t);
		assertArrayEquals(req.toByte(), insert2);
		byte[] insert3 = {13,0,0,0,33,0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,3,0,0,0,7,
				82,105,99,104,97,114,100,3,112,111,109,8,1,0,0,0,0,0,0,0};
		req = new Insert(0, Constants.BOX_ADD | Constants.BOX_REPLACE, t);
	        assertArrayEquals(req.toByte(), insert3);
		System.out.println("-------------------------------" +
				"Insert test success" +
				"-------------------------------");
	}
	
	public void testSelect(){
		Tuple t = new Tuple();
		t.add(Req[0]);
		byte[] select = {17,0,0,0,32,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,3,0,0,0,4,0,
				0,0,1,0,0,0,1,0,0,0,7,82,105,99,104,97,114,100};
		Request req = new Select(1, 2, 3, 4, t.toByte());
		assertArrayEquals(req.toByte(), select);
		System.out.println("-------------------------------" +
				"Select test success" +
				"-------------------------------");
	}
	
	public void testDelete(){
		Tuple t = new Tuple();
		t.add(Req[0]);
		byte[] delete = {21,0,0,0,20,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,1,0,0,0,7,
				82,105,99,104,97,114,100};
		Request req = new Delete(1, 2, t.toByte());		
		assertArrayEquals(req.toByte(), delete);
		System.out.println("-------------------------------" +
				"Delete test success" +
				"-------------------------------");
	}
	
	public void testCall(){
		Tuple t = new Tuple();
		t.add(Req[0]);
		byte[] call = {22,0,0,0,24,0,0,0,0,0,0,0,1,0,0,0,7,76,79,76,87,72,65,
				84,1,0,0,0,7,82,105,99,104,97,114,100};
		Request req = new Call(1, "LOLWHAT", t.toByte());
		assertArrayEquals(req.toByte(), call);
		try {
	                req = new Call(1, "LOLWHAT".getBytes("CP1251"), t.toByte());
                } catch (UnsupportedEncodingException e) {
	                e.printStackTrace();
                }
		assertArrayEquals(req.toByte(), call);
		System.out.println("-------------------------------" +
				"Call test success" +
				"-------------------------------");
	}
	
	public static Test suite(){
		return new TestSuite(RequestTest.class);
	}
	
//	public static void main(String args[]) {
//		    TestRunner.run(suite());
//	}
}
