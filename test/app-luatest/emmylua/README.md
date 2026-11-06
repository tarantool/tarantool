# EmmyLua annotation tests

This directory contains fixtures for the EmmyLua annotation test
in `test/app-luatest/emmylua_test.lua`. The test uses the real
language server (`emmylua_ls`) and checks what an editor gets from
the annotations:

- the value returned by `require(<module>)` has an inferred type;
- that type has the documented functions and fields;
- every documented function and field has a description.

The test intentionally does not check signature help, inlay hints,
diagnostics, or generated stubs. It is a regression test for the
annotations themselves, not for the whole LSP feature set.

## Files

- `test/app-luatest/emmylua.lua` is a small reusable LSP client.
  It starts `emmylua_ls` through `popen`, sends JSON-RPC messages
  over stdio, and reads `Content-Length`-framed responses.
- `test/app-luatest/emmylua_test.lua` contains the parameterized
  module tests.
- `test/app-luatest/emmylua/<module>/` contains Lua fixtures for a
  module.
- `test/app-luatest/emmylua/.emmyrc.json` is the shared test
  configuration.

## How a module test works

The test has a table of module specifications. For `varbinary` it
looks like this:

```lua
{
    name = 'varbinary',
    source = 'src/lua/varbinary.lua',
    fixtures = 'test/app-luatest/emmylua/varbinary',
    type_name = 'varbinary',
    fields = {
        is = 'Check that the given object',
        new = 'Create a new',
    },
    object = {
        type_name = 'varbinary_object',
        fields = {
            __eq = 'Checks the equality',
            __len = 'Returns the length',
            __tostring = 'Returns the binary data',
        },
    },
}
```

For each specification the test:

1. Copies the module source and fixtures into a temporary
   workspace.
2. Starts `emmylua_ls` with the fixture `.emmyrc.json`.
3. Opens `module.lua` and checks that hover on the module variable
   returns a type, and that this type contains the expected fields.
4. Requests completion after the module variable and checks that
   every field from `fields` is present.
5. Resolves every completion item and checks that its
   documentation contains the expected phrase.
6. Repeats the field and documentation checks for the type
   returned by the object expression, using `object.lua`.

The `object.lua` fixture obtains the object type with the module's
constructor, for example `varbinary.new('data')`.

## Markers

Fixtures use block-comment markers:

```lua
local varbinary--[[@lsp:module]] = require('varbinary')
varbinary.--[[@lsp:module_fields]]
```

```lua
local bin--[[@lsp:object]] = varbinary.new('data')
bin.--[[@lsp:object_fields]]
```

The test finds the marker, converts its offset to an LSP line and
character, and sends the corresponding request. A marker after a
token is used for hover with a `-1` offset, so the cursor lands on
the last character of the token.
