#!/bin/bash
set -e

rm_file='rm -f'
rm_dir='rm -rf'
mk_dir='mkdir -p'
ws_prefix=/tmp/tarantool_repo_s3

alloss='ubuntu debian el fedora'
product=tarantool
force=
skip_errors=
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
        alldists='trusty xenial bionic disco eoan'
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
    -s|--skip_errors
         Skip failing on changed packages
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
    -s|--skip_errors)
    skip_errors=1
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

    # WORKAROUND: unknown why, but reprepro doesn`t save the Sources file,
    #             let`s recreate it manualy from it's zipped version
    gunzip -c dists/$loop_dist/$component/source/Sources.gz \
        >dists/$loop_dist/$component/source/Sources

    # WORKAROUND: unknown why, but reprepro creates paths w/o distribution in
    #             it and no solution using configuration setup neither options
    #             found to set it there, let's set it here manually
    for packpath in dists/$loop_dist/$component/binary-* ; do
        sed "s#Filename: $debdir/$component/#Filename: $debdir/$loop_dist/$component/#g" \
            -i $packpath/Packages
    done
    sed "s#Directory: $debdir/$component/#Directory: $debdir/$loop_dist/$component/#g" \
        -i dists/$loop_dist/$component/source/Sources
}

function update_deb_metadata {
    packpath=$1
    packtype=$2
    packfile=$3

    file_exists=''

    if [ ! -f $packpath.saved ] ; then
        # get the latest Sources file from S3 either create empty file
        $aws ls "$bucket_path/$packpath" >/dev/null 2>&1 && \
            $aws cp "$bucket_path/$packpath" $packpath.saved || \
            touch $packpath.saved
    fi

    if [ "$packtype" == "dsc" ]; then
        # check if the DSC hash already exists in old Sources file from S3
        # find the hash from the new Sources file
        hash=$(grep '^Checksums-Sha256:' -A3 $packpath | \
            tail -n 1 | awk '{print $1}')
        # check if the file already exists in S3
        if $aws ls "$bucket_path/$packfile" ; then
            echo "WARNING: DSC file already exists in S3!"
            file_exists=$bucket_path/$packfile
        fi
        # search the new hash in the old Sources file from S3
        if grep " $hash .* .*$" $packpath.saved ; then
            echo "WARNING: DSC file already registered in S3!"
            echo "File hash: $hash"
            if [ "$file_exists" != "" ] ; then
                return
            fi
        fi
        # check if the DSC file already exists in old Sources file from S3
        file=$(grep '^Files:' -A3 $packpath | tail -n 1 | awk '{print $3}')
        if grep " .* .* $file$" $packpath.saved ; then
            if [ "$force" == "" -a "$file_exists" != "" ] ; then
                if [ "$skip_errors" == "" ] ; then
                    echo "ERROR: the file already exists, but changed, set '-f' to overwrite it: $file"
                    echo "New hash: $hash"
                    # unlock the publishing
                    $rm_file $ws_lockfile
                    exit 1
                else
                    echo "WARNING: the file already exists, but changed, set '-f' to overwrite it: $file"
                    echo "New hash: $hash"
                    return
                fi
            fi
            hashes_old=$(grep '^Checksums-Sha256:' -A3 $packpath.saved | \
                grep " .* .* $file" | awk '{print $1}')
            # NOTE: for the single file name may exists more than one
            #       entry in damaged file, to fix it all found entries
            #       of this file need to be removed
            # find and remove all package blocks for the bad hashes
            for hash_rm in $hashes_old ; do
                echo "Removing from $packpath.saved file old hash: $hash_rm"
                sed -i '1s/^/\n/' $packpath.saved
                pcregrep -Mi -v "(?s)Package: (\N+\n)+(?=^ ${hash_rm}).*?^$" \
                    $packpath.saved >$packpath.saved_new
                mv $packpath.saved_new $packpath.saved
            done
        fi
        updated_dsc=1
    elif [ "$packtype" == "deb" ]; then
        # check if the DEB file already exists in old Packages file from S3
        # find the hash from the new Packages file
        hash=$(grep '^SHA256: ' $packpath | awk '{print $2}')
        # check if the file already exists in S3
        if $aws ls "$bucket_path/$packfile" ; then
            echo "WARNING: DEB file already exists in S3!"
            file_exists=$bucket_path/$packfile
        fi
        # search the new hash in the old Packages file from S3
        if grep "^SHA256: $hash" $packpath.saved ; then
            echo "WARNING: DEB file already registered in S3!"
            echo "File hash: $hash"
            if [ "$file_exists" != "" ] ; then
                return
            fi
        fi
        # check if the DEB file already exists in old Packages file from S3
        file=$(grep '^Filename:' $packpath | awk '{print $2}')
        if grep "Filename: $file$" $packpath.saved ; then
            if [ "$force" == "" -a "$file_exists" != "" ] ; then
                if [ "$skip_errors" == "" ] ; then
                    echo "ERROR: the file already exists, but changed, set '-f' to overwrite it: $file"
                    echo "New hash: $hash"
                    # unlock the publishing
                    $rm_file $ws_lockfile
                    exit 1
                else
                    echo "WARNING: the file already exists, but changed, set '-f' to overwrite it: $file"
                    echo "New hash: $hash"
                    return
                fi
            fi
            hashes_old=$(grep -e "^Filename: " -e "^SHA256: " $packpath.saved | \
                grep -A1 "$file" | grep "^SHA256: " | awk '{print $2}')
            # NOTE: for the single file name may exists more than one
            #       entry in damaged file, to fix it all found entries
            #       of this file need to be removed
            # find and remove all package blocks for the bad hashes
            for hash_rm in $hashes_old ; do
                echo "Removing from $packpath.saved file old hash: $hash_rm"
                sed -i '1s/^/\n/' $packpath.saved
                pcregrep -Mi -v "(?s)Package: (\N+\n)+(?=SHA256: ${hash_rm}).*?^$" \
                    $packpath.saved >$packpath.saved_new
                mv $packpath.saved_new $packpath.saved
            done
        fi
        updated_deb=1
    fi
    # store the new DEB entry
    cat $packpath >>$packpath.saved
}

