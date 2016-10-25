project=cutils

#expected build dir structure
buildexp=build/Unix_Makefiles

currentdir=$PWD
builddir=./${buildexp}

mkdir -p ${builddir}

#launch cmake to generate build environment
pushd ${builddir}
cmake -G "Unix Makefiles" ${currentdir}
popd

#build from generated build environment
cmake --build ${builddir}

