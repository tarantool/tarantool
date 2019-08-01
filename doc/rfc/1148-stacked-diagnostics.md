# Stacked Diagnostics

* **Status**: In progress
* **Start date**: 30-07-2019
* **Authors**: Kirill Shcherbatov @kshcherbatov kshcherbatov@tarantool.org,
               Pettik Nikita @Korablev77 korablev@tarantool.org
* **Issues**: [#1148](https://github.com/tarantool/<repository\>/issues/1148)


## Summary

The document describes a stacked diagnostics feature. It is needed for the cases,
when there is a complex and huge call stack with variety of subsystems in it.
It may turn out that the most low-level error is not descriptive enough. In turn
user may want to be able to look at errors along the whole call stack from the
place where a first error happened. In terms of implementation single fiber's
error object is going to be replaced with a list of objects forming diagnostic
stack. Its first element will always be created by the deepest and the most
basic error. Both C and Lua APIs are extended to support adding errors in stack.

## Background and motivation

Support stacked diagnostics for Tarantool allows to accumulate all errors
occurred during request processing. It allows to better understand what
happened, and handle errors appropriately. Consider following example:
persistent Lua function referenced by functional index has a bug in it's
definition, Lua handler sets an diag message. Then functional index extractor
code setups an own, more specialized error.

### Current error diagnostics

Currently Tarantool has `diag_set()` mechanism to set a diagnostic error.
Object representing error featuring following properties:
 - type (string) error’s C++ class;
 - code (number) error’s number;
 - message (string) error’s message;
 - file (string) Tarantool source file;
 - line (number) line number in the Tarantool source file.

The last error raised is exported with `box.error.last()` function.

Type of error is represented by a few C++ classes (all are inherited from
Exception class). For instance hierarchy for ClientError is following:
```
ClientError
 | LoggedError
 | AccessDeniedError
 | UnsupportedIndexFeature
```

All codes and names of ClientError class are available in box.error.
User is able to create a new error instance of predefined type using
box.error.new() function. For example:
```
tarantool> t = box.error.new(box.error.CREATE_SPACE, "myspace", "just cause")
tarantool> t:unpack()
---
- type: ClientError
  code: 9
  message: 'Failed to create space ''myspace'': just cause'
  trace:
  - file: '[string "t = box.error.new(box.error.CREATE_SPACE, "my..."]'
    line: 1
```

User is also capable of defining own errors with any code  by means of:
```
box.error.new({code = user_code, reason = user_error_msg})
```
For instance:
```
e = box.error.new({code = 500, reason = 'just cause'})
```

Error cdata object has `:unpack()`, `:raise()`, `:match(...)`, `:__serialize()`
methods and `.type`, `.message` and `.trace` fields.

## Proposal

In some cases a diagnostic area should be more complicated than
one last raised error to provide decent information concerning incident (see
motivating example above). Without stacked diagnostic area, only last error is
delivered to user. One way to deal with this problem is to introduce stack
accumulating all errors happened during request processing.

### C API

Let's keep existent `diag_set()` method as is. It is supposed to replace the
last error in diagnostic area with a new one. To add new error at the top of
existing one, let's introduce new method `diag_add()`. It is assumed to keep
an existent error message in diagnostic area (if any) and sets it as a previous
error for a recently-constructed error object. Note that `diag_set()` is not
going to preserve pointer to previous error which is held in error to be
substituted. To illustrate last point consider example:

```
0. Errors: <NULL>
1. diag_set(code = 1)
Errors: <e1(code = 1) -> NULL>
2. diag_add(code = 2)
Errors: <e1(code = 1) -> e2(code = 2) -> NULL>
3. diag_set(code = 3)
Errors: <e3(code = 3) -> NULL>
```

Hence, developer takes responsibility of placing `diag_set()` where the most
basic error should be raised. For instance, if during request processing
`diag_add()` is called before `diag_set()` then it will result in inheritance
of all errors from previous error raise:

```
-- Processing of request #1
1. diag_set(code = 1)
Errors: <e1(code = 1) -> NULL>
2. diag_add(code = 2)
Errors: <e1(code = 1) -> e2(code = 2) -> NULL>
-- End of execution

-- Processing of request #2
1. diag_add(code = 1)
Errors: <e1(code = 1) -> e2(code = 2) -> e3(code = 1) -> NULL>
-- End of execution
```

As a result, at the end of execution of second request, three errors in
stack are reported instead of one.

Another way to resolve this issue is to erase diagnostic area before
request processing. However, it breaks current user-visible behaviour
since box.error.last() will preserve last occurred error only until execution
of the next request.

The diagnostic area (now) contains (nothing but) pointer to the top error:
```
struct diag {
  struct error *last;
};

```

To organize errors in a list let's extend error structure with pointer to
the previous element. Or alternatively, add member of any data structure
providing list properties (struct rlist, struct stailq or whatever):
```
struct diag {
  struct stailq *errors;
};

struct error {
   ...
   struct stailq_entry *in_errors;
};
```
When error is set to diagnostics area, its reference counter is incremented;
on the other hand if error is added (i.e. linked to the head of diagnostics
area list), its reference counter remains unchanged. The same concerns linking
two errors: only counter of referenced error is incremented. During error
destruction (that is the moment when error's reference counter hits 0 value)
the next error in the list (if any) is also affected: its reference counter
is decremented as well.

### Lua API

Tarantool returns a last-set (diag::last) error as `cdata` object from central
diagnostic area to Lua in case of error. User should be unable to modify it
(since it is considered to be a bad practice - in fact object doesn't belong
to user). On the other hand, user needs an ability to inspect a collected
diagnostic information. Hence, let's extend the error object API with a method
which provides the way to get the previous error (reason): `:prev()` (and
correspondingly `.prev` field).

```
-- Return a reason error object for given error object 'e'
-- (when exists, nil otherwise).
e:prev(error) == error.prev
```

Furthermore, let's extend signature of `box.error.new()` with new (optional)
argument - 'prev' - previous error object:

```
e1 = box.error.new({code = 111, reason = "just cause"})
e2 = box.error.new({code = 222, reason = "just cause x2", prev = e1})
```

User may want to link already existing errors. To achieve this let's add
`set_prev` method to error object so that one can join two errors:
```
e1 = box.error.new({code = 111, reason = "just cause"})
e2 = box.error.new({code = 222, reason = "just cause x2"})
...
e2.set_prev(e1) -- e2.prev == e1
```
### Binary protocol

Currently errors are sent as `(IPROTO_ERROR | errcode)` response with an
string message containing error details as a payload. There are not so many
options to extend current protocol wihtout breaking backward compatibility
(which is obviously one of implementation requirements). One way is to extend
existent binary protocol with a new key IPROTO_ERROR_STACK (or
IPROTO_ERROR_REASON or simply IPROTO_ERROR_V2):
```
{
        // backward compatibility
        IPROTO_ERROR: "the most recent error message",
        // modern error message
        IPROTO_ERROR_STACK: {
                {
                        // the most recent error object
                        IPROTO_ERROR_CODE: error_code_number,
                        IPROTO_ERROR_MESSAGE: error_message_string,
                },
                ...
                {
                        // the oldest (reason) error object
                },
        }
}
```

IPROTO_ERROR is always sent (as in older versions) in case of error.
IPROTO_ERROR_STACK is presented in response only if there's at least two
elements in diagnostic list. Map which contains error stack can be optimized
in terms of space, so that avoid including error which is already encoded
in IPROTO_ERROR.
