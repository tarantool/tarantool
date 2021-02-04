# Setup environment

Action setup environment to $GITHUB_ENV as suggested in [Github Action documentation](https://docs.github.com/en/actions/reference/workflow-commands-for-github-actions#setting-an-environment-variable). It can be used for common environment setup for different testing workflows.

## How to use Github Action from Github workflow

Add the following code to the running steps after checkout done:
```
  - uses: ./.github/actions/environment
```

