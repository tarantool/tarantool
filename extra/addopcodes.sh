#!/bin/sh
#
# This script appends additional token codes to the end of the
# parse.h file that lemon generates.  These extra token codes are
# not used by the parser.  But they are used by the tokenizer and/or
# the code generator.

set -eu  # Strict shell (w/o -x and -o pipefail)
set -f   # disable pathname expansion

max=0
newline="$(printf '\n')"
IFS="$newline"
while read line; do
    case "$line" in
    '#define TK_'*)
        printf '%s\n' "$line"
        IFS=" "
        set -- $line
        IFS="$newline"
        x="$3"
        if [ "$x" -gt $max ]; then
            max="$x"
        fi
        ;;
    esac
done < "$1"

# The following are the extra token codes to be added.  SPACE and
# ILLEGAL *must* be the last two token codes and they must be in that order.
extras="            \
    TO_TEXT         \
    ISNULL          \
    NOTNULL         \
    TO_BLOB         \
    TO_NUMERIC      \
    TO_INT          \
    TO_REAL         \
    END_OF_FILE     \
    UNCLOSED_STRING \
    FUNCTION        \
    COLUMN          \
    AGG_FUNCTION    \
    AGG_COLUMN      \
    UMINUS          \
    UPLUS           \
    REGISTER        \
    VECTOR          \
    SELECT_COLUMN   \
    ASTERISK        \
    SPAN            \
    SPACE           \
    ILLEGAL         \
"

IFS=" "
for x in $extras; do
    max=$((max + 1))
    printf '#define TK_%-29s %4d\n' "$x" "$max"
done

# Some additional #defines related to token codes.
printf '%b\n' "\n/* The token codes above must all fit in 8 bits */"
printf '#define %-20s %-6s\n' TKFLG_MASK 0xff
printf '%b\n' "\n/* Flags that can be added to a token code when it is not"
printf '%b\n' "** being stored in a u8: */"
printf '#define %-20s %-6s %s\n' \
    TKFLG_DONTFOLD 0x100 '/* Omit constant folding optimizations */'
