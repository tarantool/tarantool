package tarantool.common;

import java.util.AbstractList;
import java.util.Arrays;
import java.util.List;
import java.util.RandomAccess;

public class Tuple extends AbstractList<byte[]> implements List<byte[]>, RandomAccess, Cloneable{

	private byte[][] data;
	
	private int size;
	
	public Tuple(int length){
		super();
		if (length < 0)
			throw new IllegalArgumentException("Illegal Capacity: " + length);
		this.data = new byte[length][];
		size = length;
	}
	
	public Tuple(){
		this(10);
	}
	
	private void ensureCapacity(int minCapacity){
		if (minCapacity > 0 & minCapacity > data.length){
			modCount++;
			grow(minCapacity);
		}	
	}
	
	private static final int MAX_ARRAY_SIZE = Integer.MAX_VALUE - 8;
	
	private static int hugeCapacity(int minCapacity) throws OutOfMemoryError{
		if (minCapacity < 0)
			throw new OutOfMemoryError();
		return (minCapacity > MAX_ARRAY_SIZE ? Integer.MAX_VALUE : MAX_ARRAY_SIZE);
	}
	
	private void grow(int length){
		int oldCapacity = data.length;
		int newCapacity = oldCapacity + (oldCapacity << 1);
		if (newCapacity < length)
			newCapacity = length;
		if (newCapacity > MAX_ARRAY_SIZE)
			newCapacity = hugeCapacity(length);
		data = Arrays.copyOf(data, newCapacity);
	}
	
	public void trimToSize(int size){
		modCount++;
		int oldCapacity = data.length;
		if (size < oldCapacity){
			data = Arrays.copyOf(data, size);
			//this.size = size;
		}
	}
	
	@Override
	public byte[] get(int index) {
		if (index >= size | index < 0)
			throw new IndexOutOfBoundsException();
		return data(index);
	}
	
	public byte[] set(int index, byte[] element) {
		if (index >= size | index < 0)
			throw new IndexOutOfBoundsException();
		byte[] oldValue  = data[index];
		return data(index);
	}

	@Override
	public int size() {
		return size;
	}
	
	public boolean isEmpty(){
		return (size == 0);
	}

	@Override
	public byte[][] toArray() {
		// TODO Auto-generated method stub
		return Arrays.copyOf(data, size);
	}
	
	byte[] data(int index){
		if (index >= size | index < 0)
			throw new IndexOutOfBoundsException();
		return data[index];
	}
	
	@Override
	public void add(int index, byte[] element) {
		if (index > size | index < 0)
			throw new IndexOutOfBoundsException();
		ensureCapacity(size + 1);
		System.arraycopy(data, index, data, index + 1, size - index);
		data[index] = element;
		size++;
	}
	
	public boolean add (byte[] arr){
		ensureCapacity(size + 1);
		data[size++] = arr;
		return true;
	}
	
	public byte[] remove(int index){
		if (index >= size | index < 0)
			throw new IndexOutOfBoundsException();
		
		modCount++;
		byte[] oldValue = data[index];
		
		int numMoved = size - index - 1;
		if (numMoved > 0)
				System.arraycopy(data, index+1, data, index, numMoved);
		
		data[--size] = null;
		
		return oldValue;
	}
	
	public void clear(){
		modCount++;
		
		for (int i = 0; i < size; ++i)
			data[i] = null;
		
		size = 0;
	}
	
	private String outOfBoundsMsg(int index){
		return "Index: "+index+", Size: "+size;
	}
	
	//TODO: Make iterators, make push(int, long..) add(index, (int, long, ..));
}
