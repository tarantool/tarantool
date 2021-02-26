# Inter-fiber Debugger for Tarantool
* **Status**: In progress
* **Start date**: 20-01-2021
* **Authors**: Sergey Ostanevich @sergos sergos@tarantool.org,
               Igor Munkin @igormunkin imun@tarantool.org
* **Discussion**: [#5857](https://github.com/tarantool/tarantool/discussions/5857)

### Rationale

To make Tarantool platform developer-friendly we should provide a set of basic
developer tools. One of such tools is debugger. There are number of debuggers
available for the Lua environments, although all of them are missing the
critical feature needed for the Tarantool platform: they should not cause a
full-stop of the debugged program during the debug session.

In this RFC I propose to overcome the problem with a solution that will stop
only the fiber to be debugged. It will allow developers to debug their
application, while Tarantool can keep processing requests, perform replication
and so on.

### Approach

To do not reinvent the debugger techniques we may borrow the already existent
Lua debugger, put the rules about fiber use, data manipulation tweaks and so
on.

Every fiber can be considered as a "debuggee" or a regular fiber, switching
from one state to the other. To control the status we can either patch fiber
machinery - which seems excessive as fibers can serve pure C tasks - or tweak
the breakpoint hook to employ the fiber yield. The fiber will appear in a state
it waits for commands from the debugger and set the LuaJIT machinery hooks to
be prepared for the next fiber to be scheduled.

### Debug techniques

Regular debuggers provide interruption for all threads at once hence they don't
distinguish breakpoints appearance across the threads - they just stop
execution. For our case we have to introduce some specifics so that debugger
will align with the fiber nature of the server behavior. Let's consider some
techniques we can propose to the user.

#### 1) Break first fiber met

User puts a breakpoint that triggers once, stopping the first fiber the break
happens in. After breakpoint is met the fiber reports its status to the
debugger server, put itself in a wait state, clears the breakpoint and yields.
As soon as server issue a command, the debuggee will reset the breakpoint,
handle the command and proceed with execution or yield again.

#### 2) Regular breakpoint

This mode will start the same way as previous mode, but keep the breakpoint
before yield, so that the breakpoint still can trigger in another fiber. As the
server may deliver huge number of fibers during its performance, we have to set
up a user-configurable limit for the number of debuggee fibers can be set at
once. As soon as limit is reached the debuggee fiber starts behave exactly as
in previous mode, clearing the breakpoint before the yield from the debuggee.

#### 3) Run a function under debug session

This is the most straightforward way to debug a function: perform a call
through the debug interface. A new fiber will be created and break will appear
at the function entrance. The limit of debuggee fibers should be increased and
the fiber will behave similar to the modes above.

#### 4) Attach debugger to a fiber by ID

Every fiber has its numerical ID, so debugger can provide interface to start
debugging for a particular fiber. The fiber will be put in a wait state as soon
as it starts execution after the debugger is attached.

### Basic mechanism

The Tarantool side of the debugger will consist of a dedicated fiber named
`DebugSRV` that will handle requests from the developer and make bookkeeping of
debuggee fibers and their breakpoints and a Lua function `DebugHook` is set as
a hook in Lua [debug library](https://www.lua.org/pil/23.html). Users should
not use this hook for the period of debugging to avoid interference. The
external interface can be organized over arbitrary protocol, be it a socket
connection, console or even IPROTO (using `IPROTO_CALL`).

Debuggee fiber will be controlled by a debug hook function named `DebugHook`.
It is responsibility of the `DebugHook` to set the debuggee fiber status, check
the breakpoints appearance, its condition including the ignore count and update
`hit_count`. As soon as breakpoint is met, the `DebugHook` has to put its state
to pending and wait for command from the `DebugSRV`.

Communication between `DebugSRV` and the debuggee fiber can be done via
`fiber.channel` mechanism. It will simplify the wait-for semantics.

All interfaces for debugging activities are enclosed in `tdb` built-in module.

#### Data structure

Every debuggee fiber is present in the corresponding table in the `DebugSRV`
fiber. The table has the following format:

```
debuggees = {
    debugsrv_id = <fiber_id>,
    max_debuggees = <number>,
    preserved_hook = {
        [1] = <function>,
        [2] = <type>,
        [3] = <number>,
    }
    fibers = {
        [<fiber_id>] = {
            state = ['pending'|'operation'],
            current_breakpoint = <breakpoint_id>,
            channel = <fiber.channel>,
            breakpoints = {
                [<breakpoint_id>] = {
                    type = ['l'|'c'|'r'|'i'],
                    value = [<number>|<string>]
                    condition = <function>,
                    hit_count = <number>,
                    ignore_count = <number>,
                }
            }
        }
    }
    global_breakpoints = {
        [<breakpoint_id>] = {
            type = ['l'|'c'|'r'|'i'],
            value = [<number|string>]
            condition = <function>,
            hit_count = <number>,
            ignore_count = <number>,
    }
}
```
As `DebugSRV` receives commands it updates the structure of the debuggees and
forces the fiber wakeup to reset it's hook state. The state of the debuggee is
one of the following:

- `'operation'`: the fiber is already in the `debuggees.fibers` list, but it
  issued yield without any breakpoint met
- `'pending'`: `DebugHook` waits for a new command from the channel in the
  `debuggees.fibers` of its own ID

#### DebugHook behavior

For the techniques 3) and 4) fiber appears in the list of `debuggees.fibers`
first, with its status set as `'operation'` with a list of breakpoints set.

