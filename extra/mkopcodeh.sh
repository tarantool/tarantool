#!/bin/sh
#
# Generate the file opcodes.h.
#
# This shell script scans a concatenation of the parse.h output file from the
# parser and the vdbe.c source file in order to generate the opcodes numbers
# for all opcodes.
#
# The lines of the vdbe.c that we are interested in are of the form:
#
#       case OP_aaaa:      /* same as TK_bbbbb */
#
# The TK_ comment is optional.  If it is present, then the value assigned to
# the OP_ is the same as the TK_ value.  If missing, the OP_ value is assigned
# a small integer that is different from every other OP_ value.
#
# We go to the trouble of making some OP_ values the same as TK_ values
# as an optimization.  During parsing, things like expression operators
# are coded with TK_ values such as TK_ADD, TK_DIVIDE, and so forth.  Later
# during code generation, we need to generate corresponding opcodes like
# OP_Add and OP_Divide.  By making TK_ADD==OP_Add and TK_DIVIDE==OP_Divide,
# code to translate from one to the other is avoided.  This makes the
# code generator smaller and faster.
#
# This script also scans for lines of the form:
#
#       case OP_aaaa:       /* jump, in1, in2, in3, out2-prerelease, out3 */
#
# When such comments are found on an opcode, it means that certain
# properties apply to that opcode.  Set corresponding flags using the
# OPFLG_INITIALIZER macro.

set -eu  # Strict shell (w/o -x and -o pipefail)
set -f   # disable pathname expansion

currentOp=""
nOp=0
newline="$(printf '\n')"
IFS="$newline"
while read line; do
    case "$line" in
    # Remember the TK_ values from the parse.h file.
    # NB:  The "TK_" prefix stands for "ToKen", not the graphical Tk toolkit
    # commonly associated with TCL.
    '#define TK_'*)
        IFS=" "
        set -- $line
        IFS="$newline"
        eval "ARRAY_tk_$2=$3"
        continue
        ;;

    # Find "/* Opcode: " lines in the vdbe.c file.  Each one introduces
    # a new opcode.  Remember which parameters are used.
    ??' Opcode: '*)
        IFS=" "
        set -- $line
        IFS="$newline"
        currentOp="OP_$3"
        m=0
        for term in "$@"; do
            case "$term" in
                P1) m=$((m + 1)) ;;
                P2) m=$((m + 2)) ;;
                P3) m=$((m + 4)) ;;
                P4) m=$((m + 8)) ;;
                P5) m=$((m + 16)) ;;
            esac
        done
        eval "ARRAY_paramused_$currentOp=$m"
        ;;

    # Find "** Synopsis: " lines that follow Opcode:
    ??' Synopsis: '*)
        if [ -n "$currentOp" ]; then
            x=${line#??' Synopsis: '}
            eval "ARRAY_synopsis_$currentOp=\"$x\""
        fi
        ;;

    # Scan for "case OP_aaaa:" lines in the vdbe.c file
    'case OP_'*)
        IFS=" "
        set -- $line
        IFS="$newline"
        name=${2%:}
        eval "ARRAY_op_$name=-1"
        eval "ARRAY_jump_$name=0"
        eval "ARRAY_in1_$name=0"
        eval "ARRAY_in2_$name=0"
        eval "ARRAY_in3_$name=0"
        eval "ARRAY_out2_$name=0"
        eval "ARRAY_out3_$name=0"
        i=4
        while [ "$i" -lt "$#" ]; do
            eval "sym=\${$i%,}"
            case "$sym" in
                same)
                    i=$((i + 1))
                    eval "sym=\${$i}"
                    if [ "$sym" = "as" ]; then
                        i=$((i + 1))
                        eval "sym=\${$i%,}"
                        eval "val=\$ARRAY_tk_$sym"
                        eval "ARRAY_op_$name=$val"
                        eval "ARRAY_used_$val=1"
                        eval "ARRAY_sameas_$val=$sym"
                        eval "ARRAY_def_$val=$name"
                    fi
                ;;
                jump) eval "ARRAY_jump_$name=1" ;;
                in1)  eval "ARRAY_in1_$name=1" ;;
                in2)  eval "ARRAY_in2_$name=1" ;;
                in3)  eval "ARRAY_in3_$name=1" ;;
                out2) eval "ARRAY_out2_$name=1" ;;
                out3) eval "ARRAY_out3_$name=1" ;;
            esac
            i=$((i + 1))
        done
        eval "ARRAY_order_$nOp=$name"
        nOp=$((nOp + 1))
        ;;
    esac
