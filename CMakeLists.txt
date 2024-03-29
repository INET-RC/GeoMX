cmake_minimum_required(VERSION 3.0.2)

project(mxnet C CXX)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/build/private/local_config.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/build/private/local_config.cmake)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Utils.cmake)

#Some things have order. This must be put in front alone
mxnet_option(USE_CUDA             "Build with CUDA support"   ON)
mxnet_option(USE_OLDCMAKECUDA     "Build with old cmake cuda" OFF)
mxnet_option(USE_NCCL             "Use NVidia NCCL with CUDA" OFF)
mxnet_option(USE_OPENCV           "Build with OpenCV support" ON)
mxnet_option(USE_OPENMP           "Build with Openmp support" ON)
mxnet_option(USE_CUDNN            "Build with cudnn support"  ON) # one could set CUDNN_ROOT for search path
mxnet_option(USE_SSE              "Build with x86 SSE instruction support" ON IF NOT ARM)
mxnet_option(USE_F16C             "Build with x86 F16C instruction support" ON) # autodetects support if ON
mxnet_option(USE_LAPACK           "Build with lapack support" ON)
mxnet_option(USE_MKL_IF_AVAILABLE "Use MKL if found" ON)
mxnet_option(USE_MKLML_MKL        "Use MKLDNN variant of MKL (if MKL found)" ON IF USE_MKL_IF_AVAILABLE AND (NOT APPLE))
mxnet_option(USE_MKLDNN           "Use MKLDNN variant of MKL (if MKL found)" ON IF USE_MKL_IF_AVAILABLE AND (NOT APPLE))
mxnet_option(USE_OPERATOR_TUNING  "Enable auto-tuning of operators" ON IF NOT MSVC)
mxnet_option(USE_GPERFTOOLS       "Build with GPerfTools support (if found)" ON)
mxnet_option(USE_JEMALLOC         "Build with Jemalloc support"   ON)
mxnet_option(USE_PROFILER         "Build with Profiler support"   ON)
mxnet_option(USE_DIST_KVSTORE     "Build with DIST_KVSTORE support" OFF)
mxnet_option(USE_PLUGINS_WARPCTC  "Use WARPCTC Plugins" OFF)
mxnet_option(USE_PLUGIN_CAFFE     "Use Caffe Plugin" OFF)
mxnet_option(USE_CPP_PACKAGE      "Build C++ Package" OFF)
mxnet_option(USE_MXNET_LIB_NAMING "Use MXNet library naming conventions." ON)
mxnet_option(USE_GPROF            "Compile with gprof (profiling) flag" OFF)
mxnet_option(USE_CXX14_IF_AVAILABLE "Build with C++14 if the compiler supports it" OFF)
mxnet_option(USE_VTUNE            "Enable use of Intel Amplifier XE (VTune)" OFF) # one could set VTUNE_ROOT for search path
mxnet_option(ENABLE_CUDA_RTC      "Build with CUDA runtime compilation support" ON)
mxnet_option(BUILD_CPP_EXAMPLES   "Build cpp examples" ON)
mxnet_option(INSTALL_EXAMPLES     "Install the example source files." OFF)
mxnet_option(USE_SIGNAL_HANDLER   "Print stack traces on segfaults." OFF)
mxnet_option(USE_TENSORRT         "Enable infeference optimization with TensorRT." OFF)
mxnet_option(USE_ASAN             "Enable Clang/GCC ASAN sanitizers." OFF)
mxnet_option(ENABLE_TESTCOVERAGE  "Enable compilation with test coverage metric output" OFF)

message(STATUS "CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}")
if(USE_CUDA AND NOT USE_OLDCMAKECUDA)
  message(STATUS "CMake version '${CMAKE_VERSION}' using generator '${CMAKE_GENERATOR}'")
  if(
      (
        (${CMAKE_GENERATOR} MATCHES "Visual Studio.*")
        OR (${CMAKE_GENERATOR} MATCHES "Xcode.*")
        OR (${CMAKE_GENERATOR} STREQUAL "Unix Makefiles")
      ) AND (
        (${CMAKE_VERSION} VERSION_GREATER "3.9.0") OR (${CMAKE_VERSION} VERSION_EQUAL "3.9.0")
      )
    )
    set(FIRST_CUDA TRUE)
    project(mxnet C CXX CUDA)
  else()
    set(FIRST_CUDA FALSE)
    set(USE_OLDCMAKECUDA TRUE)
    project(mxnet C CXX)
  endif()
else()
  project(mxnet C CXX)
endif()


if(MSVC)
  set(SYSTEM_ARCHITECTURE x86_64)
