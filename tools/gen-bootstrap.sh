#!/bin/bash

# The script is designed for simplification of new bootstrap.snap generation.
# Just run it in tarantool build directory after updating update.lua (which
# introduces new data version) and examine its output.
# The script will try to generate bootstrap.snap in build directory (which is
# current directory by default), and in case of success will offer to copy the
# file to the source tree.
# Run --help for more options.
# The manual instruction by which the script was created is available here:
MANUAL_LINK="https://github.com/tarantool/tarantool/wiki/How-to-generate-new-bootstrap-snapshot"
# Now the script does the following:
ACTIONS=( "rebuild tarantool.")
ACTIONS+=("run box.cfg{} box.internal.bootstrap() by tarantool in a clean dir.")
ACTIONS+=("take the latest snapshot file an rename it to bootstrap.snap file.")
ACTIONS+=("edit the file and change line with VClock to an empty set: {}.")

set -eu -o pipefail

error() {
   echo "$@" 1>&2
   exit 1
}

if ! getopt -V >/dev/null 2>&1; then
    error "getopt must be (installed)."
fi

if ! xxd -v >/dev/null 2>&1; then
    error "xxd must be installed."
fi

if ! cmake --version >/dev/null 2>&1; then
    error "cmake must be installed."
fi

usage() {
    echo ""
    echo "Usage:"
    echo " $0 [options]"
    echo ""
    echo "Options:"
    echo " -b, --builddir       Set build directory of tarantool"
    echo "                       (default: current directory)"
    echo " -s, --sourcedir      Set source directory of tarantool"
    echo "                       (default: parsed from CMakeCache.txt)"
    echo " -t, --tempdir        Set temporary directory"
    echo "                       (default: <build dir>/gen-bootstrap-tmp)"
    echo " -a, --autobuild      Build tarantool before generation (default)."
    echo " -A, --no-autobuild   Don't build tarantool before generation."
    echo " -h, --help           Show this help and exit"
    exit 0
}

shortopts="b:s:t:aAh"
longopts="builddir:,sourcedir:,tempdir:,autobuild,no-autobuild,help"

builddir=.
sourcedir=
tempdir=
tempdir_default_name=gen-bootstrap-tmp
autobuild=true

opts=$(getopt --options "$shortopts" --long "$longopts" --name "$0" -- "$@")
eval set -- "$opts"

while true; do
    case "$1" in
        -b | --builddir ) builddir=$2; shift 2;;
        -s | --sourcedir ) sourcedir=$2; shift 2;;
        -t | --tempdir ) tempdir=$2; shift 2;;
        -a | --autobuild ) autobuild=true; shift 1;;
        -A | --no-autobuild ) autobuild=false; shift 1;;
        -h | --help ) usage; shift 1;;
        -- ) shift; break;;
        * ) error "Failed to parse options";;
    esac
done
if [[ $# -gt 0 ]]; then
    error "Unrecognized command: $@"
fi

if ! [[ -d "$builddir" ]]; then
    error "Build directory is not a directory!"
fi

builddir=$(realpath "$builddir")

if [[ -z "$tempdir" ]]; then
    tempdir="$builddir/$tempdir_default_name"
fi

tempdir=$(realpath "$tempdir")

if [[ -z "$sourcedir" ]]; then
    if ! [[ -f "$builddir/CMakeCache.txt" ]]; then
        error "Tarantool build was not found in '$builddir'"
    fi
    line=$(grep "^tarantool_SOURCE_DIR:STATIC=" "$builddir/CMakeCache.txt")
    if ! [[ "$line" =~ ^tarantool_SOURCE_DIR:STATIC=(.*)$ ]]; then
        error "Tarantool build was not found in '$builddir'"
    fi
    sourcedir="${BASH_REMATCH[1]}"
fi

sourcedir=$(realpath "$sourcedir")
source_bootstrap="$sourcedir/src/box/bootstrap.snap"

if ! [[ -f "$source_bootstrap" ]]; then
    error "File $source_bootstrap was not found. Wrong sourcedir?"
fi

if [[ -f "$tempdir" ]]; then
    error "Regular file $tempdir was found, has no idea what to do, exiting."
fi

if [[ -d "$tempdir" ]]; then
    echo "Folder $tempdir was found, it seems that previous run was failed."
    read -p "Drop the folder and proceed [Yn]?" yn
    case ${yn:-y} in
        [Yy]* ) rm -r "$tempdir";;
        [Nn]* ) echo "OK, exiting."; exit 0;;
        * ) error "Wrong answer.";;
    esac
fi

cd "$builddir"
tnt_binary="$builddir/src/tarantool"

if [[ $autobuild = true ]]; then
    if ! cmake --build . -j; then
        error "Failed to rebuild tarantool."
    fi
else
    unset 'ACTIONS[0]'
fi

if ! "$tnt_binary" --version >/dev/null 2>&1; then
    error "Tarantool binary was not found ($tnt_binary)."
fi

mkdir "$tempdir"
cd "$tempdir"

if ! "$tnt_binary" -e "box.cfg{} box.internal.bootstrap() os.exit(0)"; then
    error "Failed to run script by tarantool, exiting."
fi

mv $(ls *.snap | sort | tail -n 1) ./bootstrap.snap

# Clear VClock field of the file.
# Actually all the code below does only simple command:
# sed -i '0,/^VClock: {.*}$/{s//VClock: {}/}' ./bootstrap.snap
# .. but instead let's use canonical approach for patching binary files!
if ! head -n 6 ./bootstrap.snap | egrep "VClock: {.*}" > /dev/null; then
    error "Failed to patch VClock: is was not found."
fi
head -n 6 ./bootstrap.snap > old_header.txt
head -n 6 ./bootstrap.snap | sed "s/VClock: {.*}/VClock: {}/" > new_header.txt
bootstrap_hex=$(xxd -p bootstrap.snap | tr -d '\n')
old_header_hex=$(xxd -p old_header.txt | tr -d '\n')
new_header_hex=$(xxd -p new_header.txt | tr -d '\n')
if ! [[ ${bootstrap_hex} =~ ^${old_header_hex}(.*)$ ]]; then
    error "Failed to patch VClock: hex patch failed."
fi
echo ${new_header_hex}${BASH_REMATCH[1]} | xxd -p -r > ./bootstrap.snap

mv ./bootstrap.snap "$builddir/bootstrap.snap"
cd "$builddir"
rm -r "$tempdir"

echo ""
echo "What was done:"
for action in "${ACTIONS[@]}"; do
    echo "* ${action}"
done
echo "Please compare with manual instruction:"
echo "$MANUAL_LINK"
echo ""

copy_to_source_tree=false
echo "It seems that ./bootstrap.snap was successfully generated."
read -p "Copy file to the source tree ('$source_bootstrap') [yN]?" yn
case ${yn:-n} in
    [Yy]* ) copy_to_source_tree=true;;
    [Nn]* ) copy_to_source_tree=false;;
    * ) error "Wrong answer, exiting";;
esac

if [[ $copy_to_source_tree = true ]]; then
    cp ./bootstrap.snap "$source_bootstrap"
    echo "Copied to $source_bootstrap"
else
    echo "Enjoy bootstrap.snap"
fi
