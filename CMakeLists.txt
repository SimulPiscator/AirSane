cmake_minimum_required(VERSION 3.1)

project(airsaned)

# collect some version information from git
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND git rev-list --count --first-parent HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_REVISION_NUMBER
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions(
  "-DGIT_BRANCH=\"${GIT_BRANCH}\""
  "-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\""
  "-DGIT_REVISION_NUMBER=\"${GIT_REVISION_NUMBER}\""
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -Wreturn-type")
set(CMAKE_CXX_FLAGS_MINSIZEREL  "${CMAKE_CXX_FLAGS_MINSIZEREL} -s")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
include_directories(.)
add_executable(${PROJECT_NAME}
    server/main.cpp
    server/scanserver.cpp
    server/scanner.cpp
    server/scanjob.cpp
    server/scannerpage.cpp
    sanecpp/sanecpp.cpp
    basic/uuid.cpp
    basic/dictionary.cpp
    basic/fdbuf.cpp
    web/httpserver.cpp
    web/webpage.cpp
    imageformats/imageencoder.cpp
    imageformats/jpegencoder.cpp
    imageformats/pdfencoder.cpp
    imageformats/pngencoder.cpp
    zeroconf/mdnspublisher.cpp
    zeroconf/hotplugnotifier.cpp
)
target_link_libraries(
    ${PROJECT_NAME}
    Threads::Threads
    sane
    jpeg
    png
    avahi-client
    avahi-common
    usb-1.0
)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
install(FILES systemd/airsaned.service
    DESTINATION /lib/systemd/system
)
install(FILES systemd/airsaned.default
    DESTINATION /etc/default
    RENAME airsane
)