else()
  execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE SYSTEM_ARCHITECTURE)
endif()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules;${CMAKE_MODULE_PATH}")

SET(EXTRA_OPERATORS "" CACHE PATH "EXTRA OPERATORS PATH")

if("$ENV{VERBOSE}" STREQUAL "1")
  message(STATUS " Verbose Makefile ACTIVATED")
  set(CMAKE_VERBOSE_MAKEFILE ON)
endif()


if(MSVC)
  add_definitions(-DWIN32_LEAN_AND_MEAN)
  add_definitions(-DDMLC_USE_CXX11)
  add_definitions(-DMSHADOW_IN_CXX11)
  add_definitions(-D_SCL_SECURE_NO_WARNINGS)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-DMXNET_EXPORTS)
  add_definitions(-DNNVM_EXPORTS)
  add_definitions(-DDMLC_STRICT_CXX11)
  add_definitions(-DNOMINMAX)
  if(USE_F16C)
    message("F16C instruction set is not yet supported for MSVC")
  endif()
  set(CMAKE_C_FLAGS "/MP")
  set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} /bigobj")
else(MSVC)
  include(CheckCXXCompilerFlag)
  if(USE_CXX14_IF_AVAILABLE)
    check_cxx_compiler_flag("-std=c++14" SUPPORT_CXX14)
  endif()
  check_cxx_compiler_flag("-std=c++11"   SUPPORT_CXX11)
  check_cxx_compiler_flag("-std=c++0x"   SUPPORT_CXX0X)
  # For cross compilation, we can't rely on the compiler which accepts the flag, but mshadow will
  # add platform specific includes not available in other arches
  if(USE_SSE)
    check_cxx_compiler_flag("-msse2"     SUPPORT_MSSE2)
  else()
    set(SUPPORT_MSSE2 FALSE)
  endif()
  # For cross complication, turn off flag if target device does not support it
  if(USE_F16C)
    # Determine if hardware supports F16C instruction set
    message(STATUS "Determining F16C support")
    include(cmake/AutoDetectF16C.cmake)
  else()
    set(SUPPORT_F16C FALSE)
  endif()
  if(SUPPORT_F16C)
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -mf16c")
  else()
    add_definitions(-DMSHADOW_USE_F16C=0)
  endif()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CMAKE_C_FLAGS "-Wall -Wno-unknown-pragmas -Wno-sign-compare")
  if ("${CMAKE_CXX_COMPILER_ID}" MATCHES ".*Clang$")
    set(CMAKE_C_FLAGS "-Wno-braced-scalar-init")
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_definitions(-DNDEBUG=1)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g")
  else()
    add_definitions(-DNDEBUG=1)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
  endif()
  if(SUPPORT_MSSE2)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
  endif()
  set(CMAKE_CXX_FLAGS ${CMAKE_C_FLAGS})
  if(SUPPORT_CXX14)
    add_definitions(-DDMLC_USE_CXX11=1)
    add_definitions(-DDMLC_USE_CXX14=1)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
  elseif(SUPPORT_CXX11)
    add_definitions(-DDMLC_USE_CXX11=1)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
  elseif(SUPPORT_CXX0X)
    add_definitions(-DDMLC_USE_CXX11=1)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
  endif()
endif(MSVC)

if(NOT mxnet_LINKER_LIBS)
  set(mxnet_LINKER_LIBS "")
endif(NOT mxnet_LINKER_LIBS)

if(USE_GPROF)
  message(STATUS "Using GPROF")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -g -pg")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -g -pg")
  set(CMAKE_LINK_LIBRARY_FILE_FLAG "${CMAKE_LINK_LIBRARY_FILE_FLAG} -g -pg")
endif()

if(USE_VTUNE)
  message(STATUS "Using VTUNE")
  if(NOT VTUNE_ROOT)
    set(VTUNE_ROOT /opt/intel/vtune_amplifier_xe_2017)
  endif()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -g -pg")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -g -pg")
  set(CMAKE_LINK_LIBRARY_FILE_FLAG "${CMAKE_LINK_LIBRARY_FILE_FLAG} -g -pg")
  add_definitions(-DMXNET_USE_VTUNE=1)
  include_directories(${VTUNE_ROOT}/include)
  list(APPEND mxnet_LINKER_LIBS ${VTUNE_ROOT}/lib64/libittnotify.a)
  list(APPEND mxnet_LINKER_LIBS dl)
endif()