# The 'pack_deb' function especialy created for DEB packages. It works
# with DEB packing OS like Ubuntu, Debian. It is based on globally known
# tool 'reprepro' from:
#     https://wiki.debian.org/DebianRepository/SetupWithReprepro
# This tool works with complete number of distributions of the given OS.
#
# The DEB packages structure must pass the documented instructions at
# at the Tarantool web site:
#   https://www.tarantool.io/en/download/os-installation/debian/
#   https://www.tarantool.io/en/download/os-installation/ubuntu/
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

    # set the subpath with binaries based on literal character of the product name
    proddir=$(echo $product | head -c 1)

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
SignWith: $GPG_SIGN_KEY
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
                update_deb_metadata $packages deb $locpackfile
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
            update_deb_metadata dists/$loop_dist/$component/source/Sources dsc \
                $locpackfile
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
            sed -i '/./,$!d' Packages
            bzip2 -c Packages >Packages.bz2
            gzip -c Packages >Packages.gz
            popd
        done

        # 2(sources). update Sources file archives
        pushd dists/$loop_dist/$component/source
        sed -i '/./,$!d' Sources
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
        gpg -u $GPG_SIGN_KEY -abs -o Release.gpg Release
        popd

        # 4. sync the latest distribution path changes to S3
        $aws_sync_public dists/$loop_dist "$bucket_path/dists/$loop_dist"
    done
}

