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
    objects = {
        {
            fixture = 'object.lua',
            type_name = 'varbinary_object',
            fields = {},
            forbidden_fields = {'__tostring'},
            description = 'variable-length binary object',
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
6. Repeats the field and documentation checks for every object
   fixture of the module.
7. Checks that the class description of the object, declared with
   comment lines before `---@class`, contains the phrase from the
   `description` field of the specification.
8. Checks that the object members listed in `forbidden_fields` are
   not offered in completion.

An object fixture obtains the object type either with the module's
constructor, for example `varbinary.new('data')`, or from a value
returned by a module function, for example `xlog.pairs()`.

Only `---@field` annotations are checked as object members. Metamethods
are not class members: the ones declared with `---@operator` do not show
up in completion, and a hook such as `__tostring` must not be annotated
with `---@field`, so `forbidden_fields` verifies that it is not offered.
A class description is rendered only when it is written before the
`---@class` tag: the text after the tag is used neither in hover nor in
the generated documentation.

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
