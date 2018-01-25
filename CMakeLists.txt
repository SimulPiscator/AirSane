cmake_minimum_required(VERSION 2.8)

project(airsaned)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wreturn-type")
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
    pthread
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

