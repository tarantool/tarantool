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

package tarantool.common;

import java.util.AbstractList;
import java.util.Arrays;
import java.util.ConcurrentModificationException;
import java.util.Iterator;
import java.util.List;
import java.util.ListIterator;
import java.util.NoSuchElementException;
import java.util.RandomAccess;

public class Tuple extends AbstractList<byte[]> implements List<byte[]>,
        RandomAccess {
    private class Itr implements Iterator<byte[]> {
        int cursor;
        int expectedModCount = modCount;
        int lastRet = -1;

        @Override
        public boolean hasNext() {
            return cursor != size;
        }

        public Iterator<byte[]> iterator() {
            return new Itr();
        }

        public ListIterator<byte[]> listIterator() {
            return new ListItr(0);
        }

        public ListIterator<byte[]> listIterator(int index) {
            if (index < 0 || index > size)
                throw new IndexOutOfBoundsException("Index: " + index);
            return new ListItr(index);
        }

        @Override
        public byte[] next() {
            if (modCount != expectedModCount)
                throw new ConcurrentModificationException();
            final int i = cursor;
            if (i >= size)
                throw new NoSuchElementException();
            final byte[][] _data = Tuple.this.data;
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
            try {
                Tuple.this.remove(lastRet);
                cursor = lastRet;
                lastRet = -1;
                expectedModCount = modCount;
            } catch (IndexOutOfBoundsException ex) {
                throw new ConcurrentModificationException();
            }
        }
    }

    // TODO: Make iterators, make push(int, long..) add(index, (int, long, ..));
    private class ListItr extends Itr implements ListIterator<byte[]> {
        ListItr(int index) {
            super();
            cursor = index;
        }

        @Override
        public void add(byte[] arr) {
            if (modCount != expectedModCount)
                throw new ConcurrentModificationException();
            try {
                final int i = cursor;
                Tuple.this.add(i, arr);
                cursor = i + 1;
                lastRet = -1;
                expectedModCount = modCount;
            } catch (IndexOutOfBoundsException ex) {
                throw new ConcurrentModificationException();
            }
        }

        @Override
        public boolean hasPrevious() {
            return cursor != 0;
        }

        @Override
        public int nextIndex() {
            return cursor;
        }

        @Override
        public byte[] previous() {
            if (modCount != expectedModCount)
                throw new ConcurrentModificationException();
            final int i = cursor - 1;
            if (i < 0)
                throw new NoSuchElementException();
            final byte[][] _data = Tuple.this.data;
            if (i >= data.length)
                throw new ConcurrentModificationException();
            cursor = i;
            return _data[lastRet = i];
        }

        @Override
        public int previousIndex() {
            return cursor - 1;
        }

        @Override
        public void set(byte[] arr) {
            if (lastRet < 0)
                throw new IllegalStateException();
            if (modCount != expectedModCount)
                throw new ConcurrentModificationException();

            try {
                Tuple.this.set(lastRet, arr);
            } catch (IndexOutOfBoundsException ex) {
                throw new ConcurrentModificationException();
            }
        }
    }

    private static final int MAX_ARRAY_SIZE = Integer.MAX_VALUE - 8;

    private static int hugeCapacity(int minCapacity) throws OutOfMemoryError {
        if (minCapacity < 0)
            throw new OutOfMemoryError();
        return (minCapacity > MAX_ARRAY_SIZE ? Integer.MAX_VALUE
                : MAX_ARRAY_SIZE);
    }

    private byte[][] data;

    private int size;

    public Tuple() {
        this(10);
    }

    public Tuple(int length) {
        super();
        if (length < 0)
            throw new IllegalArgumentException("Illegal Capacity: " + length);
        this.data = new byte[length][];
        size = 0;
    }

    @Override
    public boolean add(byte[] arr) {
        ensureCapacity(size + 1);
        data[size++] = arr;
        return true;
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

    @Override
    public void clear() {
        modCount++;

        for (int i = 0; i < size; ++i)
            data[i] = null;

        size = 0;
    }

    byte[] data(int index) {
        if (index >= size | index < 0)
            throw new IndexOutOfBoundsException();
        return data[index];
    }

    private void ensureCapacity(int minCapacity) {
        if (minCapacity > 0 & minCapacity > data.length) {
            modCount++;
            grow(minCapacity);
        }
    }

    @Override
    public byte[] get(int index) {
        if (index >= size | index < 0)
            throw new IndexOutOfBoundsException();
        return data(index);
    }

    private void grow(int length) {
        final int oldCapacity = data.length;
        int newCapacity = oldCapacity + (oldCapacity << 1);
        if (newCapacity < length)
            newCapacity = length;
        if (newCapacity > MAX_ARRAY_SIZE)
            newCapacity = hugeCapacity(length);
        data = Arrays.copyOf(data, newCapacity);
    }

    @Override
    public boolean isEmpty() {
        return (size == 0);
    }

    private String outOfBoundsMsg(int index) {
        return "Index: " + index + ", Size: " + size;
    }

    @Override
    public byte[] remove(int index) {
        if (index >= size | index < 0)
            throw new IndexOutOfBoundsException();

        modCount++;
        final byte[] oldValue = data[index];

        final int numMoved = size - index - 1;
        if (numMoved > 0)
            System.arraycopy(data, index + 1, data, index, numMoved);

        data[--size] = null;

        return oldValue;
    }

    @Override
    public byte[] set(int index, byte[] element) {
        if (index >= size | index < 0)
            throw new IndexOutOfBoundsException();
        final byte[] oldValue = data[index];
        return data(index);
    }

    @Override
    public int size() {
        return size;
    }

    @Override
    public byte[][] toArray() {
        return Arrays.copyOf(data, size);
    }

    public byte[][] toByte() {
        return Arrays.copyOf(this.data, size);
    }

    public void trimToSize(int size) {
        modCount++;
        final int oldCapacity = data.length;
        if (size < oldCapacity) {
            data = Arrays.copyOf(data, size);
        }
    }

}
