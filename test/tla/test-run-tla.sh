#!/bin/sh

path=$(realpath ../..)
path+="/src/tla"
export TLA_JAVA_OPTS="-DTLA-Library=$path:$path/modules"

find . -type f -name "*.cfg" | while read -r cfg_file; do
    dir=$(dirname "$cfg_file")
    base=$(basename "$cfg_file" .cfg)

    (
        cd "$dir" || exit
        tlc "$base"
    )
done
