## feature/memtx

* Improved performance of tree index methods: `select()` with the `offset`
  specified and `count()` method. The underlying algorithm for these methods is
  changed: the old one's time complexity was `O(n)`, where `n` is the value of
  `offset` or the amount of counted tuples. The new algorithm's complexity is
  `O(log(size))`, where `size` is the number of tuples stored in the index. Now
  it does not depend on the `offset` value or the amount of tuples to count. It
  is safe to use these functions with arbitrary big offset values and tuple
  count (gh-8204).
