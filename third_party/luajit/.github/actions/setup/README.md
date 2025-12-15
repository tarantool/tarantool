# Setup environment

Action setups the environment to $GITHUB_ENV as suggested in
[Github Action documentation](https://docs.github.com/en/actions/reference/workflow-commands-for-github-actions#setting-an-environment-variable).
It is used as a common environment setup for the different testing workflows.

## How to use Github Action from Github workflow

Add the following code to the running steps or OS-specific GitHub Actions after
checkout step is finished:
```
- uses: ./.github/actions/setup
```
