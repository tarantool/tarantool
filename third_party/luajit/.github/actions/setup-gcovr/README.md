# Setup gcovr

Action setups the gcovr tool on Linux runners.

## How to use Github Action from Github workflow

Add the following code to the running steps before LuaJIT configuration:
```
- uses: ./.github/actions/setup-gcovr
```