if(USE_TENSORRT)
  message(STATUS "Using TensorRT")
  set(ONNX_PATH 3rdparty/onnx-tensorrt/third_party/onnx/build/)
  set(ONNX_TRT_PATH 3rdparty/onnx-tensorrt/build/)

  include_directories(${ONNX_PATH})
  include_directories(3rdparty/onnx-tensorrt/)
  include_directories(3rdparty/)
  include_directories(3rdparty/onnx-tensorrt/third_party/onnx/)
  add_definitions(-DMXNET_USE_TENSORRT=1)
  add_definitions(-DONNX_NAMESPACE=onnx)

  find_package(Protobuf REQUIRED)

  find_library(ONNX_LIBRARY NAMES libonnx.so REQUIRED
          PATHS ${ONNX_PATH}
          DOC "Path to onnx library.")
  find_library(ONNX_PROTO_LIBRARY NAMES libonnx_proto.so REQUIRED
          PATHS ${ONNX_PATH}
          DOC "Path to onnx_proto library.")
  find_library(ONNX_TRT_RUNTIME_LIBRARY NAMES libnvonnxparser_runtime.so REQUIRED
          PATHS ${ONNX_TRT_PATH}
          DOC "Path to onnx_proto library.")
  find_library(ONNX_TRT_PARSER_LIBRARY NAMES libnvonnxparser.so REQUIRED
          PATHS ${ONNX_TRT_PATH}
          DOC "Path to onnx_proto library.")

  list(APPEND mxnet_LINKER_LIBS libnvinfer.so ${ONNX_TRT_PARSER_LIBRARY} ${ONNX_TRT_RUNTIME_LIBRARY}
          ${ONNX_PROTO_LIBRARY} ${ONNX_LIBRARY} ${PROTOBUF_LIBRARY})
endif()

if(ENABLE_TESTCOVERAGE)
  message(STATUS "Compiling with test coverage support enabled. This will result in additional files being written to your source directory!")
  find_program( GCOV_PATH gcov )
  if(NOT GCOV_PATH)
    message(FATAL_ERROR "gcov not found! Aborting...")
  endif() # NOT GCOV_PATH
  
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} --coverage")
  set(GTEST_LIBRARIES "${GTEST_LIBRARIES} --coverage")
  link_libraries(gcov)
endif()

if(USE_MKLDNN)
  include(cmake/DownloadMKLML.cmake)
  # CPU architecture (e.g., C5) can't run on another architecture (e.g., g3).
  if(NOT MSVC)
    set(ARCH_OPT_FLAGS "-mtune=generic")
  else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /EHsc")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /EHsc /Gy")
  endif()

  set(WITH_TEST OFF CACHE INTERNAL "" FORCE)
  set(WITH_EXAMPLE OFF CACHE INTERNAL "" FORCE)
  set(ARCH_OPT_FLAGS "" CACHE INTERNAL "" FORCE)

  add_subdirectory(3rdparty/mkldnn)

  include_directories(3rdparty/mkldnn/include)
  add_definitions(-DUSE_MKL=1)
  add_definitions(-DCUB_MKL=1)
  add_definitions(-DMXNET_USE_MKLDNN=1)
  list(APPEND mxnet_LINKER_LIBS mkldnn)
endif()

# Allow Cuda compiles outside of src tree to find things in 'src' and 'include'
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

if(USE_CUDA)
  find_package(CUDA REQUIRED)
  add_definitions(-DMSHADOW_USE_CUDA=1)
  if(FIRST_CUDA AND (NOT USE_OLDCMAKECUDA))
    if(NOT CUDA_TOOLSET)
      set(CUDA_TOOLSET "${CUDA_VERSION_STRING}")
    endif()
  else()
    set(FIRST_CUDA FALSE)
  endif()
  if(USE_NCCL)
    find_package(NCCL)
    if(NCCL_FOUND)
      include_directories(${NCCL_INCLUDE_DIRS})
      list(APPEND mxnet_LINKER_LIBS ${NCCL_LIBRARIES})
    else()
      message(WARNING "Could not find NCCL libraries")
    endif()
  endif()
else()
  add_definitions(-DMSHADOW_USE_CUDA=0)
endif()

if(NCCL_FOUND)
  add_definitions(-DMXNET_USE_NCCL=1)
else()
  add_definitions(-DMXNET_USE_NCCL=0)
endif()

