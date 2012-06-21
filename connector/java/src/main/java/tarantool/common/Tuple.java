package tarantool.common;

import java.util.AbstractList;
import java.util.Arrays;
import java.util.ConcurrentModificationException;
import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;
import java.util.RandomAccess;
import java.util.ListIterator;

public class Tuple extends AbstractList<byte[]> implements List<byte[]>, RandomAccess{

	private byte[][] data;
	
	private int size;
	
	public Tuple(int length){
		super();
		if (length < 0)
			throw new IllegalArgumentException("Illegal Capacity: " + length);
		this.data = new byte[length][];
		size = 0;
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
	private class ListItr extends Itr implements ListIterator<byte []>{
		ListItr(int index){
			super();
			cursor = index;
		}
		
		public boolean hasPrevious(){
			return cursor != 0;
		}
		public int nextIndex(){
			return cursor;
		}
		
		public int previousIndex(){
			return cursor - 1;
		}
		
		//@SuppressWarnings("Unchecked")
		public byte[] previous(){
			if (modCount != expectedModCount)
				throw new ConcurrentModificationException();
			int i = cursor - 1;
			if (i < 0)
				throw new NoSuchElementException();
			byte[][] _data = Tuple.this.data;
			if (i >= data.length)
				throw new ConcurrentModificationException();
			cursor = i;
			return _data[lastRet = i];
		}
		
		public void set(byte[] arr){
			if (lastRet < 0)
				throw new IllegalStateException();
			if (modCount != expectedModCount)
				throw new ConcurrentModificationException();
			
			try{
				Tuple.this.set(lastRet, arr);
			} catch(IndexOutOfBoundsException ex){
				throw new ConcurrentModificationException();
			}
		}
		
		public void add(byte[] arr){
			if (modCount != expectedModCount)
				throw new ConcurrentModificationException();
			try{
				int i = cursor;
				 Tuple.this.add(i, arr);
				 cursor = i + 1;
				 lastRet = -1;
				 expectedModCount = modCount;
			} catch (IndexOutOfBoundsException ex){
				throw new ConcurrentModificationException();
			}
		}
	}
	
	private class Itr implements Iterator<byte []>{

		int cursor;
		int lastRet = -1;
		int expectedModCount = modCount;
		
		@Override
		public boolean hasNext() {
			return cursor != size;
		}

		@Override
		public byte[] next() {
			if (modCount != expectedModCount)
				throw new ConcurrentModificationException();
			int i = cursor;
			if (i >= size)
				throw new NoSuchElementException();
			byte[][] _data = Tuple.this.data;
			if (i >= _data.length)
				throw new ConcurrentModificationException();
			cursor = i + 1;
			return _data[lastRet = i];
		}

		@Override
		public void remove() {
			if (lastRet < 0)
				throw new IllegalStateException();
			if (modCount != expectedModCount)
				throw new ConcurrentModificationException();
			try{
				Tuple.this.remove(lastRet);
				cursor = lastRet;
				lastRet = -1;
				expectedModCount = modCount;
			} catch (IndexOutOfBoundsException ex){
				throw new ConcurrentModificationException();
			}
		}
	
		public ListIterator<byte []> listIterator(int index){
			if (index < 0 || index > size)
				throw new IndexOutOfBoundsException("Index: "+index);
			return new ListItr(index);
		}
		public ListIterator<byte []> listIterator(){
			return new ListItr(0);
		}
		public Iterator<byte []> iterator(){
			return new Itr();
		}
	}

}
