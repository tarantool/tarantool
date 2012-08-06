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

package tarantool.sample;

import java.io.IOException;
import java.util.Arrays;

import junit.textui.TestRunner;

//import org.testng.Assert;

import tarantool.common.ByteUtil;
import tarantool.common.Constants;
import tarantool.common.Tuple;
import tarantool.connector.Connection;
import tarantool.connector.ConnectionImpl;
import tarantool.connector.Request;
import tarantool.connector.Response;
import tarantool.connector.exception.TarantoolConnectorException;
import tarantool.connector.request.Call;
import tarantool.connector.request.Delete;
import tarantool.connector.request.Insert;
import tarantool.connector.request.Ping;
import tarantool.connector.request.Select;
import tarantool.connector.socketpool.exception.SocketPoolException;
//import tarantool.connector.testing.RequestTest;

public class Sample {
    public static final int MAX_LIMIT = 0xFFFFFFFF;

    public static void main(String[] args) throws TarantoolConnectorException,
            SocketPoolException, InterruptedException, IOException {
        final Connection conn = new ConnectionImpl();
        conn.connect("127.0.0.1", 33013);
        final byte[][] Req = new byte[200][];
        final Response Resp[] = new Response[200];

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

        Tuple t = new Tuple();
        t.add(Req[0], Req[3], Req[6]);
        System.out.println(Arrays.toString(new Ping().toByte()));
        System.out.println(Arrays.toString(new Insert(0, 0, t).toByte()));
        System.out.println(Arrays.toString(new Insert(0, Constants.BOX_ADD, t).toByte()));
        System.out.println(Arrays.toString(new Insert(0, Constants.BOX_ADD | Constants.BOX_REPLACE, t).toByte()));
        t.clear();
        t.add(Req[0]);
        System.out.println(Arrays.toString(new Select(1, 2, 3, 4, t.toByte()).toByte()));
        System.out.println(Arrays.toString(new Delete(1, 2, t.toByte()).toByte()));
        System.out.println(Arrays.toString(new Call(1, "LOLWHAT", t.toByte()).toByte()));
        System.out.println(Arrays.toString(new Call(1, "LOLWHAT".getBytes("CP1251"), t.toByte()).toByte()));
        
        //TestRunner.run(RequestTest.suite());
//        RequestTest test = new RequestTest();
//	Request req = new Ping();
//	System.out.println(Arrays.toString(req.toByte()));

//        conn.execute(new Insert(0, 1, Req[6], Req[0], Req[3]));
//        conn.execute(new Insert(0, 1, Req[7], Req[1], Req[4]));
//        conn.execute(new Insert(0, 1, Req[8], Req[2], Req[3]));
//        conn.execute(new Insert(0, 1, Req[9], Req[0], Req[5]));
//        conn.execute(new Insert(0, 1, Req[10], Req[1], Req[4]));
//        conn.execute(new Insert(0, 1, Req[11], Req[2], Req[3]));
//
//        final Tuple tup = new Tuple();
//
//        tup.add(Req[6]);
//        Resp[0] = conn.execute(new Select(0, 0, 0, MAX_LIMIT, tup.toByte()));
//        Assert.assertEquals(Arrays.deepToString(Resp[0].get_tuple()[0]),
//                "[[1, 0, 0, 0, 0, 0, 0, 0], [82, 105, 99, 104, 97, 114, 100], [112, 111, 109]]");
//        System.out.println("Test Passed");
//
//        tup.clear();
//        tup.add(Req[9]);
//        Resp[1] = conn.execute(new Select(0, 0, 0, MAX_LIMIT, tup.toByte()));
//        Assert.assertEquals(Arrays.deepToString(Resp[1].get_tuple()[0]),
//                "[[4, 0, 0, 0, 0, 0, 0, 0], [82, 105, 99, 104, 97, 114, 100], [114, 117, 109]]");
//        System.out.println("Test Passed");
//
//        tup.clear();
//        tup.add(Req[1]);
//        Resp[2] = conn.execute(new Select(0, 1, 0, MAX_LIMIT, tup.toByte()));
//        Assert.assertEquals(Arrays.deepToString(Resp[2].get_tuple()[0]),
//                "[[2, 0, 0, 0, 0, 0, 0, 0], [83, 101, 108, 109, 97], [115, 111, 109]]");
//        Assert.assertEquals(Arrays.deepToString(Resp[2].get_tuple()[1]),
//                "[[5, 0, 0, 0, 0, 0, 0, 0], [83, 101, 108, 109, 97], [115, 111, 109]]");
//        System.out.println("Test Passed");
//
//        tup.clear();
//        tup.add(Req[10]);
//        conn.execute(new Delete(0, 0, tup.toByte()));
//        tup.clear();
//        tup.add(Req[1]);
//        Resp[3] = conn.execute(new Select(0, 1, 0, MAX_LIMIT, tup.toByte()));
//        Assert.assertEquals(Resp[3].get_tuple().length, 1);
//        Assert.assertEquals(Arrays.deepToString(Resp[3].get_tuple()[0]),
//                "[[2, 0, 0, 0, 0, 0, 0, 0], [83, 101, 108, 109, 97], [115, 111, 109]]");
//        System.out.println("Test Passed");
    }
}
