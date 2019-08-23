#!/bin/bash
set -e

rm_file='rm -f'
rm_dir='rm -rf'
mk_dir='mkdir -p'
ws_prefix=/tmp/tarantool_repo_s3

alloss='ubuntu debian el fedora'
product=tarantool
force=
# the path with binaries either repository
repo=.

# AWS defines
aws="aws --endpoint-url ${AWS_S3_ENDPOINT_URL:-https://hb.bizmrg.com} s3"
aws_cp_public="$aws cp --acl public-read"
aws_sync_public="$aws sync --acl public-read"

function get_os_dists {
    os=$1
    alldists=

    if [ "$os" == "ubuntu" ]; then
        alldists='trusty xenial bionic cosmic disco eoan'
    elif [ "$os" == "debian" ]; then
        alldists='jessie stretch buster'
    elif [ "$os" == "el" ]; then
        alldists='6 7 8'
    elif [ "$os" == "fedora" ]; then
        alldists='27 28 29 30 31'
    fi

    echo "$alldists"
}

function prepare_ws {
    # temporary lock the publication to the repository
    ws_suffix=$1
    ws=${ws_prefix}_${ws_suffix}
    ws_lockfile=${ws}.lock
    if [ -f $ws_lockfile ]; then
        old_proc=$(cat $ws_lockfile)
    fi
    lockfile -l 60 $ws_lockfile
    chmod u+w $ws_lockfile && echo $$ >$ws_lockfile && chmod u-w $ws_lockfile
    if [ "$old_proc" != ""  -a "$old_proc" != "0" ]; then
        kill -9 $old_proc >/dev/null || true
    fi

    # create temporary workspace for the new files
    $rm_dir $ws
    $mk_dir $ws
}

function usage {
    cat <<EOF
Usage for store package binaries from the given path:
    $0 -o=<OS name> -d=<OS distribuition> -b=<S3 bucket> [-p=<product>] <path to package binaries>

Usage for mirroring Debian|Ubuntu OS repositories:
    $0 -o=<OS name> -d=<OS distribuition> -b=<S3 bucket> [-p=<product>] <path to packages binaries>

Arguments:
    <path>
         Path points to the directory with deb/prm packages to be used.

Options:
    -b|--bucket
        MCS S3 bucket already existing which will be used for storing the packages
    -o|--os
        OS to be checked, one of the list:
            $alloss
    -d|--distribution
        Distribution appropriate to the given OS:
EOF
    for os in $alloss ; do
        echo "            $os: <"$(get_os_dists $os)">"
    done
    cat <<EOF
    -p|--product
         Product name to be packed with, default name is 'tarantool'
    -f|--force
         Force updating the remote package with the local one despite the checksum difference
    -h|--help
         Usage help message
EOF
}

for i in "$@"
do
case $i in
    -b=*|--bucket=*)
    bucket="${i#*=}"
    shift # past argument=value
    ;;
    -o=*|--os=*)
    os="${i#*=}"
    if ! echo $alloss | grep -F -q -w $os ; then
        echo "ERROR: OS '$os' is not supported"
        usage
        exit 1
    fi
    shift # past argument=value
    ;;
    -d=*|--distribution=*)
    option_dist="${i#*=}"
    shift # past argument=value
    ;;
    -p=*|--product=*)
    product="${i#*=}"
    shift # past argument=value
    ;;
    -f|--force)
    force=1
    ;;
    -h|--help)
    usage
    exit 0
    ;;
    *)
    repo="${i#*=}"
    pushd $repo >/dev/null ; repo=$PWD ; popd >/dev/null
    shift # past argument=value
    ;;
esac
done

# check that all needed options were set and correct
if [ "$bucket" == "" ]; then
    echo "ERROR: need to set -b|--bucket bucket option, check usage"
    usage
    exit 1
fi
if [ "$option_dist" == "" ]; then
    echo "ERROR: need to set -d|--option_dist OS distribuition name option, check usage"
    usage
    exit 1
fi
if [ "$os" == "" ]; then
    echo "ERROR: need to set -o|--os OS name option, check usage"
    usage
    exit 1