For the techniques 1) and 2) there is a list of `global_breakpoints` that
should be checked by every fiber.

In case a fiber receives control from the debug machinery and its ID matches
`DebugSRV` one it should leave the hook immediately. Otherwise it should check
if it is present in `debuggees.fibers`. If it is - it should check if its
current position meets any breakpoint from the
`debuggees.fibers[<fiber_id>].breakpoints` or `debuggees.global_breakponts`. If
breakpoint is met, the fiber sets its state into `'pending'` and waits for a
command from the `debuggees.fibers[<fiber_id>].channel`.

In case a fiber is neither `DebugSRV` one nor present in the `debuggees.fibers`
it should check that the number of fibers entries in the `debuggees` structure
is less than `max_debuggees`. In such a case it checks if it met any of the
`global_breakpoints` and put itself into the fibers list, updating the
[array size](https://www.lua.org/pil/19.1.html). Also it should open a channel
to the `DebugSRV` and put itself into the `'pending'` state.

#### DebugSRV behavior

DebugSRV handles the input from the user and supports the following list of
commands (as mentioned, it can be used from any interface, so commands are
function calls for general case):

- `tdb.break.info([<fiber_id>])` - list all breakpoints with counts and
  conditions, limits output for the fiber with `<fiber_id>`
- `tdb.break.cond(<breakpoint_id>, <condition>)` - set a condition for the
  breakpoint, condition should be Lua code evaluating into a boolean value
- `tdb.break.ignore(<breakpoint_id>, <count>)` - ignore the number of
  breakpoint executions
- `tdb.break.delete(<breakpoint_id>)` - removes a breakpoint
- `tdb.step(<fiber_id>)` - continue execution, stepping into the call
- `tdb.stepover(<fiber_id>)` - continue execution until the next source line,
  skip calls
- `tdb.stepout(<fiber_id>)` - continue execution until return from the current
  function
- `tdb.continue(<fiber_id>)` - continue execution until next breakpoint is met
  or debug session is stopped

The functions above are common for many debuggers, just some tweaks to adopt
fibers. Functions below are more specific, so let's get into some details:

- `tdb.set_max_debuggees(<number>)` - set the number of fibers can be debugged
  simultaneously. It modifies the `debuggees.max_debuggees` so that new fibers
  will respect the amount of debuggees. For example, if at some point of
  debugging there were 5 debuggee fibers user can set this value to 3 - it
  will not cause any problem, just a new fiber will not become a debuggee if it
  meet some global breakpoint.
- `tdb.eval(<fiber_id>, <code>)` - allows to evaluate the code in the context
  of the debuggee fiber if it is in `'pending'` mode. User can issue a
  `debug_eval(113, function() return fiber.id() end)` to receive 113 as a
  result
- `tdb.break.create(<breakpoint description>, [<fiber_id>])` - add a new
  breakpoint in the fiber's breakpoint list or in the global list if no fiber
  ID provided
- `tdb.start()` - starts debug session: creates debuggees structure, preserve
  current debug hook in `debuggees.preserved_hook` and sets `DebugHook` as the
  current hook
- `tdb.stop()` - quits debug session: resets the debug hook, clears debuggees
  structure
