# RFC Vclock implementation design

* **Status**: in progress
* **Start date**: 16-05-2018
* **Authors**: Ilya Markov @IlyaMarkovMipt \<imarkov@tarantool.org\>
* **Issues**:

## Summary

Overview of possible implementations of vector clocks in large scale replicasets.

## Background and motivation

Vector clocks are used for following states(more exactly LSN) of nodes in replicasets.
Currently, the clocks are implemented with static arrays with size limited by constant `VCLOCK_MAX`
Indices of the array represent replica identifier in replicaset, value is LSN.
In a large scale environment array is far from the best implementation in terms of time and memory consumption.

The main problem here is that within large scale nodes may be added and deleted and the array may contain
 large gaps. So most of memory space might turn out to be useless.

For example, in star topology, one replica has fully filled vclock,
 others have large arrays with only two valuable for them cells.

## Ideas
The new design must address the following requirements:
1. Minimize memory consumption within constantly changing replicaset.
2. Fast vector clock comparison, following taking into account frequent updated nodes.

### Tree
As a possible solution to address the gap problem is to use a tree.
 The tree allocates nodes only for non-empty values. So memory usage in this case is minimized.
 Comparison and vclock following would take O(N), N -size of replicaset.
This time complexity is the same as in implementation with static array but with worse constant.

Though operations like set and get take O(logN) instead constant time in array.
As we can notice vclock_get is highly used with replica ids, which are written in logs.
Under assumption that number of writing replicas is less than the size of replicaset,
the problem with vclock_get may be solved with some fixed size cache in front of tree,
 which will contain frequently replicas lsns.

### Remapping with garbage collecting
Another approach addressing gap problem is shifting replica id to the start of vclock array,
getting rid of gaps.

This idea helps avoiding gaps and simplifies comparison, setting vector clocks.
On the other hand, it requires dedicated calls which follow the state of vclock and shift it, when gaps are found.
Also the shift requires remapping of replica identifiers which also costs something in terms of memory and time consumption.

### Paging
Allocate fixed size arrays for ranges of ids and store references to them in hash/tree index.
For example, we have several ranges of ids: 1-10, 65-100. Let's assume size of each array is 32.
For this set, there would 3 ranges: 1-32, 65-96, 97-128. The index would contain 3 records, which could be get by 1, 65, 97 respectively.

In this approach gaps are limited to the certain size, there is no need in shifting.
Copying and comparison are almost the same as in approach with static size array.

### Skip-lists
One more possible solution to gap problem may be lists.
But, as we need to index sometimes, we can use skip-lists, which in terms of time complexity of indexing are almost the same as trees.
Moreover, traversing lists is faster than trees.

Bad side of the idea is that it consumes memory excessively.

## Conclusion
The most easiest to implement solution is a tree. Nevertheless, it needs an optimizations for vclock_get.

The paging looks like an approach which solves the current problem with gaps and doesn't create new problems or complexities.

The shifting with remapping looks the worst one to my mind, mostly because of its difficulty
and generating new maintaining processes(e.g remapping) and, therefore, new possible problems.

Skip-lists are just one of variations of trees, but with extra memory consumption.
