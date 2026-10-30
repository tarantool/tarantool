# VyLog: ups and downs

* **Status**: In progress
* **Start date**: 08-06-2019
* **Authors**: Vladimir Davydov @locker vdavydov.dev@gmail.com
* **Issues**: N/A

# Summary

VyLog, also known as Vinyl metadata log, is the infrastructure that is needed
to match Vinyl data files (\*.run)  to Tarantool indexes so that they can be
safely recovered after restart. This document says a few words about how we
have come to the conclusion that we need such an entity, briefly describes
VyLog implementation, mentions some problems we are facing now because of it,
and proposes a few possible ways to deal with them.

# Origins

Vinyl data files are stored under the directory specified by box.cfg.vinyl\_dir
configuration option. Within this directory there are sub-directories, one for
each index, named space\_id/index\_id (e.g. 512/0), that store \*.run and
\*.index files: \*.run files contain rows sorted by the index key while
\*.index files contain information necessary to access the rows quickly (page
index, bloom filters).  Both kinds of files have the same format as xlog and
snap files. The file names have format %020d.run and %020d.index where the
number (%d) grows incrementally and is the same for related \*.run and \*.index
files.

Initially, we recovered data files simply by scanning the directory and adding
all found files to the index, just like RocksDB and Cassandra do. However, over
time the complexity of such a solution had grown to insurmountable levels,
because in contrast to the aforementioned database engines, even in its crib
Vinyl had much more complex structure as it splits files into key ranges. With
the appearance of the shared memory level and the concept of slices it turned
out to be practically impossible to recover an index looking solely at its file
directory. There were other problems, too. For instance, how to detect a
situation when we failed to remove a data file due to a system error? The file
is not needed, but it's still in the directory and hence will be read by the
recovery procedure.

To address the recovery issue, it was initially suggested to store vinyl
metadata information in a system space, but an attempt to implement that failed
for a number of reasons described further in the test. Eventually, we had come
to an agreement that the best we could do at the time was to store vinyl
metadata in its own \*.vylog file.

# Internals

## File format and basic idea

\*.vylog files have the same format as \*.xlog files. Similarly to \*.xlog
files, they store operations done on Vinyl indexes and files, such as "create
index", "drop index", "create key range", "prepare run file", "commit run
file". Each operation has parameters that identify the objects it deals with.
For instance for an operations that adds a key range to an LSM tree, we need to
specify which key range and which LSM tree to use. Objects are identified by
unique integer identifiers.

On recovery we first read the last \*.vylog file and build a vy\_recovery
structure which stores information about all LSM trees, their key ranges, and
run files. The structure is used during recovery to build vy\_lsm objects:
whenever we encounter an index creation record in \*.snap or \*.xlog file, we
look up the index in the vy\_recovery structure to restore the corresponding
vy\_log object.

## Relationship with checkpoints

When a checkpoint is created, the current vylog file is closed, its content is
read,"compressed", meaning that operations annihilating each other, such as
"create run" and "drop run" are discarded, and written to a new vylog file,
which is then used for writing until a new checkpoint is created. So \*.vylog
files are named after checkpoints (\*.snap) files. In contrast to \*.xlog
files, we don't have separate \*.vysnap files - both checkpoint data and
records written after the checkpoint are stored in the same \*.vylog file for
simplicity. We have a special "snapshot" record written to the \*.vylog right
after a checkpoint is created so that we can replay a \*.vylog file until a
checkpoint if necessary.

The close association with checkpoints stems from the necessity to send the
consistent view of the last checkpoint to a replica on initial join. It is also
used for backups - we only allow to backup files corresponding to a particular
checkpoint.

## Association with indexes

As it was mentioned earlier, when we encounter an xlog record creating an index
on recovery, we look up the corresponding LSM tree object in the vy\_recovery
structure loaded from the most recent \*.vylog file. Initially, we used
space\_id/index\_id pair to identify indexes, but the problem is an index can
be dropped and then recreated with the same space\_id/index\_id. That is, we
needed a way to differentiate different incarnations of the same index object.
So we started assigning a unique identifier to each index object and store it
in a persistent manner in the index options (in \_index system space). It was
decided not to introduce a new counter, but use existing box.info.signature for
that. Everything worked out fine until we realized there was an inherent race
condition in that decision.

