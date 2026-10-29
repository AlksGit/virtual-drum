# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "V:/firmware/stick")
  file(MAKE_DIRECTORY "V:/firmware/stick")
endif()
file(MAKE_DIRECTORY
  "V:/firmware/stick/build1/stick"
  "V:/firmware/stick/build1/_sysbuild/sysbuild/images/stick-prefix"
  "V:/firmware/stick/build1/_sysbuild/sysbuild/images/stick-prefix/tmp"
  "V:/firmware/stick/build1/_sysbuild/sysbuild/images/stick-prefix/src/stick-stamp"
  "V:/firmware/stick/build1/_sysbuild/sysbuild/images/stick-prefix/src"
  "V:/firmware/stick/build1/_sysbuild/sysbuild/images/stick-prefix/src/stick-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "V:/firmware/stick/build1/_sysbuild/sysbuild/images/stick-prefix/src/stick-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "V:/firmware/stick/build1/_sysbuild/sysbuild/images/stick-prefix/src/stick-stamp${cfgdir}") # cfgdir has leading slash
endif()