# The 'pack_rpm' function especialy created for RPM packages. It works
# with RPM packing OS like CentOS, Fedora. It is based on globally known
# tool 'createrepo' from:
#   https://linux.die.net/man/8/createrepo
# This tool works with single distribution of the given OS.
#
# The RPM packages structure must pass the documented instructions at
# at the Tarantool web site:
#   https://www.tarantool.io/en/download/os-installation/rhel-centos/
#   https://www.tarantool.io/en/download/os-installation/fedora/
function pack_rpm {
    pack_subdir=$1
    pack_patterns=$2

    pack_rpms=$(cd $repo && ls $pack_patterns 2>/dev/null || true)
    if [ -z "$pack_rpms" ]; then
        echo "ERROR: Current '$repo' path doesn't have '$pack_patterns' packages in path"
        usage
        exit 1
    fi

    # copy the needed package binaries to the workspace
    ( cd $repo && cp $pack_rpms $ws/. )

    pushd $ws

    # set the paths
    repopath=$option_dist/$pack_subdir
    rpmpath=Packages
    packpath=$repopath/$rpmpath

    # prepare local repository with packages
    $mk_dir $packpath
    mv $pack_rpms $packpath/.
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
        file_exists=''
        name=$(zcat repodata/other.xml.gz | grep "<package pkgid=\"$hash\"" | \
            awk -F'"' '{print $4}')
        file=$(zcat repodata/primary.xml.gz | \
            grep -e "<checksum type=" -e "<location href=" | \
            grep "$hash" -A1 | grep "<location href=" | \
            awk -F'"' '{print $2}')
        # check if the file already exists in S3
        if $aws ls "$bucket_path/$repopath/$file" ; then
            echo "WARNING: DSC file already exists in S3!"
            file_exists=$bucket_path/$repopath/$file
        fi
        # search the new hash in the old meta file from S3
        if zcat repodata.base/filelists.xml.gz | grep "pkgid=\"$hash\"" | \
            grep "name=\"$name\"" ; then
            echo "WARNING: $name file already registered in S3!"
            echo "File hash: $hash"
            if [ "$file_exists" != "" ] ; then
                continue
            fi
        fi
        updated_rpms=1
        # check if the hashed file already exists in old meta file from S3
        if zcat repodata.base/primary.xml.gz | \
                grep "<location href=\"$file\"" ; then
            if [ "$force" == "" -a "$file_exists" != "" ] ; then
                if [ "$skip_errors" == "" ] ; then
                    echo "ERROR: the file already exists, but changed, set '-f' to overwrite it: $file"
                    echo "New hash: $hash"
                    # unlock the publishing
                    $rm_file $ws_lockfile
                    exit 1
                else
                    echo "WARNING: the file already exists, but changed, set '-f' to overwrite it: $file"
                    echo "New hash: $hash"
                    continue
                fi
            fi
            hashes_old=$(zcat repodata.base/primary.xml.gz | \
                grep -e "<checksum type=" -e "<location href=" | \
                grep -B1 "$file" | grep "<checksum type=" | \
                awk -F'>' '{print $2}' | sed 's#<.*##g')
            # NOTE: for the single file name may exists more than one
            #       entry in damaged file, to fix it all found entries
            #       of this file need to be removed
            for metafile in repodata.base/other \
                            repodata.base/filelists \
                            repodata.base/primary ; do
                up_lines=''
                if [ "$metafile" == "repodata.base/primary" ]; then
                    up_full_lines='(\N+\n)*'
                fi
                packs_rm=0
                zcat ${metafile}.xml.gz >${metafile}.xml
                # find and remove all <package> tags for the bad hashes
                for hash_rm in $hashes_old ; do
                    echo "Removing from ${metafile}.xml file old hash: $hash_rm"
                    sed -i '1s/^/\n/' ${metafile}.xml
                    pcregrep -Mi -v "(?s)<package ${up_full_lines}\N+(?=${hash_rm}).*?package>" \
                        ${metafile}.xml >${metafile}_tmp.xml
                    sed '/./,$!d' ${metafile}_tmp.xml >${metafile}.xml
                    rm ${metafile}_tmp.xml
                    packs_rm=$(($packs_rm+1))
                done
                # reduce number of packages in metafile counter
                packs=$(($(grep " packages=" ${metafile}.xml | \
                    sed 's#.* packages="\([0-9]*\)".*#\1#g')-${packs_rm}))
                sed "s# packages=\"[0-9]*\"# packages=\"${packs}\"#g" \
                    -i ${metafile}.xml
                gzip ${metafile}.xml
            done
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
    for file in $pack_rpms ; do
        $aws_cp_public $rpmpath/$file "$bucket_path/$repopath/$rpmpath/$file"
    done

    # update the metadata at the S3
    $aws_sync_public repodata "$bucket_path/$repopath/repodata"
}

if [ "$os" == "ubuntu" -o "$os" == "debian" ]; then
    # prepare the workspace
    prepare_ws ${os}
    pack_deb
    # unlock the publishing
    $rm_file $ws_lockfile
    popd
elif [ "$os" == "el" -o "$os" == "fedora" ]; then
    # RPM packages structure needs different paths for binaries and sources
    # packages, in this way it is needed to call the packages registering
    # script twice with the given format:
    # pack_rpm <packages store subpath> <patterns of the packages to register>

    # prepare the workspace
    prepare_ws ${os}_${option_dist}
    pack_rpm x86_64 "*.x86_64.rpm *.noarch.rpm"
    # unlock the publishing
    $rm_file $ws_lockfile
    popd

    # prepare the workspace
    prepare_ws ${os}_${option_dist}
    pack_rpm SRPMS "*.src.rpm"
    # unlock the publishing
    $rm_file $ws_lockfile
    popd
else
    echo "USAGE: given OS '$os' is not supported, use any single from the list: $alloss"
    usage
    exit 1
fi