fi
alldists=$(get_os_dists $os)
if [ -n "$option_dist" ] && ! echo $alldists | grep -F -q -w $option_dist ; then
    echo "ERROR: set distribution at options '$option_dist' not found at supported list '$alldists'"
    usage
    exit 1
fi

# set the subpath with binaries based on literal character of the product name
proddir=$(echo $product | head -c 1)

# set bucket path of the given OS in options
bucket_path="$bucket/$os"

function update_deb_packfile {
    packfile=$1
    packtype=$2
    update_dist=$3

    locpackfile=$(echo $packfile | sed "s#^$ws/##g")
    # register DEB/DSC pack file to Packages/Sources file
    reprepro -Vb . include$packtype $update_dist $packfile
    # reprepro copied DEB/DSC file to component which is not needed
    $rm_dir $debdir/$component
    # to have all sources avoid reprepro set DEB/DSC file to its own registry
    $rm_dir db
}

function update_deb_metadata {
    packpath=$1
    packtype=$2

    if [ ! -f $packpath.saved ] ; then
        # get the latest Sources file from S3 either create empty file
        $aws ls "$bucket_path/$packpath" >/dev/null 2>&1 && \
            $aws cp "$bucket_path/$packpath" $packpath.saved || \
            touch $packpath.saved
    fi

    if [ "$packtype" == "dsc" ]; then
        # WORKAROUND: unknown why, but reprepro doesn`t save the Sources
        # file, lets recreate it manualy from it's zipped version
        gunzip -c $packpath.gz >$packpath
        # check if the DSC hash already exists in old Sources file from S3
        # find the hash from the new Sources file
        hash=$(grep '^Checksums-Sha256:' -A3 $packpath | \
            tail -n 1 | awk '{print $1}')
        # search the new hash in the old Sources file from S3
        if grep " $hash .* .*$" $packpath.saved ; then
            echo "WARNING: DSC file already registered in S3!"
            return
        fi
        # check if the DSC file already exists in old Sources file from S3
        file=$(grep '^Files:' -A3 $packpath | tail -n 1 | awk '{print $3}')
        if [ "$force" == "" ] && grep " .* .* $file$" $packpath.saved ; then
            echo "ERROR: the file already exists, but changed, set '-f' to overwrite it: $file"
            echo "New hash: $hash"
            # unlock the publishing
            $rm_file $ws_lockfile
            exit 1
        fi
        updated_dsc=1
    elif [ "$packtype" == "deb" ]; then
        # check if the DEB file already exists in old Packages file from S3
        # find the hash from the new Packages file
        hash=$(grep '^SHA256: ' $packpath)
        # search the new hash in the old Packages file from S3
        if grep "^SHA256: $hash" $packpath.saved ; then
            echo "WARNING: DEB file already registered in S3!"
            return
        fi
        # check if the DEB file already exists in old Packages file from S3
        file=$(grep '^Filename:' $packpath | awk '{print $2}')
        if [ "$force" == "" ] && grep "Filename: $file$" $packpath.saved ; then
            echo "ERROR: the file already exists, but changed, set '-f' to overwrite it: $file"
            echo "New hash: $hash"
            # unlock the publishing
            $rm_file $ws_lockfile
            exit 1
        fi
        updated_deb=1
    fi
    # store the new DEB entry
    cat $packpath >>$packpath.saved
}