if(USE_CUDA AND FIRST_CUDA)
  include(cmake/ChooseBlas.cmake)
  include(3rdparty/mshadow/cmake/Utils.cmake)
  include(cmake/FirstClassLangCuda.cmake)
  include_directories(${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
else()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mshadow/cmake)
    include(3rdparty/mshadow/cmake/mshadow.cmake)
    include(3rdparty/mshadow/cmake/Utils.cmake)
    include(3rdparty/mshadow/cmake/Cuda.cmake)
  else()
    include(mshadowUtils)
    include(Cuda)
    include(mshadow)
  endif()
endif()

if(USE_ASAN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
  set(GTEST_LIBRARIES "${GTEST_LIBRARIES} -fsanitize=address")
  list(APPEND mxnet_LINKER_LIBS asan)
endif()

list(APPEND mxnet_LINKER_LIBS ${mshadow_LINKER_LIBS})

foreach(var ${C_CXX_INCLUDE_DIRECTORIES})
    include_directories(${var})
endforeach()

include_directories("include")
include_directories("3rdparty/mshadow")
include_directories("3rdparty/cub")
include_directories("3rdparty/tvm/nnvm/include")
include_directories("3rdparty/tvm/include")
include_directories("3rdparty/dmlc-core/include")
include_directories("3rdparty/dlpack/include")

# commented out until PR goes through
#if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dlpack)
#  add_subdirectory(3rdparty/dlpack)
#endif()

# Prevent stripping out symbols (operator registrations, for example)
if(NOT MSVC AND NOT APPLE)
  set(BEGIN_WHOLE_ARCHIVE -Wl,--whole-archive)
  set(END_WHOLE_ARCHIVE -Wl,--no-whole-archive)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # using regular Clang or AppleClang
  set(BEGIN_WHOLE_ARCHIVE -Wl,-force_load)
endif()

if(UNIX)
  find_library(RTLIB rt)
  if(RTLIB)
    list(APPEND mxnet_LINKER_LIBS ${RTLIB})
  endif()
endif()

set(ALT_MALLOC_FLAGS "-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free")

# ---[ gperftools
if(USE_GPERFTOOLS)
  find_package(Gperftools)
  if(GPERFTOOLS_FOUND)
    message(STATUS "Using Gperftools malloc (tcmalloc)")
    include_directories(${GPERFTOOLS_INCLUDE_DIR})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ALT_MALLOC_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ALT_MALLOC_FLAGS}")
    set(mxnet_LINKER_LIBS ${mxnet_LINKER_LIBS} ${GPERFTOOLS_LIBRARIES})
    set(USE_JEMALLOC 0)
  endif()
endif()

# ---[ jemalloc
if(USE_JEMALLOC)
  if(GPERFTOOLS_FOUND)
    message(ERROR " Only one of USE_JEMALLOC and USE_GPERFTOOLS can be defined at once")
  endif()
  find_package(JeMalloc)
  if(JEMALLOC_FOUND)
    message(STATUS "Using JEMalloc malloc")
    add_definitions(-DUSE_JEMALLOC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ALT_MALLOC_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ALT_MALLOC_FLAGS}")
    include_directories(${JEMALLOC_INCLUDE_DIRS})
    set(mxnet_LINKER_LIBS ${mxnet_LINKER_LIBS} ${JEMALLOC_LIBRARIES})
  endif()
endif()

# ---[ OpenCV
if(USE_OPENCV)
  find_package(OpenCV COMPONENTS core highgui imgproc imgcodecs)
  if(NOT OpenCV_FOUND) # if not OpenCV 3.x, then imgcodecs are not found
    message(STATUS "OpenCV imgcodecs missing")
    find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc)
  endif()
  include_directories(SYSTEM ${OpenCV_INCLUDE_DIRS})
  list(APPEND mxnet_LINKER_LIBS ${OpenCV_LIBS})
  message(STATUS " OpenCV_LIBS=${OpenCV_LIBS}")
  message(STATUS "OpenCV ${OpenCV_VERSION} found (${OpenCV_CONFIG_PATH})")
  add_definitions(-DMXNET_USE_OPENCV=1)
else(USE_OPENCV)
  message(STATUS "OpenCV Disabled")
  add_definitions(-DMXNET_USE_OPENCV=0)
endif()

# ---[ OpenMP
if(USE_OPENMP)
  find_package(OpenMP REQUIRED)
  # This should build on Windows, but there's some problem and I don't have a Windows box, so
  # could a Windows user please fix?
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/openmp/CMakeLists.txt
     AND SYSTEM_ARCHITECTURE STREQUAL "x86_64"
     AND NOT MSVC
     AND NOT CMAKE_CROSSCOMPILING)

    # Intel/llvm OpenMP: https://github.com/llvm-mirror/openmp
    set(OPENMP_STANDALONE_BUILD TRUE)
    set(LIBOMP_ENABLE_SHARED TRUE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/openmp)
    list(REMOVE_ITEM mxnet_LINKER_LIBS iomp5)
    list(APPEND mxnet_LINKER_LIBS omp)
    if(UNIX)
      list(APPEND mxnet_LINKER_LIBS pthread)
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
  else()
    if(OPENMP_FOUND)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    endif()
  endif()