The problem is two fibers may create Vinyl indexes concurrently. Since the
index creation operation yields, it may occur that both of them use the same
value of box.info.signature for their indexes, thus breaking recovery. One way
to fix that was ditch box.info.signature and start using a dedicated counter
which would be incremented before any index creation operation (like
auto-increment field). However, the decision to stick to xlog signatures
prevailed so instead we decided to use the signature associated with the xlog
operation by the WAL. Since they are guaranteed to be unique (no two xlog rows
are allowed to have the same signature), no race is possible.

## DDL operations

Since VyLog stores information about all Vinyl indexes out there, we have to
log index creation and drop events in it, too. The difficulty associated with
this is that those events are generated from commit triggers, after the
corresponding operation have been written to WAL, when we are not allowed to
fail or yield. So we submit those events to VyLog buffer without waiting for
them to complete (fire-and-forget). If a write fails, VyLog will retry to flush
those records along with the next write. If it fails to flush those records to
disk before restart, we will have to detect this on recovery and restore those
records on the fly, which greatly complicates the recovery procedure, see all
this juggling with lsn in vy\_lsm\_recover.

# What went wrong

All VyLog issues can be divided into two sub-categories:

 1. As explained above, \*.vylog files are rotated along with checkpoints.
    Besides, we use vclock signatures to uniquely identify LSM trees in VyLog.
    This close relationship with checkpoints and xlogs forces us to add hacks
    to the engine-independent code from time to time. The last hack was added
    along with transactional DDL when we had to patch alter.cc to pass correct
    signatures to Vinyl.

 2. Another issue with VyLog is its complexity, which makes it difficult to
    understand let alone modify its code. The two most notable examples are:
    "fire-and-forget" requests issued from commit triggers to log index
    creation and drop operations; matching different incarnations of the same
    index to vy\_lsm objects on recovery using vclock signatures.

Arguably, the first sub-category lists more serious issues than the second one.

# What we can do about it

This section presents a few ways of alleviating the issues outlined in the
previous section.

## Incorporate VyLog into xlog

The idea is to store information about Vinyl data files in memtx, similarly to
how we store meta information in \_space, \_index, and other spaces. The
biggest challenge here is to preserve backward compatibility as the old and the
new approaches would have to co-exist for some time, at least as long as the
recovery procedure concerns. Putting that aside, we can write down the pros and
cons of this approach.

Pros:

 - The idea of storing all the meta information in system spaces instead of
   "re-inventing the wheel" in the shape of VyLog does indeed look tempting and
   elegant.

 - This approach would ease introspection as one could simply look through
   these spaces to figure out what's going on with Vinyl internals.

 - It would also allow us to simplify Vinyl internals (i.e. deal with the
   second sub-category of issues presented in the previous section). For
   example, vylog-vs-xlog sync would come for free naturally and we wouldn't
   need to juggle with vclock signatures to achieve that.

 - Apparently, there wouldn't be any \*.vylog files should we take this path.
   The fewer kinds of files we have, the better, obviously.

Cons:

 - We would have to rethink the concept of \*.snap files. Currently, they are
   equivalent to memtx checkpoints, i.e. they store a consistent view of the
   database at the moment a checkpoint was taken. If Vinyl metadata were stored
   in memtx, we would have to log creation of run files that store checkpoint
   data after we start writing a \*.snap file. This means that to get to a
   checkpoint, we would have to not only read the corresponding \*.snap file,
   but also apply a chunk of the next \*.xlog file. This was a show stopper
   when we considered this approach in the first place.

 - We would probably have to scan the xlog file twice on recovery, because we
   need to recover Vinyl metadata before we start replaying DML records on
   Vinyl spaces. Rationale: we want to skip records that have already been
   dumped to disk. If we don't scan the xlog file twice, we can either apply
   all records, which would slow down the recovery procedure, or check the
   filesystem to figure out if a run we are about to create on recovery was
   dumped to disk before restart. Checking the filesystem neglects the whole
   idea of logging so we wind up either with slow recovery or scanning xlogs
   twice. Both ways are ugly.

 - It's unclear what to do with rebootstrap. It's a procedure that allows a
   replica to initiate bootstrap from a remote master in case it detects that
   the master has removed the xlogs it is subscribed to. During rebootstrap we
   don't write xlogs, but we may write Vinyl files and hence we may log
   something to VyLog. We discard the data from VyLog if reboostrap fails. If
   we switched completely to xlog, we'd have to augment the xlog with ability
   to discard certain records. Besides, we'd probably have to skip all DML
   records during rebootstrap and only write DDL and Vinyl-related data to
   xlogs. Otherwise, we'd somewhat slow-down rebootstrap procedure.

