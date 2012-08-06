package tarantool.connector.testing;

import junit.framework.Test;
import junit.framework.TestSuite;
import junit.textui.TestRunner;

public class AllTest {
	public static Test suite(){
		TestSuite suite	= new TestSuite("All Tests");
		return suite;
	}
}