elseif(UNIX AND NOT ANDROID)
  list(APPEND mxnet_LINKER_LIBS pthread)
endif()


# ---[ LAPack
if(USE_LAPACK)
  message("USE_LAPACK is ON")
  add_definitions(-DMXNET_USE_LAPACK=1)
  if (NOT MSVC)
    list(APPEND mxnet_LINKER_LIBS lapack)
  endif()
endif()


# ---[ jemalloc
if(USE_JEMALLOC)
  find_package(JeMalloc)
  if(JEMALLOC_FOUND)
    add_definitions(-DUSE_JEMALLOC)
    include_directories(${JEMALLOC_INCLUDE_DIRS})
    set(mxnet_LINKER_LIBS ${mxnet_LINKER_LIBS} ${JEMALLOC_LIBRARIES})
  endif()
endif()

include(CTest)
set(GTEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/googletest/googletest")
set(GTEST_INCLUDE_DIR ${GTEST_ROOT}/include)
#set(GTEST_BOTH_LIBRARIES gtest gtest_main)
set(GTEST_LIBRARIES gtest gtest_main)
set(GTEST_MAIN_LIBRARY gtest_main)
set(GTEST_LIBRARY gtest)

add_subdirectory(${GTEST_ROOT})
find_package(GTest REQUIRED)

# cudnn detection
if(USE_CUDNN AND USE_CUDA)
  detect_cuDNN()
  if(HAVE_CUDNN)
    add_definitions(-DUSE_CUDNN)
    include_directories(SYSTEM ${CUDNN_INCLUDE})
    list(APPEND mxnet_LINKER_LIBS ${CUDNN_LIBRARY})
      add_definitions(-DMSHADOW_USE_CUDNN=1)
  endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dmlc-core/cmake)
  add_subdirectory("3rdparty/dmlc-core")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mshadow/cmake)
  add_subdirectory("3rdparty/mshadow")
endif()
FILE(GLOB_RECURSE SOURCE "src/*.cc" "src/*.h" "include/*.h")
FILE(GLOB_RECURSE CUDA "src/*.cu" "src/*.cuh")

