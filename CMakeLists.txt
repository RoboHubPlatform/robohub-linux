cmake_minimum_required(VERSION 3.16)
project(robohub-linux C)

# ---------------------------------------------------------------------------
# Locating a sibling module
# ---------------------------------------------------------------------------
# The platform implements the common contracts, so it links the module that
# declares them. A path is never hard-coded: a variable the parent build already
# set (rho writes one for every dependency) wins, then the environment, then a
# sibling checkout, and finally the module's repository, fetched.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")
  include("${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")
endif()

function(robohub_locate_module module out_var)
  string(TOUPPER "${module}" upper)
  string(REPLACE "-" "_" upper "${upper}")
  if(${out_var})
    return()
  endif()
  if(DEFINED ENV{${upper}_DIR})
    set(${out_var} "$ENV{${upper}_DIR}" PARENT_SCOPE)
    return()
  endif()
  set(dir "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(level RANGE 6)
    get_filename_component(parent "${dir}/.." ABSOLUTE)
    if(EXISTS "${parent}/${module}/CMakeLists.txt")
      set(${out_var} "${parent}/${module}" PARENT_SCOPE)
      return()
    endif()
    set(dir "${parent}")
  endforeach()
  include(FetchContent)
  message(STATUS "[${PROJECT_NAME}] fetching ${module}")
  FetchContent_Declare(${module}
    GIT_REPOSITORY "https://github.com/RoboHubPlatform/${module}.git"
    GIT_TAG main GIT_SHALLOW TRUE GIT_PROGRESS TRUE)
  FetchContent_MakeAvailable(${module})
  set(${out_var} "${${module}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

robohub_locate_module(robohub-common ROBOHUB_COMMON_DIR)
if(NOT ROBOHUB_COMMON_DIR)
  message(FATAL_ERROR "robohub-common not found and could not be fetched")
endif()
message(STATUS "[${PROJECT_NAME}] robohub-common at ${ROBOHUB_COMMON_DIR}")

if(PROJECT_IS_TOP_LEVEL)
  add_subdirectory("${ROBOHUB_COMMON_DIR}" robohub-common)
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(robohub-linux STATIC
    src/linux_platform.c
    src/linux_net.c
)

target_include_directories(robohub-linux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ROBOHUB_COMMON_DIR}/include
    ${ROBOHUB_COMMON_DIR}/include/robohub
    ${ROBOHUB_COMMON_DIR}/include/robohub/hal
    ${ROBOHUB_COMMON_DIR}/include/robohub/drivers
)

target_link_libraries(robohub-linux PUBLIC robohub-common)
target_compile_features(robohub-linux PUBLIC c_std_11)

# ---------------------------------------------------------------------------
# Host tests
# ---------------------------------------------------------------------------
# The platform is a host, so its test is a host program: it brings the platform
# up and checks the contracts it registers against the machine.
if(PROJECT_IS_TOP_LEVEL AND RHO_BUILD_HOST_TESTS AND NOT RHO_TARGET_IS_BAREMETAL)
  message(STATUS "[robohub-linux] registering host unit tests")
  enable_testing()
  add_executable(robohub-linux-test-platform tests/test_linux_platform.c)
  target_link_libraries(robohub-linux-test-platform PRIVATE robohub-linux)
  add_test(NAME robohub-linux-test-platform COMMAND robohub-linux-test-platform)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS robohub-linux ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
