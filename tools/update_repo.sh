#!/bin/bash
set -ue

rm_file='rm -f'
rm_dir='rm -rf'
mk_dir='mkdir -p'
ws_prefix=/tmp/tarantool_repo_s3

alloss='ubuntu debian el fedora opensuse-leap'
product=tarantool
remove=
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
        alldists='trusty xenial bionic disco eoan focal groovy hirsute'
    elif [ "$os" == "debian" ]; then
        alldists='jessie stretch buster bullseye'
    elif [ "$os" == "el" ]; then
        alldists='6 7 8'
    elif [ "$os" == "fedora" ]; then
        alldists='27 28 29 30 31 32 33'
    elif [ "$os" == "opensuse-leap" ]; then
        alldists='15.0 15.1 15.2'
    fi

    echo "$alldists"
}

function prepare_ws {
    # temporary lock the publication to the repository
    ws_suffix=$1
    ws=${ws_prefix}_${ws_suffix}
    ws_lockfile=${ws}.lock
    old_proc=""
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

Usage for removing specific package from S3 repository:
    $0 -o=<OS name> -d=<OS distribuition> -b=<S3 bucket> [-p=<product>] -r <package version> <path to packages binaries>

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
         Product name to be packed with, default name is 'tarantool'.
	 Product name value may affect location of *.deb, *.rpm and
         related files relative to a base repository URL. It can be
         provided or not: the script will generate correct repository
         metainfo anyway.
         However providing meaningful value for this option enables
         grouping of related set of packages into a subdirectory
         named as '${product}' (only for Deb repositories at moment
         of writing this).
         It is enabled here for consistency with locations of other
         Deb packages in our repositories, but in fact it is the
         internal detail, which does not lead to any change in the
         user experience.
         Example of usage on 'tarantool-queue' product:
           - for DEB packages:
             ./$0 -b=s3://<target repository> -o=debian -d=stretch \\
               <DEB repository>/debian/pool/stretch/main/t/tarantool-queue \\
               -p=tarantool-queue
           - for RPM packages:
             # prepare local path with needed "tarantool-queue-*" packages only
             ./$0 -b=s3://<target repository> -o=fedora -d=30 <local path>
    -r|--remove
         Remove package specified by version from S3 repository. It will remove
         all found appropriate source and binaries packages from the given S3
         repository, also the meta files will be corrected there.
         Example of usage for DEB repositories on '2.2.2.0.<hash>' version:
           ./tools/update_repo.sh -o=<OS> -d=<DIST> -b=<S3 repo> \\
               -r=tarantool_2.2.2.0.<hash>
           It will search and try to remove packages:
             tarantool_2.2.2.0.<hash>-1_all.deb
             tarantool_2.2.2.0.<hash>-1_amd64.deb
             tarantool_2.2.2.0.<hash>-1.dsc
             tarantool_2.2.2.0.<hash>-1.debian.tar.xz
             tarantool_2.2.2.0.<hash>.orig.tar.xz
         Example of usage for RPM repositories on '2.2.2.0' version:
           ./tools/update_repo.sh -o=<OS> -d=<DIST> -b=<S3 repo> \\
               -r=tarantool-2.2.2.0
           It will search and try to remove packages:
             x86_64/tarantool-2.2.2.0-1.*.x86_64.rpm
             x86_64/tarantool-2.2.2.0-1.*.noarch.rpm
             SRPMS/tarantool-2.2.2.0-1.*.src.rpm
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
    -r=*|--remove=*)
    remove="${i#*=}"
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
    shift # past argument=value
    ;;
esac
done

if [ "$remove" == "" ]; then
    pushd $repo >/dev/null ; repo=$PWD ; popd >/dev/null