# add nnvm to source
FILE(GLOB_RECURSE NNVMSOURCE
  3rdparty/tvm/nnvm/src/c_api/*.cc
  3rdparty/tvm/nnvm/src/core/*.cc
  3rdparty/tvm/nnvm/src/pass/*.cc
  3rdparty/tvm/nnvm/src/c_api/*.h
  3rdparty/tvm/nnvm/src/core/*.h
  3rdparty/tvm/nnvm/src/pass/*.h
  3rdparty/tvm/nnvm/include/*.h)
list(APPEND SOURCE ${NNVMSOURCE})

# add mshadow file
FILE(GLOB_RECURSE MSHADOWSOURCE "3rdparty/mshadow/mshadow/*.h")
FILE(GLOB_RECURSE MSHADOW_CUDASOURCE "3rdparty/mshadow/mshadow/*.cuh")
list(APPEND SOURCE ${MSHADOWSOURCE})
list(APPEND CUDA ${MSHADOW_CUDASOURCE})

# add source group
FILE(GLOB_RECURSE GROUP_SOURCE "src/*.cc" "3rdparty/tvm/nnvm/*.cc" "plugin/*.cc")
FILE(GLOB_RECURSE GROUP_Include "src/*.h" "3rdparty/tvm/nnvm/*.h" "3rdparty/mshadow/mshadow/*.h" "plugin/*.h")
FILE(GLOB_RECURSE GROUP_CUDA "src/*.cu" "src/*.cuh" "3rdparty/mshadow/mshadow/*.cuh" "plugin/*.cu"
  "plugin/*.cuh" "3rdparty/cub/cub/*.cuh")
assign_source_group("Source" ${GROUP_SOURCE})
assign_source_group("Include" ${GROUP_Include})
assign_source_group("CUDA" ${GROUP_CUDA})

if(USE_PLUGINS_WARPCTC)
    set(WARPCTC_INCLUDE  "" CACHE PATH "WARPCTC include")
    set(WARPCTC_LIB_DEBUG  "" CACHE FILEPATH "WARPCTC lib")
    set(WARPCTC_LIB_RELEASE  "" CACHE FILEPATH "WARPCTC lib")
    include_directories(SYSTEM ${WARPCTC_INCLUDE})
    list(APPEND mxnet_LINKER_LIBS ${WARPCTC_LIB})
    FILE(GLOB_RECURSE PLUGINS_SOURCE "plugin/warpctc/*.cc" "plugin/warpctc/*.h")
    FILE(GLOB_RECURSE PLUGINS_CUSRC "plugin/warpctc/*.cu")
    list(APPEND SOURCE ${PLUGINS_SOURCE})
    list(APPEND CUDA ${PLUGINS_CUSRC})
endif()

if(USE_OPERATOR_TUNING AND USE_OPENMP)
  add_definitions(-DMXNET_USE_OPERATOR_TUNING=1)
endif()

if(USE_PLUGIN_CAFFE)
  if(NOT USE_CUDA)
    set(CPU_ONLY ON)
    add_definitions(-DCPU_ONLY=1)
  endif()
  if(NOT DEFINED CAFFE_PATH)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/caffe)
      # Need newer FindCUDA.cmake that correctly handles -std=c++11
      cmake_minimum_required(VERSION 3.3)
      set(CAFFE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/caffe)
    else()
      set(CAFFE_PATH $ENV{CAFFE_PATH})
    endif()
  endif()
  list(APPEND CMAKE_MODULE_PATH ${CAFFE_PATH}/cmake)
  include_directories(${CAFFE_PATH}/include)
  include_directories(${CAFFE_PATH}/build/src)
  include_directories(${CMAKE_BINARY_DIR}/caffe/include)
  link_directories(${CAFFE_PATH}/build/lib)
  if(NOT DEFINED CAFFE_PATH)
    message(FATAL_ERROR "Please set CAFFE_PATH to point to the caffe source installation")
  endif()
  FILE(GLOB_RECURSE PLUGINS_SOURCE "plugin/caffe/*.cc" "plugin/caffe/*.h")
  FILE(GLOB_RECURSE PLUGINS_CUSRC "plugin/caffe/*.cu")
  list(APPEND SOURCE ${PLUGINS_SOURCE})
  list(APPEND CUDA ${PLUGINS_CUSRC})
  include_directories(${CMAKE_BINARY_DIR}/include)
  add_definitions(-DMXNET_USE_CAFFE=1)
  list(APPEND mxnet_LINKER_LIBS
    protobuf boost_system boost_thread boost_filesystem
    gflags glog caffe
    ${Caffe_LINKER_LIBS}
)
endif()

if (NOT (EXTRA_OPERATORS STREQUAL ""))
    mxnet_source_group("Extra"   GLOB_RECURSE "${EXTRA_OPERATORS}/*.cc")
    mxnet_source_group("Extra\\Cuda"   GLOB_RECURSE "${EXTRA_OPERATORS}/*.cu")
    FILE(GLOB_RECURSE EXTRA_SRC "${EXTRA_OPERATORS}/*.cc")
    FILE(GLOB_RECURSE EXTRA_CUSRC "${EXTRA_OPERATORS}/*.cu")
    list(APPEND SOURCE ${EXTRA_SRC} ${EXTRA_CUSRC})
endif()

if(MSVC)
  foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    if(${flag_var} MATCHES "/MD")
      string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endif(${flag_var} MATCHES "/MD")
  endforeach(flag_var)
endif()

if(USE_CUDA)
  if(FIRST_CUDA)
    mshadow_select_nvcc_arch_flags(NVCC_FLAGS_ARCH)
    string(REPLACE ";" " " NVCC_FLAGS_ARCH "${NVCC_FLAGS_ARCH}")
    set(CMAKE_CUDA_FLAGS "${NVCC_FLAGS_ARCH}")
    list(APPEND mxnet_LINKER_LIBS cublas cufft cusolver curand)
    if(ENABLE_CUDA_RTC)
        list(APPEND mxnet_LINKER_LIBS nvrtc cuda)
        add_definitions(-DMXNET_ENABLE_CUDA_RTC=1)
    endif()
    list(APPEND SOURCE ${CUDA})
    add_definitions(-DMXNET_USE_CUDA=1)
    link_directories(${CUDA_TOOLKIT_ROOT_DIR}/lib64)
  else()
    list(APPEND CUDA_INCLUDE_DIRS ${INCLUDE_DIRECTORIES})
    # define preprocessor macro so that we will not include the generated forcelink header
    mshadow_cuda_compile(cuda_objs ${CUDA})
    if(MSVC)
        if(ENABLE_CUDA_RTC)
            FIND_LIBRARY(CUDA_nvrtc_LIBRARY nvrtc "${CUDA_TOOLKIT_ROOT_DIR}/lib/x64"  "${CUDA_TOOLKIT_ROOT_DIR}/lib/win32")
            list(APPEND mxnet_LINKER_LIBS ${CUDA_nvrtc_LIBRARY})
            set(CUDA_cuda_LIBRARY "${CUDA_nvrtc_LIBRARY}/../cuda.lib")
            list(APPEND mxnet_LINKER_LIBS ${CUDA_cuda_LIBRARY})
            add_definitions(-DMXNET_ENABLE_CUDA_RTC=1)
        endif()
        FIND_LIBRARY(CUDA_cufft_LIBRARY nvrtc "${CUDA_TOOLKIT_ROOT_DIR}/lib/x64"  "${CUDA_TOOLKIT_ROOT_DIR}/lib/win32")
        list(APPEND mxnet_LINKER_LIBS "${CUDA_cufft_LIBRARY}/../cufft.lib") # For fft operator
        FIND_LIBRARY(CUDA_cusolver_LIBRARY nvrtc "${CUDA_TOOLKIT_ROOT_DIR}/lib/x64"  "${CUDA_TOOLKIT_ROOT_DIR}/lib/win32")
        list(APPEND mxnet_LINKER_LIBS "${CUDA_cusolver_LIBRARY}/../cusolver.lib") # For cusolver
        link_directories(${CUDA_TOOLKIT_ROOT_DIR}/lib/win32)
        link_directories(${CUDA_TOOLKIT_ROOT_DIR}/lib/x64)
    else(MSVC)
        list(APPEND mxnet_LINKER_LIBS cufft cusolver)
        if(ENABLE_CUDA_RTC)
            list(APPEND mxnet_LINKER_LIBS nvrtc cuda)
            add_definitions(-DMXNET_ENABLE_CUDA_RTC=1)
        endif()
        link_directories("${CUDA_TOOLKIT_ROOT_DIR}/lib64")
    endif()
    list(APPEND SOURCE ${cuda_objs} ${CUDA})
    add_definitions(-DMXNET_USE_CUDA=1)
    if(CUDA_LIBRARY_PATH)
        if(IS_CONTAINER_BUILD)
        # In case of building on a production-like build container which may not have Cuda installed
        if(NOT CMAKE_SYSTEM_HAS_CUDA)
            # Assuming building in a container that doesn't have CUDA installed (ie CPU-only build machine)
            # so use the stub cuda driver shared library
            if(EXISTS ${CUDA_LIBRARY_PATH}/stubs/libcuda.so)
            link_directories(${CUDA_LIBRARY_PATH}/stubs)
            endif()
        endif()
        endif()
    endif()
 endif()
endif()

# unsupported: if caffe is a subdirectory of mxnet, load its CMakeLists.txt as well
if(USE_PLUGIN_CAFFE)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/caffe)
    add_subdirectory(caffe)
  endif()
endif()

if(NOT MSVC)
  # Only add c++11 flags and definitions after cuda compiling
  add_definitions(-DDMLC_USE_CXX11)
  add_definitions(-DMSHADOW_IN_CXX11)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /EHsc")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /EHsc /Gy")
  set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} /EHsc /Gy")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /EHsc /Gy")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /OPT:REF /OPT:ICF")
  set(CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL "${CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL} /OPT:REF /OPT:ICF")
  set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF /OPT:ICF")

endif()

set(MXNET_INSTALL_TARGETS mxnet)
if(UNIX)
  # Create dummy file since we want an empty shared library before linking
  set(DUMMY_SOURCE ${CMAKE_BINARY_DIR}/dummy.c)
  file(WRITE ${DUMMY_SOURCE} "")
  list(APPEND MXNET_INSTALL_TARGETS mxnet_static)
  add_library(mxnet_static STATIC ${SOURCE})
  add_library(mxnet SHARED ${DUMMY_SOURCE})
  target_link_libraries(mxnet PRIVATE ${BEGIN_WHOLE_ARCHIVE} $<TARGET_FILE:mxnet_static> ${END_WHOLE_ARCHIVE})
  target_link_libraries(mxnet PRIVATE mxnet_static)
  set_target_properties(mxnet_static PROPERTIES OUTPUT_NAME mxnet)
else()
  add_library(mxnet SHARED ${SOURCE})
endif()

if(USE_CUDA)
  if(FIRST_CUDA AND MSVC)
    target_compile_options(mxnet PUBLIC "$<$<CONFIG:DEBUG>:-Xcompiler=-MTd -Gy>")
    target_compile_options(mxnet PUBLIC "$<$<CONFIG:RELEASE>:-Xcompiler=-MT -Gy>")
  endif()
endif()
if(USE_DIST_KVSTORE)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ps-lite/CMakeLists.txt)
    add_subdirectory("3rdparty/ps-lite")
    list(APPEND pslite_LINKER_LIBS pslite protobuf)
    target_link_libraries(mxnet PUBLIC debug ${pslite_LINKER_LIBS_DEBUG})
    target_link_libraries(mxnet PUBLIC optimized ${pslite_LINKER_LIBS_RELEASE})
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      list(APPEND mxnet_LINKER_LIBS ${pslite_LINKER_LIBS_DEBUG})
    else()
      list(APPEND mxnet_LINKER_LIBS ${pslite_LINKER_LIBS_RELEASE})
    endif()
    target_link_libraries(mxnet PUBLIC debug ${pslite_LINKER_LIBS_DEBUG})
    target_link_libraries(mxnet PUBLIC optimized ${pslite_LINKER_LIBS_RELEASE})

  else()
    set(pslite_LINKER_LIBS protobuf zmq-static)
  endif()
  add_definitions(-DMXNET_USE_DIST_KVSTORE)
  include_directories(SYSTEM ${pslite_INCLUDE_DIR})
  list(APPEND mxnet_LINKER_LIBS ${pslite_LINKER_LIBS})
endif()

target_link_libraries(mxnet PUBLIC ${mxnet_LINKER_LIBS})

if(USE_PLUGINS_WARPCTC)
  target_link_libraries(mxnet PUBLIC debug ${WARPCTC_LIB_DEBUG})
  target_link_libraries(mxnet PUBLIC optimized ${WARPCTC_LIB_RELEASE})
endif()


if(USE_OPENCV AND OpenCV_VERSION_MAJOR GREATER 2)
  add_executable(im2rec "tools/im2rec.cc")
  if(MSVC)
    target_link_libraries(im2rec mxnet)
  else()
    target_link_libraries(im2rec ${BEGIN_WHOLE_ARCHIVE} mxnet_static ${END_WHOLE_ARCHIVE})
  endif()
  target_link_libraries(im2rec
    ${mxnet_LINKER_LIBS}
    ${OpenCV_LIBS}
    dmlc
    ${pslite_LINKER_LIBS}
    )
else()
    message(WARNING "OpenCV_VERSION_MAJOR: ${OpenCV_VERSION_MAJOR}, version 3 with imgcodecs \
    is required for im2rec, im2rec will not be available")
endif()

target_link_libraries(mxnet PUBLIC dmlc)

if(MSVC AND USE_MXNET_LIB_NAMING)
  set_target_properties(mxnet PROPERTIES OUTPUT_NAME "libmxnet")
endif()

include(GNUInstallDirs)
install(TARGETS ${MXNET_INSTALL_TARGETS}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if (INSTALL_EXAMPLES)
  install(DIRECTORY example  DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
endif()

if (USE_SIGNAL_HANDLER)
    add_definitions(-DMXNET_USE_SIGNAL_HANDLER=1)
endif()

# AUTO_INSTALL_DIR -> Optional: specify post-build install direcory
if(AUTO_INSTALL_DIR)
  # ---[ Install Includes
  add_custom_command(TARGET mxnet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/include ${AUTO_INSTALL_DIR}/include
    )

  # ---[ Install Examples
  add_custom_command(TARGET mxnet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/example ${AUTO_INSTALL_DIR}/example
    )
endif()

if(INSTALL_PYTHON_VERSIONS)
  message(STATUS "Installing for python versions: ${INSTALL_PYTHON_VERSIONS}")
  foreach(version ${INSTALL_PYTHON_VERSIONS})
    set(outdir ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/python${version}/site-packages/mxnet)
    add_custom_command(TARGET mxnet POST_BUILD
      COMMAND mkdir -p ${outdir}
      COMMAND cp -ru ${CMAKE_CURRENT_SOURCE_DIR}/python/mxnet/* ${outdir}
      )
  endforeach()
endif()

if(USE_CPP_PACKAGE)
  add_subdirectory(cpp-package)
endif()

if(BUILD_CPP_EXAMPLES)
  add_subdirectory(example/image-classification/predict-cpp)
endif()

# ---[ Linter target
if(MSVC)
  find_package(PythonInterp)
  set(PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE} CACHE FILEPATH "Path to the python executable")
endif()
set(LINT_DIRS "include src plugin cpp-package")
set(EXCLUDE_PATH "src/operator/contrib/ctc_include")
add_custom_target(mxnet_lint COMMAND ${CMAKE_COMMAND} -DMSVC=${MSVC} -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE} -DLINT_DIRS=${LINT_DIRS} -DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DPROJECT_NAME=mxnet -DEXCLUDE_PATH=${EXCLUDE_PATH} -P ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dmlc-core/cmake/lint.cmake)
