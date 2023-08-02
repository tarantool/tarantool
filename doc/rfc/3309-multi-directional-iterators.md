# Multi-directional iterators

* **Status**: In progress
* **Start date**: 22-01-2020
* **Authors**: [Nikita Pettik](https://github.com/korablev77), [Magomed Kostoev](https://github.com/mkostoevr)
* **Issues**: [#3243](https://github.com/tarantool/tarantool/issues/3243), [#3309](https://github.com/tarantool/tarantool/issues/3309)

## Requirements

1. Should we be able to not only specify sort order of a field during iteration
   but also conditions aganist which we comapre? Like
   `select({1, 2, 3}, {LT, GT, LT})`. Or we only need per-field sort orer.
2. Should we separate the order and the condition? Let's say I want all tuples
   with field 2 smaller than 10 in ascending order. It will also allow to
   select all the tupples less than {10, 10, 99}, but want to sort the result
   in {asc, desc, asc} order.

## My concerns about the idea

The semantics of a tree index is that we sort the tuples by the key and so make
it easy to search the _key value_, which is interprethed as a scalar. This is
the idea of the tree index: it's a sorted collection of scalars. So the
comparison semantics we have is that we compare the _tuple key value_ with the
search _key value_.

Does the sort order specification break the idea? Yes it does, but not as much
as per-part conditional search.

If we just implement a sort order we presevre the comparison semantics: the
tuple is still scalar according to the search result: we return a range of
values from the index and comparisons work as before. So the tree works as it
should.

If we have a per-part condition, then index search changes the tuple comparison
semantics: now the tuple key is not just a scalar we sort in our index, the key
is a multipart structure and we are free to compare each part of the structure
independently. Again, it's possible in Tree index but will lead to significant
slowdown. To me it's a kind of `BITS_ALL_SET` or `NEIGHBOR` - the thing that
should be available in a specific index that is supposed to solve these kinds
of problems, but the functionality is not supposed for the BPS-tree.

## Summary

Multi-directional iterator is an iterator which allows iterating through
different key parts orders. For instance: consider index `i` consisting of three
key parts: `a`, `b` and `c`. Creating and using casual iterator looks like this:
```
i = box.space.T.index.i
i:select({1, 2, 3}, {iterator = 'EQ'}) -- returns all tuples which has
                                       -- fields a == 1, b == 2, c == 3.
```
It is OK to omit one or more key parts declaring key value to be searched. In
this case they are assumed to be nils:
`i:select({1}, {iterator = 'EQ'})` is the same as
`i:select({1, nil, nil}, {iterator = 'EQ'})`. So all tuples which has `a == 1`
are getting to the result set. More formally matching rule for nil parts is
following (returns TRUE in case a search key is matched with an index key):
```
if (search-key-part[i] is nil) {
  if (iterator is LT or GT)
	return FALSE
  return TRUE
}
```

Another example:
`i:select({1, 1, 1}, {iterator = 'GE'})`

Here all tuples with
`(a = 1 AND b = 1 AND c >= 1) OR (a = 1 AND b > 1) OR (a > 1)`
are returned keeping the lexicographically order.
But some users may want to select tuples with `a >= 1`, `b >= 1` but `c < 1`.
Moreover, somebody may be willing to get tuples ordered by `a` and `b` in
ascending order but by `c` in descending order:
`i:select({}, {iterator = {'GE', 'GE', 'LE'})`, which is analogue of common SQL
query `SELECT * FROM t ORDER BY a ASC, b ASC, c DESC`. Or even query like this:
`SELECT * FROM t WHERE a > 1 AND b < 1 ORDER BY a ASC, b DESC`
which corresponds to Tarantool's
`i:select({1, 1}, {iterator = {'GT', 'LT'})`.
These requests are obviously impossible to fulfill with current indexes and
iterators implementations. This RFC suggests ways to resolve mentioned problem
in particular for Memtx TREE indexes.

## Non-invasive approach

TREE indexes in memtx engine are implemented as BPS-trees (see
`src/lib/salad/bps_tree.h` for details). Keys correspond to particular values
of key parts; data - to pointers to tuples. Hence, all data are sorted by their
key values due to tree structure. For this reason HASH indexes have only GT and
EQ (and ergo GE) iterators - data stored in a hash is unordered. Tree interface
itself provides several functions to operate on data.  Iteration process starts
in `tree_iterator_start()` (which is called once as `iterator->next()`):
depending on its type iterator is positioned to the lower or upper bound (via
`memtx_tree_lower_bound()`) of range of values satisfying search condition.
In case key is not specified (i.e. empty), iterator is simply set to the first
or the last element of tree. At this moment first element to be returned (if any)
is ready. To continue iterating `next` method of iterator object is changed to one
of `tree_iterator_next()`, `tree_iterator_prev()` or their analogues for GE and
LE iterators. Actually these functions fetch next element from B-tree leaf
block. If iterator points to the last element in the block, it is set to the
first element of the next block (leaf blocks are linked into list); if there's
no more blocks, iterator is invalidated and iteration process is finished.  
Taking into account this information let's consider implementation of
multi-directional iterators.

### Implementation details

Let's suppose we don't have `sort_order` implemented and so the tuples are
stored in ascending order by all their parts. And we want to unconditionally
select all the values but with different `sort_order` for some parts. Example:

```lua
s:create_index('i', {parts = {{1, 'integer'}, {2, 'integer'}}})`
s:insert({1, 1})
s:insert({1, 2})
s:insert({1, 3})
s:insert({2, 1})
s:insert({2, 2})
s:insert({2, 3})
s:insert({3, 1})
s:insert({3, 2})
s:insert({3, 3})
i:select({}, {iterator = {'GE', 'LE'}})
```

The selection result should be the following:

```
[1, 3]
[1, 2]
[1, 1]
[2, 3]
[2, 2]
[2, 1]
[3, 3]
[3, 2]
[3, 1]
```

So, the first part is ascending, the second one is descending. The straight
forward way to approach this iteration order is the following: for each unique
first field we search its upper bound and iterate from that backwards
(exclusively, meaning, from the upper bound iterator minus one).

Illustration (iteration order above, upper bounds below):

```
3  2  1  6  5  4  9  8  7
11 12 13 21 22 23 31 32 33
^        ^        ^        ^
|        |        |        +- upper_bound({3, nil}) == end()
|        |        +- upper_bound({2, nil})
|        +- upper_bound({1, nil})
+- begin()
```

So, we:
1. Start iterating from `upper_bound({1, nil}) - 1` to `begin()` inclusive.
2. Start iterating from `upper_bound({2, nil}) - 1` to `upper_bound({1, nil})` inclusive.
3. Start iterating from `upper_bound({3, nil}) - 1` to `upper_bound({2, nil})` inclusive.
4. `upper_bound({3, nil})` is `end()`, so we finish.

The pseudocode for this case looks like this:

```
level_1_it = begin();
while level_1_it != end() {
	level_1_it_next = upper_bound({level_1_it->field_1, nil});
	level_2_it = level_1_it_next - 1;
	do {
		results.push(*level_2_it);
		level_2_it--;
	} while level_2_it != level_1_it;
	level_1_it = level_1_it_next;
}
```

Let's consider more complex example: three-part index. And unconditional select
with different sort orders.

```
s:create_index('i', {parts = {{1, 'integer'}, {2, 'integer'}, {3, 'integer'}}})`
--[[ Fill the space with the tuples:
    111, 112, 113, 121, 122, 123, 131, 132, 133,
    211, 212, 213, 221, 222, 223, 231, 232, 233
]]--
s:select({}, {iterator = {'GE', 'LE', 'GE'}})
```

The selection result should be this:

```
[1, 3, 1]
[1, 3, 2]
[1, 3, 3]
[1, 2, 1]
[1, 2, 2]
[1, 2, 3]
[1, 1, 1]
[1, 1, 2]
[1, 1, 3]
[2, 3, 1]
[2, 3, 2]
[2, 3, 3]
[2, 2, 1]
[2, 2, 2]
[2, 2, 3]
[2, 1, 1]
[2, 1, 2]
[2, 1, 3]
```

So the first part is ascending, the second one is descending and the third is
ascending. The iteration algorithm is a scaled version of the previous one.
Let's see the process first.

The required illustration (iteration order above, bounds points below).

```
part_1_it: 1                                   2                                   3
part_2_it:         3           2           1           6           5           4
part_3_it: 7   8   9   4   5   6   1   2   3   16  17  18  13  14  15  10  11  12
           111 112 113 121 122 123 131 132 133 211 212 213 221 222 223 231 232 233
           ^           ^           ^           ^           ^           ^           ^
           |           |           |           |           |           |           / upper_bound({2, 3, nil})
           |           |           |           |           |           |           | upper_bound({2, nil})
           |           |           |           |           |           |           \ end()
           |           |           |           |           |           / lower_bound({2, 3, nil})
           |           |           |           |           |           \ upper_bound({2, 2, nil})
           |           |           |           |           / lower_bound({2, 2, nil})
           |           |           |           |           \ upper_bound({2, 1, nil})
           |           |           |           / lower_bound({2, 1, nil})
           |           |           |           | upper_bound({1, 3, nil})
           |           |           |           \ upper_bound({1, nil})
           |           |           / lower_bound({1, 3, nil})
           |           |           \ upper_bound({1, 2, nil})
           |           / lower_bound({1, 2, nil})
           |           \ upper_bound({1, 1, nil})
           \ begin()
```

We have three iterators here: one iterates over tuples with distinct values of
first field, the second one is recursively iterating over tuples with distinct
values of the second field, and the third one recursively iterates over the
tuples with distinct values of the third field. Let's call them `part_1_it`,
`part_2_it` and `part_3_it` respectively.

So here are the steps we perform in order to provide the right iteration order:
1. The first part is ascending, so we get the first item in the space. The item
   is `{1, 1, 1}`. This is the current target of the `part_1_it` iterator.

   Formally:
   `part_1_it = begin() -- {1, 1, 1}`
2. The first part is ascending, so we find the upper bound of `{1, nil, nil}`.
   The result is `{2, 1, 1}`. This is going to be the next target of the
   `part_1_it` iterator.

   Formally:
   `part_1_it_next = upper_bound({1, nil, nil}) -- {2, 1, 1}`
3. The second part is descending, so we find a tuple with field 1 equal to 1
   with the biggest field 2. To do that we find the upper bound of
   `{1, nil, nil}` and iterate one tuple back. Here we see `{1, 3, 3}` tuple.
   This is the current target of the `part_2_it` iterator.

   Formally:
   `part_2_it = upper_bound({1, nil, nil}) - 1 -- {1, 3, 3}`
4. The second part is descending, so we find a tuple with field 1 equal to 1
   with the biggest field 2 less than 3. To do that we find we find the lower
   bound of `{1, 3, nil}` and iterate one tuple back. Here we see `{1, 2, 3}`
   tuple. This is going to be the next target of the `part_2_it` iterator.

   Formally:
   `part_2_it_next = lower_bound({1, 3, nil}) - 1 -- {1, 2, 3}`
5. The third part is ascending so we find the lower bound of `{1, 3, nil}`.
   The result is `{1, 3, 1}`. This is the current target of the `part_3_it`
   iterator.

   Formally:
   `part_3_it = lower_bound({1, 3, nil}) -- {1, 3, 1}`
6. Start reading tuples from the `part_3_it` up to the `part_2_it + 1`
   (exclusive).

   Formally:
   `while part_3_it != part_2_it + 1 { yield *part_2_it; part_2_it++; }`
7. Advance the `part_2_it` (we have found the next target in the step 4). The
   new position of the iterator is `{1, 2, 3}`.

   Formally:
   `part_2_it = part_2_it_next -- {1, 2, 3}`
8. `part_2_it_next = lower_bound({1, 2, nil}) - 1 -- {1, 1, 3}`
9. `part_3_it = lower_bound({1, 2, nil}) -- {1, 2, 1}`
10. `while part_3_it != part_2_it + 1 { yield *part_2_it; part_2_it++; }`
11. `part_2_it = part_2_it_next -- {1, 1, 3}`
12. `part_2_it_next = lower_bound({1, 1, nil}) - 1 -- part_1_it - 1 == INVALID`
13. `part_3_it = lower_bound({1, 1, nil}) -- {1, 1, 1}`
14. `while part_3_it != part_2_it + 1 { yield *part_2_it; part_2_it++; }`
15. `part_2_it_next == part_1_it - 1`, so, we are going to get out of the part
     2 loop and continue iteration with part 1.
16. `part_1_it = part_1_it_next -- {2, 1, 1}`
17. `part_1_it_next = upper_bound({1, nil, nil}) -- end() == INVALID`
18. `part_2_it = upper_bound({2, nil, nil}) - 1 -- {2, 3, 3}`
19. `part_2_it_next = lower_bound({2, 3, nil}) - 1 -- {2, 2, 3}`
20. `part_3_it = lower_bound({1, 3, nil}) -- {1, 3, 1}`
21. `while part_3_it != part_2_it + 1 { yield *part_2_it; part_2_it++; }`
22. `part_2_it = part_2_it_next -- {2, 2, 3}`
23. `part_2_it_next = lower_bound({2, 2, nil}) - 1 -- {2, 1, 3}`
24. `part_3_it = lower_bound({2, 2, nil}) -- {2, 2, 1}`
25. `while part_3_it != part_2_it + 1 { yield *part_2_it; part_2_it++; }`
26. `part_2_it = part_2_it_next -- {2, 1, 3}`
27. `part_2_it_next = lower_bound({2, 1, nil}) - 1 -- {1, 3, 3}, same as part_1_it - 1`
28. `part_3_it = lower_bound({2, 1, nil}) -- {2, 1, 1}`
29. `while part_3_it != part_2_it + 1 { yield *part_2_it; part_2_it++; }`
30. `part_2_it_next == part_1_it - 1`, so, we are going to get out of the part
     2 loop and continue iteration with part 1.
31. `part_1_it_next == end()`, so, we are going to finish.

So, the iterators movement for the `{'GE', 'LE', 'GE'}` query is this:
1. `part_1_it` is going from `begin()` to `end()` exclusive.
2. `part_2_it` is going from `part_1_it_next - 1` to `part_1_it - 1` exclusive.
3. `part_3_it` is going from `part_2_it_next + 1` to `part_2_it + 1` exclusive.

The iteration process of each of these iterators is now not just a pointer
increment. Now each iterator has to search for its next position using lower
or upper bound depending on the iterator type and the current tuple value.

The rule for selecing upper or lower bound to approach the next target is
identical to the one for a regular requests:
- `lower_bound` for EQ, GE and LT.
- `upper_bound` for REQ, GT and LE.

But the key the search is performed against depends on the iterator level (its
1-based part number). The next position of the iterator depends on its current
position key. The key is cut to N parts, where N is the 1-based iterator level,
and used for the next position search.

In the example above we get the next position of the first iterator by getting
the upper bound of the current tuple's key cut to 1 part, because the iterator
type is GE.

Let's see more iteration movement patterns and ranges.

Pattern for `{'GE', 'LE', 'LE'}`:

```
part_1_it: 1                                   2                                   3
part_2_it:         3           2           1           6           5           4
part_3_it: 9   8   7   6   5   4   3   2   1   18  17  16  15  14  13  12  11  10
           111 112 113 121 122 123 131 132 133 211 212 213 221 222 223 231 232 233
```

Ranges:
1. [`begin()`, `end()`)
2. [`part_1_it_next - 1`, `part_1_it - 1`)
3. [`part_2_it`, `part_2_it_next`)

Pattern for `{'LE', 'LE', 'LE'}`:

```
part_1_it:                                 2                                   1
part_2_it:         6           5           4           3           2           1
part_3_it: 18  17  16  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1
           111 112 113 121 122 123 131 132 133 211 212 213 221 222 223 231 232 233
```

Ranges:
1. [`end() - 1`, `begin() - 1`)
2. [`part_1_it`, `part_1_it_next`)
3. [`part_2_it`, `part_2_it_next`)

Pattern for `{'LE', 'GE', 'LE'}`:

```
part_1_it:                                 2                                   1
part_2_it: 4           5           6           1           2           3
part_3_it: 12  11  10  15  14  13  18  17  16  3   2   1   6   5   4   9   8   7
           111 112 113 121 122 123 131 132 133 211 212 213 221 222 223 231 232 233
```

Ranges:
1. [`end() - 1`, `begin() - 1`)
2. [`part_1_it_next + 1`, `part_1_it + 1`)
3. [`part_2_it_next - 1`, `part_2_it - 1`)

Pattern for `{'LE', 'GE', 'GE'}`:

```
part_1_it:                                 2                                   1
part_2_it: 4           5           6           1           2           3
part_3_it: 10  11  12  13  14  15  16  17  18  1   2   3   4   5   6   7   8   9
           111 112 113 121 122 123 131 132 133 211 212 213 221 222 223 231 232 233
```

Ranges:
1. [`end() - 1`, `begin() - 1`)
2. [`part_1_it_next + 1`, `part_1_it + 1`)
3. [`part_2_it`, `part_2_it_next`)

#### Conditions with values

The next part iterator should only be started of we have the conditions we're
looking for according to this part. For example, consider the conditional query:
{GE 4, nil, nil}. For the tuple set we was working with above the query should
return no results, because there's no tuples with the first field greater or
equal to 4.

This example is simple, because we receive an invalid iterator trying to get
the lower bound of the {4, nil, nil} key. Let's see more difficult situation.

Let's say we have a tuple set: `11, 12, 13, 23, 31, 32, 33`. And we query the
following: {GE nil, LE 2}. So we want the ruples with the second field less or
equal to 2, the sort forward by the first field and reverse by the second and
the third field.

The algorithm proposed above does not allow to specify conditions for each part,
it only implements index sort order override. Let's see how to appriach the
iteration sequence requied by the query.

1. `part_1_it = begin() -- {1, 1}`, and this is a special case: the first field
   is nil, so we interpret this like an empty key, so we just iterate from the
   beginning of the index to the end. Here's a catch: should we do this? What is
   I want all the values that really less or equal to nil? Use the box.NULL? So
   we gonna have different semantics for nil and box.NULL, is this OK? We have
   the semantics that the last entries of a key can be nli, and then we match
   everything with the nil, and box.NULL, and then we only match actual nulls.
   But it's not the case for nil not being the last part of the key.
2. `part_1_it_next = upper_bound({1, nil}) -- {2, 3}`.
3. `part_2_it = upper_bound({1, 2}) - 1 -- {1, 2}`. Here's the interesting part:
   instead of getting the upper bound with nil as it was done before, we use the
   provided key.
4. `while part_2_it != part_1_it - 1 { yield *part_2_it; part_2_it--; }`
5. `part_1_it = part_1_it_next -- {2, 3}`
6. `part_1_it_next = upper_bound({2, nil}) -- {3, 1}`
7. `part_2_it = upper_bound({2, 2}) - 1 -- {1, 3}`. Here's one more interesting
   part: we couldn't find the tuple satisfying our requirements. So we got the
   iterator which equal to `part_1_it - 1` right after the first `upper_bound`.
   Is it always true? Will we always receive the enditerator? Or we have to
   check if the result is not only equal to the end, but also less than it?
   Can it be less that end?

   We have four possible variants of the begin/end iterators:
   1. FORWARD, FORWARD: [`prev_it`, `prev_it_next`]
   2. FORWARD, REVERSE: [`prev_it_next - 1`, `prev_it - 1`).
   3. REVERSE, FORWARD: [`prev_it_next + 1`, `prev_it + 1`).
   4. REVERSE, REVERSE: [`prev_it`, `prev_it_next`]

   So we have situations:
   ```
   1. FORWARD, FORWARD

      part_1_it: 1           2
      part_2_it: 1   2   3   4   5   6
                 11, 12, 13, 21, 22, 23

   2. FORWARD, REVERSE

      part_1_it: 1           2
      part_2_it: 3   2   1   6   5   4
                 11, 12, 13, 21, 22, 23

   3. REVERSE, FORWARD

      part_1_it:         2           1
      part_2_it: 4   5   6   1   2   3
                 11, 12, 13, 21, 22, 23

   4. REVERSE, REVERSE

      part_1_it:         2           1
      part_2_it: 6   5   4   3   2   1
                 11, 12, 13, 21, 22, 23
   ```

   Can in these situation we get out of `end` for the `part_2_it` if it's
   conditional? Because in the worst case it will get to the previous part
   next distinct value, which will always be the last point, because the
   internal iterator it checking with the current value of the previous
   part. It can't go further than another value of the previous part, which
   is what the `end` points to. Or to invalid iterator, but the iterator will
   also be the `end` anyways.

   Let's put it more understandable way: `end` will always point to the point
   where the previous part's value is different than the value in the [`begin`,
   `end`) range. In other words, inbounds of the [`begin`, `end`) range the
   value of the previous part is the same. Out of these bounds is either
   different or does not exist at all (there's no tuples on that side).

   When we do lower/upper bound for the key of the current part we use the
   previous part's value from the given [begin, end) range. If the lower/upper
   bound fail it will get to the invalid iterator or to the place with a
   different previous part value. And can never go further, because if we
   go lower bound and find no relevant values, we get to the bound if the
   first tuple with the current part 2 value, because it is definitely greater
   or equal to the revious part 2 value. And if we go the upper bound and find
   no values, we finish at the next part 2 value, because it is definitely
   greater than our key, which has the current part 2 value.

   The illustration:
   ```
   For a forward iterator:

   1xx 1xx 1xx 2xx 2xx 2xx 3xx 3xx 3xx
               ^           ^
             begin        end

   Let's see behavior of each iterator type fail:
   GT: We wanted to find a 2xx, where xx is greater than in any of the existing
       2xx's. We have failed the `upper_bound` and got to the first 3xx,
       because the given 2xx is greater than any of existing 2xx, but the
       first 3xx is greater than the given 2xx in any case. Here the 3xx is the
       `end`, so the condition is fine.
   GE: We wanted to find a 2xx, where xx is greater or equal to those of any of
       th existing 2xx's. So we've made the `lower_bound` for the given value.
       Since the given value is less than any of the exosting 2xx's we got to
       the last 1xx. This is an unpredicted situation. !!!!!!!!!!!!!!!!!!!!!!!!
   EQ: We wanted to find a value equal to some 2xx, but it does not exist. We
       have failed the `lower_bound` (the keys aren't equal), so we just return.

   For a reverse iterator:

   1xx 1xx 1xx 2xx 2xx 2xx 3xx 3xx 3xx
           ^           ^
          end        begin

   Here: The lower bound of anything starting with 2xx is
   ```

   So.
8. `part_2_it == part_1_it - 1`, so we continuw with `part_1_it`.
9. `part_1_it = part_1_it_next -- {3, 1}`
10. `part_1_it_next = upper_bound({})`

#### Formalization

Let's formalize the algorithm. First, for each key part we are going to have
one iterator. The iterator has its bounds and it iterates over the tuples with
distinct part N values (where N is the 1-based level of the iterator).

Let's say, we have an part 1 iterator. That means it iterates over tuples with
distinct first part. One can visualize it like so:

```
1xx 1xx 1xx 2xx 2xx 2xx 3xx 3xx 3xx
^           ^           ^
|           |           |
+- next() >-+- next() >-+
```

Part 2 iterator would go like this:

```
11x 11x 11x 12x 12x 12x 13x 13x 13x
^           ^           ^
|           |           |
+- next() >-+- next() >-+
```

So it would iterate over tuples with distinct values of part 2.

Each of iterators has its limit. Once it gets to the limit, the iteration
process for the iterator is finished. The bounds depend on the previous
iterator current and next positions, direction of the current iterator and
the difference (or indifference) of the current and the previous iterator
direction.

If we have two key parts with specified iterator types, according to the
iterator direction there're four cases we can have:

1. FORWARD, FORWARD
2. FORWARD, REVERSE
3. REVERSE, FORWARD
4. REVERSE, REVERSE

In case if direction of current and previous iterator are equal (cases 1 and 4)
the range of the current iterator is [`prev_it`, `prev_it_next`).

In case if directions differ, let's see the cases separately.
1. FORWARD, REVERSE: [`prev_it_next - 1`, `prev_it - 1`).
2. REVERSE, FORWARD: [`prev_it_next + 1`, `prev_it + 1`).

So the bounds are swapped and 1 or -1 are added to each border depending on the
current iterator direction: 1 for the forward iterator, -1 for the reversed
one.

One can use some tricky value assignment to create a generic formula. If
`FORWARD` equals 0 and `REVERSE` equals 1, we can calculate
`prev_it_kind - curr_it_kind` and get 0 for cases 1 and 4, -1 for case 2 and 1
for case 3. If we introduce `the_diff = prev_it_kind - curr_it_kind`, then:
1. FORWARD, FORWARD: [`prev_it + the_diff`, `prev_it_next + the_diff`)
2. FORWARD, REVERSE: [`prev_it_next + the_diff`, `prev_it + the_diff`)
3. REVERSE, FORWARD: [`prev_it_next + the_diff`, `prev_it + the_diff`)
4. REVERSE, REVERSE: [`prev_it + the_diff`, `prev_it_next + the_diff`)

And the one can find that if we swap bounds then we have 0 as `the_diff` and
make the formula even more generic:

```C
the_diff = prev_it_kind - curr_it_kind;
begin = (the_diff ? prev_it_next : prev_it) + the_diff;
end = (the_diff ? prev_it : prev_it_next) + the_diff;
```

But this is messy, so further I'll express this with simple conditions.

So the pseudocode of the generic solution is going to be like this:

```Python
def iterate(part, begin, end):
    curr_it = begin
    do:
	curr_key = curr_it.tuple.key.extract_first_n_parts(part.number)
        if part.iterator_direction == forward:
            # 1xx 1xx 1xx 2xx 2xx 2xx
            # +-----------^
            next_it = upper_bound(curr_key) # Or just increment if the part is last.
        else:
            # 1xx 1xx 1xx 2xx 2xx 2xx
            #         ^-----------+
            next_it = lower_bound(curr_key) - 1 # Or just decrement if the part is last.
	if part.next_exists:
            if part.iterator_direction == part.next.iterator_direction:
                iterate(part.next, curr_it, next_it)
            else:
                if part.next.iterator_direction == backward:
                    #   curr_it     next_it
                    #      |           |
                    #  ??? 11x 12x 13x 21x 22x 23x
                    #  \____(<--]____/ range excluding the ???.
                    iterate(part.next, next_it - 1, curr_it - 1)
                else:
                    #      next_it     curr_it
                    #         |           |
                    # 11x 12x 13x 21x 22x 23x ???
                    #             \____[-->)____/ range excluding the ???.
                    iterate(part.next, next_it + 1, curr_it + 1)
        else:
            yield curr_it.tuple
        curr_it = next_it
    while curr_it != end
```

#### Optimization

If we look at the iteration process it turns out that the upper/lower bound
search is only required in certain conditions. Let's say we have ascending
sort order for each of three parts of the key, and then we query the iteration
in order `{'asc', 'desc', 'desc'}`. E. g. the first part sort order is the
same, the second and third part sort order differ.

So we have such state for actual and requested sort order difference:
`{'same', 'different', 'different'}`. As we see, parts wiht different order
requested are sequential. In such case we can omit upper/lower bound search
for the third part.

So the idea is that we only need to locate the next point of iterator if
its part's sort order differentiation differs from previous part's sort order
differentiation. Meaning if the current part sort order is different from
the index sort order for the same part and previous part sort order is also
different from the original index sort order for the part, then we can omit
the next iterator position calculation for this part.

Example: **TBD**.

### Implementation details (old)

First solution doesn't involve any additional data structures so that it deals
with multi-directional iterators only using existing B-tree structure.  
It fact, the first key part can be used to locate the first element as a
candidate in the range to be selected. To illustrate this point let's consider
following Fexample:

```
s:create_index('i', {parts = {{1, 'integer'}, {2, 'integer'}}})`
s:insert({1, 0})
s:insert({1, 0})
s:insert({1, 1})
s:insert({2, 0})
s:insert({2, 1})
i:select({}, {iterator = {'GE', 'LE'}})
```

Result should be:
```
[1, 1]
[1, 0]
[1, 0]
[2, 1]
[2, 0]
```
Note that in case of casual GE iterator (i.e. {GE, GE} in terms of
multi-directional iterators) result is:
```
[1, 0]
[1, 0]
[1, 1]
[2, 0]
[2, 1]
```
As one can see, results are sorted in different orders by second key part,
but in the same order by first key part (not surprisingly). Assume first
element with first key part satisfying search condition is located: {1, 0}.
It can be done by creating GE iterator with {nil, nil} search key. Then let's
find out the first key part with different iterating order - in our case it is
second key part. Since the order is different for that key part, it is required
to locate the upper bound of iteration, i.e. {1, 1} tuple. Searching that
element in BPS tree can be processed with creating auxiliary iterator by
`bps_tree_upper_bound({1, nil})`. After auxiliary iterator positioned to that
tuple (see schema below), we can move it towards main one until they match
(calling `bps_tree_iterator_prev` on auxuliary iterator). All keys between these
iterators satisfy search condition.

```
[1, 0],   [1, 0],   [1, 1],   [2, 0] ... // tuples are arranged as in B-tree
^         ^         ^         ^
|         |         |         |
| <- prev() <- prev() Aux.itr |
|                             |
| Main.itr                    |
+---------- next()----------->|
```
Auxiliary iterator is required for each key part starting from that which
direction is different from one of first key part. Let's consider a bit more
sophisticated example with three key parts:
```
s:create_index('i', {parts = {{1, 'integer'}, {2, 'integer'}, {3, integer}}})`
s:insert({0, 0, 0})
s:insert({0, 1, 3})
s:insert({0, 2, 1})
s:insert({0, 2, 3})
s:insert({0, 2, 4})
s:insert({0, 4, 5})
s:insert({1, 1, 1})
i:select({0, 2, 3}, {iterator = {'GE', 'LE', 'GE'}})
```
```
[0, 0, 0],   [0, 1, 3],   [0, 2, 1],   [0, 2, 3],   [0, 2, 4],   [0, 4, 5],   [1, 1, 1]
^                                      ^                         ^
itrA                                  itrC       next()--->     itrB


```
In this case we have to use three iterators: `iterA` over first key part
(created with key {0, nil, nil}), `iterB` over second key part (i.e. with
fixed first key part - upper bound of {0, 2, nil}) and `iterC` over third
key part - lower bound of {0, 2, 3}. At first, `iterA` is positioned at
{0, 0, 0} tuple, `iterB` - at {0, 4, 5}, `iterC` - at {0, 2, 3}. Moving `iterC`
towards `itrB` (invoking `bps_tree_iterator_next`) gives us first part of keys
in range satisfying search condition: [0, 2, 3] and [0, 2, 4]. Then we move
`itrB` to its second position
(i.e. `bps_tree_lower_bound` + `bps_tree_iterator_prev`):
```
[0, 0, 0],   [0, 1, 3],   [0, 2, 1],   [0, 2, 3],   [0, 2, 4],   [0, 4, 5],   [1, 1, 1]
^            ^             
itrA         itrB
             itrC

```
`ItrC` is positioned at key {`itrA_key`, `itrB_key`, 3} == {0, 1, 3}. Since it
matches with `itrB`, tuple is added to result set. After moving `itrB` to the
next position ({0, 0, 0}) it matches with `itrA`, but tuple does not sotisfy
search criteria, so we can move `itrA` to its next position -
`bps_tree_upper_bound({0, nil, nil})` + `bps_tree_iterator_next()`.  
```
...  [0, 4, 5],   [1, 1, 1] ...
                  ^
                 itrA
```
For EQ iterator type it is possible to simply skip those tuples which doesn't
satisfy equality condition. In turn, it results in necessity to extract part of
key value for all 'EQ' iterator key parts and compare it with reference key
value. This algorithm can be generalized for any number of key parts in index.
In the common case we maintain stack of N iterators: iterator which corresponds
to the last key part is placed on the top; on the bottom - to the first key part.
Depending on the order of iteration (LE/GE/LT/GT) we move auxiliary iterators
towards each other. Displacement of iterator corresponding to k-th part of
key def (out of n parts) results in creating `n-k` auxiliary iterators and
pushing them into the stack. When iterator is popped from stack, we move it to
the position of iterator on the top of stack. When these positions match, we
destroy popped iterator and pop the next iterator from the stack.
Using pseudocode:
```
for (int i = 0; i < key_part_count; ++i)
  if (it->type[i] == ITER_GE) {
    //get_iter_key returns proper iteration key for i-th
    memtx_tree_key_data key = get_itr_key(iter, i);
    //depending on iterator's type we call upper/lower bound
    itr_stack.push(bps_tree_upper_bound(key);
  }
}
while (! iter_stack.isEmpty())
{
  iterator itr = itr_stack.pop();
  while (itr.pos != itr_stack.top().pos) {
    // next() or prev() depending on the iterator's type
    itr->next();
  }
}
```
## Rationale and alternatives

Proposed approach allows to specify any sets of key part iteration orders. It
introduces almost no memory overhead in contrast to alternative solution below.
However, it requires re-creating and positioning auxiliary iterators which is
much slower than normal iteration over BPS-tree elements.

### Alternative approach

Since BPS tree is built without acknowledge in which order keys should be
placed, it is assumed that order is always ascending: keys in blocks are sorted
from smaller to bigger (left-to-right); comparison between keys is made by
`tuple_compare_with_key()` function. It makes given tree unsuitable for
efficient iteration in different orders. On the other hand, it is possible to
build new *temporary in-memory* data structure (e.g. array) featuring correct
key order. It seems to be easy to achieve since order of keys depends on result
of comparator function. Reverting result of comparison for key parts
corresponding to opposite iteration direction gives appropriate keys ordering
in the tree. Note that not all data in space is needed to be in tree (in case
of non-empty search key); only sub-tree making up lower or upper bound of first
key part is required. The main drawback of this approach is that the first tuple
to be selected is probably returned with significant delay. What is more, we got
tree memory construction overhead (but only during iteration routine).
