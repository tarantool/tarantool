# Foreign Keys

* **Status**: In progress
* **Start date**: 27-06-2018
* **Authors**: Nikita Pettik @korablev77 korablev@tarantool.org
* **Issues**: [#3271](https://github.com/tarantool/tarantool/issues/3271)

## Summary

Introduce Foreign Keys (FK) constraints in server.

## Background and motivation

Used terminology:
```
CREATE TABLE t1 (id1 INT PRIMARY KEY, a INT);  
CREATE TABLE t2 (id2 INT PRIMARY KEY REFERENECES t1(id1));
```
t1 - parent or referenced table; id1 - referenced columns;  
t2 - child or referencing table; id2 - referencing columns;

Originally, SQLite provides quite specific way of handling FK constraints:
any checks are deferred as much as possible. For instance, existence of
parent table's existence is verified each time on data manipulation in
child space, but not checked on creation of FK constraint itself.
The same is for UNIQUE index: according to ANSI referenced columns must
make up UNIQUE constraint; otherwise error is raised. But in SQLite
presence of such index is tested only when child table is involved in DML.
On the other hand it allows us to get rid of checks on indexes/spaces
destroying; moreover, it allows us to avoid storing additional dependencies
in internal data dictionary holding only space name and extracting fresh
pointers to DD by lookup in hash.

To be closer to ANSI FK constraints, it was suggested to rework and
move them to server DD.

## Implementation details

1. Prohibit actions with FK constraints which strictly contradict ANSI:
 - Ban opportunity to create FK constraint on table which doesn't exist
   (except for self-references). Since SQLite allows creating
   FK constraints only within <CREATE TABLE> declaration (i.e. there is
   no separate <ALTER TABLE ADD CONSTRAINT> statement), this step will
   deprive us of ability to create circular FK dependencies.
 - Ban opportunity to drop parent table even if it doesn't violate FK
   constraints. Child table must always be dropped before parent.
 - Ban opportunity to create FK constraints on VIEW.

2. To return ability to create circular FK dependecies, we are going to
   introduce <ALTER TABLE ADD CONSTRAINT> statement. It compels us to
   store somehow created constraints. Hence, new system space to
   persist FK constraints is added:
```
 - [350, 1, '_constraint', 'memtx', 0, {}, [{'name': 'name', 'type': 'string'}, {
      'name': 'parent_id', 'type': 'unsigned'}, {'name': 'child_id', 'type': 'unsigned'},
     {'name': 'deferred', 'type': 'boolean'}, {'name': 'match', 'type': 'string'},
     {'name': 'on_delete', 'type': 'string'}, {'name': 'on_update', 'type': 'string'},
     {'name': 'links', 'type': 'map'}]]
```
In other words, FK constraint is completely described by:
 - Its name;
 - Referenced space;
 - Child space;
 - Time of resolution (deferred until the end of transaction or not);
 - Match clause;
 - ON DELETE and ON UPDATE actions;
 - Column links;

3. Insertion on _constraint space leads to creation of FK constraints.
   FK constraints in DD are represented as following structs:
```
struct fkey_def {
       /** id of child space. */
       uint32_t child_id;
       /** id of parent space. */
       uint32_t parent_id;
       /** Number of fields (links) in this key. */
       uint32_t field_count;
       /** True if constraint checking is deferred till COMMIT. */
       bool is_deferred;
       /** Match condition for foreign key. SIMPLE by default. */
       enum fkey_match match;
       /** ON DELETE action. */
       enum fkey_action on_delete;
       /** ON UPDATE action. */
       enum fkey_action on_update;
       /** Mapping of fields in child to fields in parent. */
       struct field_link *links;
       /** Name of the constraint. */
       char name[0];
};

struct fkey {
       /** Space containing the REFERENCES clause (aka: child). */
       struct space *child;
       /** Space that the key points to (aka: parent). */
       struct space *parent;
       /** Number of fields (links) in this key. */
       uint32_t field_count;
       /** True if constraint checking is deferred till COMMIT. */
       bool is_deferred;
       /** Match condition for foreign key. SIMPLE by default. */
       enum fkey_match match;
       /** Triggers for actions. */
       struct Trigger *on_delete_trigger;
       struct Trigger *on_update_trigger;
       /** Contraints are orginized into list. */
       struct fkey *fkey_next;
       /** Mapping of fields in child to fields in parent. */
       struct field_link *links;
       /** Name of the constraint. */
       char name[0];
};
```

struct fkey_def is used only during creation of struct fkey.
Foreign keys will be stored in struct space as two linked lists:

```
@@ -182,7 +182,8 @@ struct space {
         */
        struct index **index;
        /** Foreign key constraints. */
+       struct fkey *parent_fkey;
+       struct fkey *child_fkey;
```

on_replace_dd_constraint() would create definition of FK constrain
from tuple; from def construct struct fkey, and finally add it
to parent's list of FK constraints and to child's one.
In case of WAL fail, simply remove it from those lists.
On drop of FK actions are inverted: FK constraint is removed from
parent and child lists and if WAL fails, it will be returned back.

4. Alongside with insertion to _constraint, we must check that tuples
   which are already in child table don't violate FK constraint under
   construction. There several approaches to resolve this problem.
   The first and common one is to handle this routine within
   on_replace_dd_constraint trigger: create iterators for parent and
   child spaces and for each tuple from child space find appropriate
   tuple with given FK key in parent space. The question here is how
   to process inserted or deleted tuples until FK constraint is committed?
   Another solution is to temporary execute this check only if creation
   of FK consraint is occurred via SQL: before insertion to _constraint
   run VDBE program which will test this condition. Anyway, now it is
   impossible to resolve FK constraints (in common sense) without
   involving VDBE: we can't run ON DELETE and ON UPDATE triggers directly
   from server. The last and the easiest way is to allow creation of
   FK constraint only for empty spaces and remove this restriction when
   we will be able to run any VDBE code from server.

5. Introduce SQL statement to create and drop FK constraints (ANSI syntax):

```
ALTER TABLE <referencing table> ADD CONSTRAINT
  <referential constraint name> FOREIGN KEY
  <left parent> <referencing columns> <right paren> REFERENCES
  <referenced table> [ <referenced columns> ] [ MATCH <match type> ]
  [ <referential triggered action> ] [ <constraint check time> ]

ALTER TABLE <referencing table> DROP CONSTRAINT <referential constrain name>
```
In terms of our SQL parser:

```
cmd ::= ALTER TABLE fullname(X) ADD CONSTRAINT nm(Z) FOREIGN KEY
        LP eidlist(FA) RP REFERENCES nm(T) eidlist_opt(TA) refargs(R)
        defer_subclause_opt(D).

cmd ::= ALTER TABLE fullname(X) DROP CONSTRAINT nm(Z).
```
Example:
```
ALTER TABLE t1 ADD CONSTRAINT f1 FOREIGN KEY(id, a) REFERENCES t2 (id, b) MATCH FULL;
ALTER TABLE t1 DROP CONSTRAINT f1;
```
These statements are going to emit VDBE code to construct appropriate
tuple and insert it into _constraint space (and maybe run VDBE program
to make sure that all tuples in child space satisfy new FK constraint).
The rest of routine will be handled by on_replace_dd_constraint() trigger.

6. Refactor SQL routine to operate on new struct fkey instead of
   obsolete SQLite struct FKey.

## Open questions

Should we resolve fields and indexes right after insertion to
_constraint (as it happens in other DBs) or defer it until usage of FK
(as it occurs in SQLite)? If we chose first way, we would have to
implicitly link index to particular index and ban ability to drop such
index until all FK constraints are dropped. Such behaviour may be not
so obvious for users, but it is used for instance in PostgreSQL and MySQL.