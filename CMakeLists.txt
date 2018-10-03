cmake_minimum_required(VERSION 3.1)

project(airsaned)
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