# The 'pack_deb' function especialy created for DEB packages. It works
# with DEB packing OS like Ubuntu, Debian. It is based on globaly known
# tool 'reprepro' from:
#     https://wiki.debian.org/DebianRepository/SetupWithReprepro
# This tool works with complete number of distributions of the given OS.
# Result of the routine is the debian package for APT repository with
# file structure equal to the Debian/Ubuntu:
#     http://ftp.am.debian.org/debian/pool/main/t/tarantool/
#     http://ftp.am.debian.org/ubuntu/pool/main/t/
function pack_deb {
    # we need to push packages into 'main' repository only
    component=main

    # debian has special directory 'pool' for packages
    debdir=pool

    # get packages from pointed location
    if ! ls $repo/*.deb $repo/*.dsc $repo/*.tar.*z >/dev/null ; then
        echo "ERROR: files $repo/*.deb $repo/*.dsc $repo/*.tar.*z not found"
        usage
        exit 1
    fi

    # prepare the workspace
    prepare_ws ${os}

    # copy single distribution with binaries packages
    repopath=$ws/pool/${option_dist}/$component/$proddir/$product
    $mk_dir ${repopath}
    cp $repo/*.deb $repo/*.dsc $repo/*.tar.*z $repopath/.
    pushd $ws

    # create the configuration file for 'reprepro' tool
    confpath=$ws/conf
    $rm_dir $confpath
    $mk_dir $confpath

    for loop_dist in $alldists ; do
        cat <<EOF >>$confpath/distributions
Origin: Tarantool
Label: tarantool.org
Suite: stable
Codename: $loop_dist
Architectures: amd64 source
Components: $component
Description: Tarantool DBMS and Tarantool modules
SignWith: 91B625E5
DebIndices: Packages Release . .gz .bz2
UDebIndices: Packages . .gz .bz2
DscIndices: Sources Release .gz .bz2

EOF
    done

    # create standalone repository with separate components
    for loop_dist in $alldists ; do
        echo ================ DISTRIBUTION: $loop_dist ====================
        updated_files=0

        # 1(binaries). use reprepro tool to generate Packages file
        for deb in $ws/$debdir/$loop_dist/$component/*/*/*.deb ; do
            [ -f $deb ] || continue
            updated_deb=0
            # regenerate DEB pack
            update_deb_packfile $deb deb $loop_dist
            echo "Regenerated DEB file: $locpackfile"
            for packages in dists/$loop_dist/$component/binary-*/Packages ; do
                # copy Packages file to avoid of removing by the new DEB version
                # update metadata 'Packages' files
                update_deb_metadata $packages deb
                [ "$updated_deb" == "1" ] || continue
                updated_files=1
            done
            # save the registered DEB file to S3
            if [ "$updated_deb" == 1 ]; then
                $aws_cp_public $deb $bucket_path/$locpackfile
            fi
        done

        # 1(sources). use reprepro tool to generate Sources file
        for dsc in $ws/$debdir/$loop_dist/$component/*/*/*.dsc ; do
            [ -f $dsc ] || continue
            updated_dsc=0
            # regenerate DSC pack
            update_deb_packfile $dsc dsc $loop_dist
            echo "Regenerated DSC file: $locpackfile"
            # copy Sources file to avoid of removing by the new DSC version
            # update metadata 'Sources' file
            update_deb_metadata dists/$loop_dist/$component/source/Sources dsc
            [ "$updated_dsc" == "1" ] || continue
            updated_files=1
            # save the registered DSC file to S3
            $aws_cp_public $dsc $bucket_path/$locpackfile
            tarxz=$(echo $locpackfile | sed 's#\.dsc$#.debian.tar.xz#g')
            $aws_cp_public $ws/$tarxz "$bucket_path/$tarxz"
            orig=$(echo $locpackfile | sed 's#-1\.dsc$#.orig.tar.xz#g')
            $aws_cp_public $ws/$orig "$bucket_path/$orig"
        done

        # check if any DEB/DSC files were newly registered
        [ "$updated_files" == "0" ] && \
            continue || echo "Updating dists"

        # finalize the Packages file
        for packages in dists/$loop_dist/$component/binary-*/Packages ; do
            mv $packages.saved $packages
        done

        # finalize the Sources file
        sources=dists/$loop_dist/$component/source/Sources
        mv $sources.saved $sources

        # 2(binaries). update Packages file archives
        for packpath in dists/$loop_dist/$component/binary-* ; do
            pushd $packpath
            sed "s#Filename: $debdir/$component/#Filename: $debdir/$loop_dist/$component/#g" -i Packages
            bzip2 -c Packages >Packages.bz2
            gzip -c Packages >Packages.gz
            popd
        done

        # 2(sources). update Sources file archives
        pushd dists/$loop_dist/$component/source
        sed "s#Directory: $debdir/$component/#Directory: $debdir/$loop_dist/$component/#g" -i Sources
        bzip2 -c Sources >Sources.bz2
        gzip -c Sources >Sources.gz
        popd

        # 3. update checksums entries of the Packages* files in *Release files
        # NOTE: it is stable structure of the *Release files when the checksum
        #       entries in it in the following way:
        # MD5Sum:
        #  <checksum> <size> <file orig>
        #  <checksum> <size> <file debian>
        # SHA1:
        #  <checksum> <size> <file orig>
        #  <checksum> <size> <file debian>
        # SHA256:
        #  <checksum> <size> <file orig>
        #  <checksum> <size> <file debian>
        #       The script bellow puts 'md5' value at the 1st found file entry,
        #       'sha1' - at the 2nd and 'sha256' at the 3rd
        pushd dists/$loop_dist
        for file in $(grep " $component/" Release | awk '{print $3}' | sort -u) ; do
            sz=$(stat -c "%s" $file)
            md5=$(md5sum $file | awk '{print $1}')
            sha1=$(sha1sum $file | awk '{print $1}')
            sha256=$(sha256sum $file | awk '{print $1}')
            awk 'BEGIN{c = 0} ; {
                if ($3 == p) {
                    c = c + 1
                    if (c == 1) {print " " md  " " s " " p}
                    if (c == 2) {print " " sh1 " " s " " p}
                    if (c == 3) {print " " sh2 " " s " " p}
                } else {print $0}
            }' p="$file" s="$sz" md="$md5" sh1="$sha1" sh2="$sha256" \
                    Release >Release.new
            mv Release.new Release
        done
        # resign the selfsigned InRelease file
        $rm_file InRelease
        gpg --clearsign -o InRelease Release
        # resign the Release file
        $rm_file Release.gpg
        gpg -abs -o Release.gpg Release
        popd

        # 4. sync the latest distribution path changes to S3
        $aws_sync_public dists/$loop_dist "$bucket_path/dists/$loop_dist"
    done

    # unlock the publishing
    $rm_file $ws_lockfile

    popd
}

