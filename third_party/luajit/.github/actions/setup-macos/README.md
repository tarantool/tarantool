# Setup environment on macOS

Action setups the environment on macOS runners (install requirements, setup the
workflow environment, etc).

## How to use Github Action from Github workflow

Add the following code to the running steps before LuaJIT configuration:
```
- uses: ./.github/actions/setup-macos
  if: ${{ matrix.OS == 'macOS' }}
```
