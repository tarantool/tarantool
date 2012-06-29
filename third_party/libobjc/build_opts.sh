#!/bin/sh
LLVM_PATH=`llvm-config --src-root`
LIBOBJC_PATH=`pwd`
if [ x$LLVM_PATH != x ] ; then
	if [ -d $LLVM_PATH ] ; then
		cd $LLVM_PATH
		cd lib/Transforms
		if [ ! -d GNURuntime ] ; then
			mkdir GNURuntime
		fi
		cd GNURuntime
		for I in `ls $LIBOBJC_PATH/opts/` ; do
			if [ ! $I -nt $LIBOBJC_PATH/opts/$I ] ; then
				cp $LIBOBJC_PATH/opts/$I .
			fi
		done
		$1 $2
		cd ..
	fi
fi