# The 'pack_rpm' function especialy created for RPM packages. It works
# with RPM packing OS like Centos, Fedora. It is based on globaly known
# tool 'createrepo' from:
#     https://linux.die.net/man/8/createrepo
# This tool works with single distribution of the given OS.
# Result of the routine is the rpm package for YUM repository with
# file structure equal to the Centos/Fedora:
#     http://mirror.centos.org/centos/7/os/x86_64/Packages/
#     http://mirrors.kernel.org/fedora/releases/30/Everything/x86_64/os/Packages/t/
function pack_rpm {
    if ! ls $repo/*.rpm >/dev/null ; then
        echo "ERROR: Current '$repo' path doesn't have RPM packages in path"
        usage
        exit 1
    fi

    # prepare the workspace
    prepare_ws ${os}_${option_dist}

    # copy the needed package binaries to the workspace
    cp $repo/*.rpm $ws/.

    pushd $ws

    # set the paths
    if [ "$os" == "el" ]; then
        repopath=$option_dist/os/x86_64
        rpmpath=Packages
    elif [ "$os" == "fedora" ]; then
        repopath=releases/$option_dist/Everything/x86_64/os
        rpmpath=Packages/$proddir
    fi
    packpath=$repopath/$rpmpath

    # prepare local repository with packages
    $mk_dir $packpath
    mv *.rpm $packpath/.
    cd $repopath

    # copy the current metadata files from S3
    mkdir repodata.base
    for file in $($aws ls $bucket_path/$repopath/repodata/ | awk '{print $NF}') ; do
        $aws ls $bucket_path/$repopath/repodata/$file || continue
        $aws cp $bucket_path/$repopath/repodata/$file repodata.base/$file
    done

    # create the new repository metadata files
    createrepo --no-database --update --workers=2 \
        --compress-type=gz --simple-md-filenames .

    updated_rpms=0
    # loop by the new hashes from the new meta file
    for hash in $(zcat repodata/other.xml.gz | grep "<package pkgid=" | \
        awk -F'"' '{print $2}') ; do
        updated_rpm=0
        name=$(zcat repodata/other.xml.gz | grep "<package pkgid=\"$hash\"" | \
            awk -F'"' '{print $4}')
        # search the new hash in the old meta file from S3
        if zcat repodata.base/filelists.xml.gz | grep "pkgid=\"$hash\"" | \
            grep "name=\"$name\"" ; then
            echo "WARNING: $name file already registered in S3!"
            echo "File hash: $hash"
            continue
        fi
        updated_rpms=1
        # check if the hashed file already exists in old meta file from S3
        file=$(zcat repodata/primary.xml.gz | \
            grep -e "<checksum type=" -e "<location href=" | \
            grep "$hash" -A1 | grep "<location href=" | \
            awk -F'"' '{print $2}')
        # check if the file already exists in S3
        if [ "$force" == "" ] && zcat repodata.base/primary.xml.gz | \
                grep "<location href=\"$file\"" ; then
            echo "ERROR: the file already exists, but changed, set '-f' to overwrite it: $file"
            echo "New hash: $hash"
            # unlock the publishing
            $rm_file $ws_lockfile
            exit 1
        fi
    done

    # check if any RPM files were newly registered
    [ "$updated_rpms" == "0" ] && \
        return || echo "Updating dists"

    # move the repodata files to the standalone location
    mv repodata repodata.adding

    # merge metadata files
    mkdir repodata
    head -n 2 repodata.adding/repomd.xml >repodata/repomd.xml
    for file in filelists.xml other.xml primary.xml ; do
        # 1. take the 1st line only - to skip the line with
        #    number of packages which is not needed
        zcat repodata.adding/$file.gz | head -n 1 >repodata/$file
        # 2. take 2nd line with metadata tag and update
        #    the packages number in it
        packsold=0
        if [ -f repodata.base/$file.gz ] ; then
            packsold=$(zcat repodata.base/$file.gz | head -n 2 | \
                tail -n 1 | sed 's#.*packages="\(.*\)".*#\1#g')
        fi
        packsnew=$(zcat repodata.adding/$file.gz | head -n 2 | \
            tail -n 1 | sed 's#.*packages="\(.*\)".*#\1#g')
        packs=$(($packsold+$packsnew))
        zcat repodata.adding/$file.gz | head -n 2 | tail -n 1 | \
            sed "s#packages=\".*\"#packages=\"$packs\"#g" >>repodata/$file
        # 3. take only 'package' tags from new file
        zcat repodata.adding/$file.gz | tail -n +3 | head -n -1 \
            >>repodata/$file
        # 4. take only 'package' tags from old file if exists
        if [ -f repodata.base/$file.gz ] ; then
            zcat repodata.base/$file.gz | tail -n +3 | head -n -1 \
                >>repodata/$file
        fi
        # 5. take the last closing line with metadata tag
        zcat repodata.adding/$file.gz | tail -n 1 >>repodata/$file

        # get the new data
        chsnew=$(sha256sum repodata/$file | awk '{print $1}')
        sz=$(stat --printf="%s" repodata/$file)
        gzip repodata/$file
        chsgznew=$(sha256sum repodata/$file.gz | awk '{print $1}')
        szgz=$(stat --printf="%s" repodata/$file.gz)
        timestamp=$(date +%s -r repodata/$file.gz)

        # add info to repomd.xml file
        name=$(echo $file | sed 's#\.xml$##g')
        cat <<EOF >>repodata/repomd.xml
<data type="$name">
  <checksum type="sha256">$chsgznew</checksum>
  <open-checksum type="sha256">$chsnew</open-checksum>
  <location href="repodata/$file.gz"/>
  <timestamp>$timestamp</timestamp>
  <size>$szgz</size>
  <open-size>$sz</open-size>
</data>"
EOF
    done
    tail -n 1 repodata.adding/repomd.xml >>repodata/repomd.xml
    gpg --detach-sign --armor repodata/repomd.xml

    # copy the packages to S3
    for file in $rpmpath/*.rpm ; do
        $aws_cp_public $file "$bucket_path/$repopath/$file"
    done

    # update the metadata at the S3
    $aws_sync_public repodata "$bucket_path/$repopath/repodata"

    # unlock the publishing
    $rm_file $ws_lockfile

    popd
}

if [ "$os" == "ubuntu" -o "$os" == "debian" ]; then
    pack_deb
elif [ "$os" == "el" -o "$os" == "fedora" ]; then
    pack_rpm
else
    echo "USAGE: given OS '$os' is not supported, use any single from the list: $alloss"
    usage
    exit 1
fi