fi

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

    psources=dists/$loop_dist/$component/source/Sources

    # WORKAROUND: unknown why, but reprepro doesn`t save the Sources file,
    #             let`s recreate it manualy from it's zipped version
    gunzip -c $psources.gz >$psources

    # WORKAROUND: unknown why, but reprepro creates paths w/o distribution in
    #             it and no solution using configuration setup neither options
    #             found to set it there, let's set it here manually
    for packpath in dists/$loop_dist/$component/binary-* ; do
        if [ -f $packpath/Packages ]; then
            sed "s#Filename: $debdir/$component/#Filename: $debdir/$loop_dist/$component/#g" \
                -i $packpath/Packages
        fi
    done
    if [ -f $psources ]; then
        sed "s#Directory: $debdir/$component/#Directory: $debdir/$loop_dist/$component/#g" \
            -i $psources
    fi
}

function initiate_deb_metadata {
    distssuffix=$1

    # get the latest Packages/Sources files from S3 either create empty files
    mkdir -p `dirname $distssuffix` || :
    $aws ls "$bucket_path/$distssuffix" >/dev/null 2>&1 && \
        $aws cp "$bucket_path/$distssuffix" $distssuffix.saved || \
        touch $distssuffix.saved
}

function update_deb_metadata {
    packpath=$1
    packtype=$2
    packfile=$3

    file_exists=''

    # reinitiate if the new suffix used, not as used before
    [ -f $packpath.saved ] || initiate_deb_metadata $packpath

    [ "$remove" == "" ] || cp $packpath.saved $packpath

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

function update_deb_dists {
    # 2(binaries). update Packages file archives
    for packpath in dists/$loop_dist/$component/binary-* ; do
        pushd $packpath
        if [ -f Packages ]; then
            sed -i '/./,$!d' Packages
            bzip2 -c Packages >Packages.bz2
            gzip -c Packages >Packages.gz
        fi
        popd
    done

    # 2(sources). update Sources file archives
    pushd dists/$loop_dist/$component/source
    if [ -f Sources ]; then
        sed -i '/./,$!d' Sources
        bzip2 -c Sources >Sources.bz2
        gzip -c Sources >Sources.gz
    fi
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
        [ -f $file ] || continue
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
}

function remove_deb {
    pushd $ws

    # get Release file
    relpath=dists/$option_dist
    $mk_dir $relpath
    $aws cp $bucket_path/$relpath/Release $relpath/

    # get Packages files
    for bindirs in $($aws ls $bucket_path/dists/$option_dist/$component/ \
            | awk '{print $2}' | grep -v 'source/' | sed 's#/##g') ; do
        packpath=$relpath/$component/$bindirs
        $mk_dir $packpath
        pushd $packpath
        $aws cp $bucket_path/$packpath/Packages .

        hashes_old=$(grep -e "^Filename: " -e "^SHA256: " Packages | \
            grep -A1 "$remove" | grep "^SHA256: " | awk '{print $2}')
        # NOTE: for the single file name may exists more than one
        #       entry in damaged file, to fix it all found entries
        #       of this file need to be removed
        # find and remove all package blocks for the bad hashes
        for hash_rm in $hashes_old ; do
            echo "Removing from Packages file old hash: $hash_rm"
            sed -i '1s/^/\n/' Packages
            pcregrep -Mi -v "(?s)Package: (\N+\n)+(?=SHA256: ${hash_rm}).*?^$" \
                Packages >Packages.new
            mv Packages.new Packages
        done
        popd
    done

    # get Sources file
    packpath=$relpath/$component/source
    $mk_dir $packpath
    pushd $packpath
    $aws cp $bucket_path/$packpath/Sources .

    hashes_old=$(grep '^Checksums-Sha256:' -A3 Sources | \
        grep " .* .* $remove" | awk '{print $1}')
    # NOTE: for the single file name may exists more than one
    #       entry in damaged file, to fix it all found entries
    #       of this file need to be removed
    # find and remove all package blocks for the bad hashes
    for hash_rm in $hashes_old ; do
        echo "Removing from Sources file old hash: $hash_rm"
        sed -i '1s/^/\n/' Sources
        pcregrep -Mi -v "(?s)Package: (\N+\n)+(?=^ ${hash_rm}).*?^$" \
            Sources >Sources.new
        mv Sources.new Sources
    done
    popd

    # call DEB dists path updater
    loop_dist=$option_dist
    update_deb_dists

    # remove all found file by the given pattern in options
    for suffix in '-1_all.deb' '-1_amd64.deb' '-1.dsc' '-1.debian.tar.xz' '.orig.tar.xz' ; do
        file="$bucket_path/$poolpath/${remove}$suffix"
        echo "Searching to remove: $file"
        $aws ls "$file" || continue
        $aws rm "$file"
    done
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
    # copy single distribution with binaries packages
    file_found=0
    for file in $repo/*.deb $repo/*.dsc $repo/*.tar.*z ; do
        [ -f $file ] || continue
        file_found=1
        cp $file $repopath/.
    done
    # check that any files found
    if [ "$file_found" == "0" ]; then
        echo "ERROR: files $repo/*.deb $repo/*.dsc $repo/*.tar.*z not found"
        usage
        exit 1
    fi
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
        initiate_deb_metadata dists/$loop_dist/$component/binary-amd64/Packages
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
        initiate_deb_metadata dists/$loop_dist/$component/source/Sources
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
            [ ! -f $packages.saved ] || mv $packages.saved $packages
        done

        # finalize the Sources file
        sources=dists/$loop_dist/$component/source/Sources
        [ ! -f $sources.saved ] || mv $sources.saved $sources

        # call DEB dists path updater
        update_deb_dists
    done
}

# The 'pack_rpm' function especialy created for RPM packages. It works
# with RPM packing OS like CentOS, Fedora. It is based on globally known
# tool 'createrepo' from:
#   https://github.com/rpm-software-management/createrepo_c.git
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
    [ -n "$pack_rpms" ] || return 0
    packed_rpms=pack_rpms

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
    createrepo_c --no-database --update --workers=2 \
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
                up_full_lines=''
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
                gzip -f ${metafile}.xml
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

function remove_rpm {
    pack_subdir=$1
    pack_patterns=$2

    pushd $ws

    # set the paths
    repopath=$option_dist/$pack_subdir
    rpmpath=Packages
    packpath=$repopath/$rpmpath

    # prepare local repository with packages
    $mk_dir $packpath
    cd $repopath

    # copy the current metadata files from S3
    mkdir repodata
    pushd repodata
    for file in $($aws ls $bucket_path/$repopath/repodata/ | awk '{print $NF}') ; do
        $aws ls $bucket_path/$repopath/repodata/$file || continue
        $aws cp $bucket_path/$repopath/repodata/$file $file
        [[ ! $file =~ .*.gz ]] || gunzip $file
    done

    updated_rpms=0
    # loop by the new hashes from the new meta file
    for hash in $(grep "<package pkgid=" other.xml | awk -F'"' '{print $2}') ; do
        updated_rpm=0
        file_exists=''
        name=$(grep "<package pkgid=\"$hash\"" other.xml | awk -F'"' '{print $4}')
        file=$(grep -e "<checksum type=" -e "<location href=" primary.xml | \
            grep "$hash" -A1 | grep "<location href=" | \
            awk -F'"' '{print $2}')
        for pack_pattern in $pack_patterns ; do
            [[ $file =~ Packages/$remove$pack_pattern ]] || continue

            # check if the file already exists in S3
            ! $aws ls "$bucket_path/$repopath/$file" || \
                $aws rm "$bucket_path/$repopath/$file"

            # search the new hash in the old meta file from S3
            grep "pkgid=\"$hash\"" filelists.xml | grep "name=\"$name\"" || continue
            updated_rpms=1
            hashes_old=$(grep -e "<checksum type=" -e "<location href=" primary.xml | \
                grep -B1 "$file" | grep "<checksum type=" | \
                awk -F'>' '{print $2}' | sed 's#<.*##g')
            # NOTE: for the single file name may exists more than one
            #       entry in damaged file, to fix it all found entries
            #       of this file need to be removed
            for metafile in other filelists primary ; do
                up_full_lines=''
                if [ "$metafile" == "primary" ]; then
                    up_full_lines='(\N+\n)*'
                fi
                packs_rm=0
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
            done
        done
    done

    # Remove all files found by the given pattern in the options.
    # The loop above already delete files, but only which were
    # presented in the metadata. However it is possible that some
    # broken update left orphan files: they are present in the
    # storage, but does not mentioned in the metadata.
    for suffix in 'x86_64' 'noarch' 'src'; do
        if [ "$os" == "opensuse-leap" ]; then
            # Open Build Service (openSUSE) does not follow the usual
            # approach: 'Release' is like lp152.1.1, where the first
            # '1' is $(RELEASE) RPM spec directive value and the second
            # '1' is the number of rebuilds.
            os_dist="lp$(echo $option_dist | sed 's#\.##g').1.1"
        elif [ "$os" == "fedora" ]; then
            os_dist="1.fc${option_dist}"
        else
            os_dist="1.${os}${option_dist}"
        fi
        file="$bucket_path/$packpath/${remove}-${os_dist}.${suffix}.rpm"
        echo "Searching to remove: $file"
        $aws ls $file || continue
        $aws rm $file
    done

    # check if any RPM files were newly registered
    [ "$updated_rpms" == "0" ] && \
        return || echo "Updating dists"

    # merge metadata files
    mv repomd.xml repomd_saved.xml
    head -n 2 repomd_saved.xml >repomd.xml
    for file in filelists.xml other.xml primary.xml ; do
        # get the new data
        chsnew=$(sha256sum $file | awk '{print $1}')
        sz=$(stat --printf="%s" $file)
        gzip $file
        chsgznew=$(sha256sum $file.gz | awk '{print $1}')
        szgz=$(stat --printf="%s" $file.gz)
        timestamp=$(date +%s -r $file.gz)

        # add info to repomd.xml file
        name=$(echo $file | sed 's#\.xml$##g')
        cat <<EOF >>repomd.xml
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
    tail -n 1 repomd_saved.xml >>repomd.xml
    rm -f repomd_saved.xml repomd.xml.asc
    popd
    gpg --detach-sign --armor repodata/repomd.xml

    # update the metadata at the S3
    $aws_sync_public repodata "$bucket_path/$repopath/repodata"
}

if [ "$os" == "ubuntu" -o "$os" == "debian" ]; then
    # prepare the workspace
    prepare_ws ${os}

    # we need to push packages into 'main' repository only
    component=main

    # debian has special directory 'pool' for packages
    debdir=pool

    # set the subpath with binaries based on literal character of the product name
    proddir=$(echo $product | head -c 1)

    # copy single distribution with binaries packages
    poolpath=pool/${option_dist}/$component/$proddir/$product
    repopath=$ws/$poolpath
    $mk_dir ${repopath}

    if [ "$remove" != "" ]; then
        remove_deb
    else
        pack_deb
    fi

    # unlock the publishing
    $rm_file $ws_lockfile
    popd
elif [ "$os" == "el" -o "$os" == "fedora" -o "$os" == "opensuse-leap" ]; then
    # RPM packages structure needs different paths for binaries and sources
    # packages, in this way it is needed to call the packages registering
    # script twice with the given format:
    # pack_rpm <packages store subpath> <patterns of the packages to register>

    packed_rpms=""
    # prepare the workspace
    prepare_ws ${os}_${option_dist}
    if [ "$remove" != "" ]; then
        remove_rpm x86_64 "-1.*.x86_64.rpm -1.*.noarch.rpm"
    else
        pack_rpm x86_64 "*.x86_64.rpm *.noarch.rpm"
    fi
    # unlock the publishing
    $rm_file $ws_lockfile
    popd 2>/dev/null || true

    # prepare the workspace
    prepare_ws ${os}_${option_dist}
    if [ "$remove" != "" ]; then
        remove_rpm SRPMS "-1.*.src.rpm"
    else
        pack_rpm SRPMS "*.src.rpm"
    fi
    # unlock the publishing
    $rm_file $ws_lockfile
    popd 2>/dev/null || true

    if [ "$remove" == "" -a "$packed_rpms" == "" ]; then
        echo "ERROR: Current '$repo' path doesn't have '*.x86_64.rpm *.noarch.rpm *.src.rpm' packages in path"
        usage
        exit 1
    fi
else
    echo "USAGE: given OS '$os' is not supported, use any single from the list: $alloss"
    usage
    exit 1
fi