Overall, I don't like this approach. Even if we close our eyes at the
complexity of its implementation and the necessity to support the old approach
for an unknown period of time, there's a much more serious problem inherent to
it. While this approach does simplify the Vinyl metadata logging in general, it
ties Vinyl metadata with generic concepts, such as checkpointing and signatures
even more aggressively than the current VyLog implementation. This means that
should we change anything in either of the sub-systems, we'd have to think how
it'll reflect upon the other one.

## Relax VyLog dependency of checkpoints and xlogs

As the worst problems come from the fact that VyLog is too tightly connected
with checkpoints and xlogs, we could try to get rid of this dependency
altogether.

The first thing we need to do in this direction is to get rid of the vclock
signature propagation to VyLog where it is then used to differentiate various
incarnations of the same index. As mentioned above, we could instead store a
unique Vinyl id right in index options (in \_index space). The id would be
generated by applying an auto-increment operation on a system space (\_schema,
similarly to space\_id) and hence would be known before an index is even
created. As a result, we wouldn't have to pass the signature of the index
creation operation to VyLog on commit.

The only problem here is transactional DDL: index build operation must always
go first in a transaction, as it may yield while in order to build a new index
we would need to get a unique id from another system space (\_schema). It isn't
quite clear how to solve this problem. Maybe, we could reuse the same approach
that is already used by sequences, i.e. skip generation of a row for such
auto-increment operations, or perhaps we could simply generate UUIDs to uniquely
identify indexes in engines.

The second step on the way of making VyLog an independent entity is untying it
log rotation from checkpointing. To achieve that, we have to reimplement
initial join and backups in such a way that they don't require the consistent
view of the last checkpoint (currently both operations can only deal with
checkpoints).

In case of initial join, we could open an iterator over a space instead of
using the last snapshot. In fact, that's how it was initially implemented.
True, it would result in accumulation of garbage on the master while initial
join is in progress. On the other hand, garbage is accumulated anyway, because
we have to pin the \*.run files corresponding to the last checkpoint.

In case of backup we would have to make box.backup() work on the current
database state, i.e. consistently copy the current Vinyl state, the last
\*.snap file and all following \*.xlog files. This seems doable and reasonable
as the limitation that states that one may only create a backup of a checkpoint
seems to be artificial. After all, the option to store a few last checkpoints
(\*.snap files) was dictated by the absence of backups. I am inclined to think
that we should only keep the last \*.snap file and the actual state of a Vinyl
database (i.e. no \*.run files should be pinned by previous checkpoints).

## Make VyLog per index

On our way of untying VyLog from checkpointing and xlogs we could go even
further and maintain an own VyLog in each index directory. This would simplify
the VyLog implementation as we wouldn't have to deal with DDL operations: upon
creation of an index we would simply create a new directory without logging
anything. On recovery we would open the VyLog file stored in the directory and
load the index. Just like with the previous approach we would store the link to
the index directory and/or VyLog file right in index options (\_index system
space).

This way VyLog files would only store information about \*.run files and their
relationship with key ranges. We wouldn't have to log anything from commit
triggers and hence we wouldn't have to implement "fire-and-forget" like
operations. This would also make the whole system more robust, because it would
eliminate the single point of failure - if a VyLog file of an index was
corrupted, we could still recover other indexes.

The downside of this approach is that we would have to scan the Vinyl directory
to find and delete orphan indexes. An index can become an orphan in case the
instance crashes while performing an index creation operation. In this case we
need to delete files written by the incomplete operation on recovery.
Currently, we find such orphan indexes by scanning the global VyLog file. If
there was no global VyLog file, the only option would be scanning the directory
where indexes are stored.
