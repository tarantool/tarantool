# Setup environment for Valgrind on Linux

Action setups the environment on Linux runners (install requirements, setup the
workflow environment, etc) for testing with Valgrind.

## How to use Github Action from Github workflow

Add the following code to the running steps before LuaJIT configuration:
```
- uses: ./.github/actions/setup-valgrind
  if: ${{ matrix.OS == 'Linux' }}
```