# Setup environment for sanitizers on Linux

Action setups the environment on Linux runners (install requirements, setup the
workflow environment, etc) for testing with sanitizers enabled.

## How to use Github Action from Github workflow

Add the following code to the running steps before LuaJIT configuration:
```
- uses: ./.github/actions/setup-sanitizers
  if: ${{ matrix.OS == 'Linux' }}
```
