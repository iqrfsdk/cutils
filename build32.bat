set project=cutils

rem //expected build dir structure
set buildexp=build\\Visual_Studio_12_2013\\x86

set currentdir=%cd%
set builddir=.\\%buildexp%

mkdir %builddir%

rem //launch cmake to generate build environment
pushd %builddir%
cmake -G "Visual Studio 12 2013" %currentdir%
popd

rem //build from generated build environment
cmake --build %builddir%
