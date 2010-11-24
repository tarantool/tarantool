#!/bin/bash

echo -n 'const char tarantool_version_string[] = "'
git tag | tail -n 1 | tr -d \\n
echo -n " +"$(git log --oneline $(git tag | tail -n1)..HEAD | wc -l)"commits "
git log -1 --format='%h' | tr -d \\n
git diff --quiet || (echo -n ' AND'; git diff --shortstat) | tr -d \\n
echo '";'
