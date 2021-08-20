# Check if commit exist

Test case for checking if commit sha is existing inside repository

## Usage
```yaml
- name: Check Commit SHA
  uses: ./.github/actions/commit-hash-check
  with:
    commit: '123456'
```

Commit is an argument that is not required

## Action.yml
```yaml
name: 'Check Commit SHA'
description: 'Checks if commit sha exist in repo'
inputs:
  commit:
    description: 'Commit ID'
    required: false
runs:
  using: 'composite'
  steps:
    - name: 'Check Commit SHA'
      run: |
        git checkout ${{ inputs.commit }}
      shell: bash
```