done

# Assign numbers to all opcodes and output the result.
printf '%s\n' "/* Automatically generated.  Do not edit */"
printf '%s\n' "/* See the tool/mkopcodeh.sh script for details */"
for name in OP_Noop OP_Explain; do
    eval "ARRAY_jump_$name=0"
    eval "ARRAY_in1_$name=0"
    eval "ARRAY_in2_$name=0"
    eval "ARRAY_in3_$name=0"
    eval "ARRAY_out2_$name=0"
    eval "ARRAY_out3_$name=0"
    eval "ARRAY_op_$name=-1"
    eval "ARRAY_order_$nOp=$name"
    nOp=$((nOp + 1))
done

# Assign small values to opcodes that are processed by resolveP2Values()
# to make code generation for the switch() statement smaller and faster.
cnt=-1
i=0
while [ "$i" -lt "$nOp" ]; do
    eval "name=\$ARRAY_order_$i"
    case "$name" in
    # The following are the opcodes that are processed by resolveP2Values()
    OP_AutoCommit  | \
    OP_Savepoint   | \
    OP_Checkpoint  | \
    OP_JournalMode | \
    OP_Next        | \
    OP_NextIfOpen  | \
    OP_SorterNext  | \
    OP_Prev        | \
    OP_PrevIfOpen)
        cnt=$((cnt + 1))
        eval "used=\${ARRAY_used_$cnt:-}"
        while [ -n "$used" ]; do
            cnt=$((cnt + 1))
            eval "used=\${ARRAY_used_$cnt:-}"
        done
        eval "ARRAY_op_$name=$cnt"
        eval "ARRAY_used_$cnt=1"
        eval "ARRAY_def_$cnt=$name"
        ;;
    esac
    i=$((i + 1))
done

# Assign the next group of values to JUMP opcodes
i=0
while [ "$i" -lt "$nOp" ]; do
    eval "name=\$ARRAY_order_$i"
    eval "op=\$ARRAY_op_$name"
    eval "jump=\$ARRAY_jump_$name"
    if [ "$op" -ge 0 ]; then i=$((i + 1)); continue; fi
    if [ "$jump" -eq 0 ]; then i=$((i + 1)); continue; fi
    cnt=$((cnt + 1))
    eval "used=\${ARRAY_used_$cnt:-}"
    while [ -n "$used" ]; do
        cnt=$((cnt + 1))
        eval "used=\${ARRAY_used_$cnt:-}"
    done
    eval "ARRAY_op_$name=$cnt"
    eval "ARRAY_used_$cnt=1"
    eval "ARRAY_def_$cnt=$name"
    i=$((i + 1))
done

# Find the numeric value for the largest JUMP opcode
mxJump=-1
i=0
while [ "$i" -lt "$nOp" ]; do
    eval "name=\$ARRAY_order_$i"
    eval "op=\$ARRAY_op_$name"
    eval "jump=\$ARRAY_jump_$name"
    if [ "$jump" -eq 1 -a "$op" -gt "$mxJump" ]; then
        mxJump="$op"
    fi
    i=$((i + 1))
done

# Generate the numeric values for all remaining opcodes
i=0
while [ "$i" -lt "$nOp" ]; do
    eval "name=\$ARRAY_order_$i"
    eval "op=\$ARRAY_op_$name"
    if [ "$op" -lt 0 ]; then
        cnt=$((cnt + 1))
        eval "used=\${ARRAY_used_$cnt:-}"
        while [ -n "$used" ]; do
            cnt=$((cnt + 1))
            eval "used=\${ARRAY_used_$cnt:-}"
        done
        eval "ARRAY_op_$name=$cnt"
        eval "ARRAY_used_$cnt=1"
        eval "ARRAY_def_$cnt=$name"
    fi
    i=$((i + 1))
