# Note: CMAKE_CXX_COMPILER needs to be set to the path of nvcc_wrapper
# nvcc_wrapper will choose either the Nvidia Cuda compiler or the OpenMP compiler depending on what's being compiled

SET(BUILD_HOMME_PREQX_KOKKOS TRUE CACHE BOOL "")
SET (WITH_PNETCDF FALSE CACHE FILEPATH "")

set (USE_NUM_PROCS 1 CACHE STRING "Num mpiprocs to use")
set (NETCDF_DIR $ENV{NETCDF_ROOT} CACHE FILEPATH "")
set (NetCDF_Fortran_PATH /ascldap/users/lbertag/workdir/libs/netcdf/netcdf-f/netcdf-f-install/waterman/gcc CACHE FILEPATH "")
set (HDF5_DIR $ENV{HDF5_ROOT} CACHE FILEPATH "")
set (ZLIB_DIR $ENV{ZLIB_ROOT} CACHE FILEPATH "")
set (CURL_ROOT $ENV{CURL_ROOT} CACHE FILEPATH "")
set (CURL_LIBRARY -L$ENV{CURL_ROOT}/lib -lcurl CACHE LIST "")

SET (HAVE_EXTRAE TRUE CACHE BOOL "")
SET (Extrae_LIBRARY "-L${NETCDF_DIR}/lib -L${NetCDF_Fortran_PATH}/lib -lnetcdff -lnetcdf -lhdf5_hl -lhdf5 -ldl -lz" CACHE STRING "")

SET(HOMME_FIND_BLASLAPACK TRUE CACHE BOOL "")
SET(USE_QUEUING FALSE CACHE BOOL "")
SET(ENABLE_CUDA TRUE CACHE BOOL "")
SET(USE_TRILINOS OFF CACHE BOOL "")

SET(CMAKE_C_COMPILER "mpicc" CACHE STRING "")
SET(CMAKE_Fortran_COMPILER "mpifort" CACHE STRING "")

set (ENABLE_COLUMN_OPENMP CACHE BOOL OFF)
set (ENABLE_HORIZ_OPENMP CACHE BOOL OFF)

set (USE_NUM_PROCS 8 CACHE STRING "")
