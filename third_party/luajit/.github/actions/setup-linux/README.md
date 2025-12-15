# Setup environment on Linux

Action setups the environment on Linux runners (install requirements, setup the
workflow environment, etc).

## How to use Github Action from Github workflow

Add the following code to the running steps before LuaJIT configuration:
```
- uses: ./.github/actions/setup-linux
  if: ${{ matrix.OS == 'Linux' }}
```