done
max="$cnt"
i=0
while [ "$i" -lt "$nOp" ]; do
    eval "used=\${ARRAY_used_$i:-}"
    if [ -z "$used" ]; then
        eval "ARRAY_def_$i=OP_NotUsed_$i"
    fi
    eval "name=\$ARRAY_def_$i"
    printf '#define %-16s %3d' "$name" "$i"
    com=""
    eval "sameas=\${ARRAY_sameas_$i:-}"
    if [ -n "$sameas" ]; then
        com="same as $sameas"
    fi
    eval "synopsis=\${ARRAY_synopsis_$name:-}"
    if [ -n "$synopsis" ]; then
        if [ -z "$com" ]; then
            com="synopsis: $synopsis"
        else
            com="${com}, synopsis: $synopsis"
        fi
    fi
    if [ -n "$com" ]; then
        printf ' /* %-42s */' "$com"
    fi
    printf '\n'
    i=$((i + 1))
done

# Generate the bitvectors:
ARRAY_bv_0=0
i=0
while [ "$i" -le "$max" ]; do
    eval "name=\$ARRAY_def_$i"
    x=0
    eval "jump=\$ARRAY_jump_$name"
    eval "in1=\$ARRAY_in1_$name"
    eval "in2=\$ARRAY_in2_$name"
    eval "in3=\$ARRAY_in3_$name"
    eval "out2=\$ARRAY_out2_$name"
    eval "out3=\$ARRAY_out3_$name"
    x=$((x + jump))
    x=$((x + 2 * in1))
    x=$((x + 4 * in2))
    x=$((x + 8 * in3))
    x=$((x + 16 * out2))
    x=$((x + 32 * out3))
    eval "ARRAY_bv_$i=$x"
    i=$((i + 1))
done

printf '%s\n' ""
printf '%s\n' "/* Properties such as \"out2\" or \"jump\" that are specified in"
printf '%s\n' "** comments following the \"case\" for each opcode in the vdbe.c"
printf '%s\n' "** are encoded into bitvectors as follows:"
printf '%s\n' "*/"
printf '%s\n' "#define OPFLG_JUMP        0x01  /* jump:  P2 holds jmp target */"
printf '%s\n' "#define OPFLG_IN1         0x02  /* in1:   P1 is an input */"
printf '%s\n' "#define OPFLG_IN2         0x04  /* in2:   P2 is an input */"
printf '%s\n' "#define OPFLG_IN3         0x08  /* in3:   P3 is an input */"
printf '%s\n' "#define OPFLG_OUT2        0x10  /* out2:  P2 is an output */"
printf '%s\n' "#define OPFLG_OUT3        0x20  /* out3:  P3 is an output */"
printf '%s\n' "#define OPFLG_INITIALIZER {\\"
i=0
while [ "$i" -le "$max" ]; do
    if [ "$((i % 8))" -eq 0 ]; then
        printf '/* %3d */' "$i"
    fi
    eval "bv=\$ARRAY_bv_$i"
    printf ' 0x%02x,' "$bv"
    if [ "$((i % 8))" -eq 7 ]; then
        printf '%s\n' "\\"
    fi
    i=$((i + 1))
done
printf '%s\n' "}"
printf '%s\n' ""
printf '%s\n' "/* The sqlite3P2Values() routine is able to run faster if it knows"
printf '%s\n' "** the value of the largest JUMP opcode.  The smaller the maximum"
printf '%s\n' "** JUMP opcode the better, so the mkopcodeh.sh script that"
printf '%s\n' "** generated this include file strives to group all JUMP opcodes"
printf '%s\n' "** together near the beginning of the list."
printf '%s\n' "*/"
printf '%s\n' "#define SQLITE_MX_JUMP_OPCODE  $mxJump  /* Maximum JUMP opcode */"
