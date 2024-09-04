cmake_minimum_required(VERSION 3.5)

project(airsaned)

include(GNUInstallDirs)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/_version.cpp
    COMMAND ${CMAKE_COMMAND}
            -DFILE=${CMAKE_CURRENT_BINARY_DIR}/version.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -Wreturn-type")
set(LINK_FLAGS_MINSIZEREL  "${LINK_FLAGS_MINSIZEREL} -s")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(.)
if(CMAKE_SYSTEM_NAME STREQUAL FreeBSD)
  include_directories(/usr/local/include)
  link_directories(/usr/local/lib)
endif()

if(APPLE)
  include_directories(
    /opt/local/include
    /opt/homebrew/include
    /usr/local/include
  )
  if(IS_DIRECTORY /opt/local/lib)
    link_directories(/opt/local/lib)
  endif()
  if(IS_DIRECTORY /opt/homebrew/lib)
    link_directories(/opt/homebrew/lib)
  endif()
  if(IS_DIRECTORY /usr/local/lib)
    link_directories(/usr/local/lib)
  endif()
  set(ZEROCONF_FILES
    zeroconf/mdnspublisher-dnssd.mm
    zeroconf/networkhotplugnotifier-macos.mm
  )
  set(ZEROCONF_LIBS
    "-lobjc"
    "-framework Foundation"
    "-framework IOKit"
    "-framework CFNetwork"
    "-framework CoreFoundation"
  )
else()
  set(ZEROCONF_FILES
    zeroconf/mdnspublisher-avahi.cpp
    zeroconf/networkhotplugnotifier.cpp
  )
  set(ZEROCONF_LIBS
    avahi-client
    avahi-common
  )
endif()
add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    server/main.cpp
    server/optionsfile.cpp
    server/server.cpp
    server/mainpage.cpp
    server/scanner.cpp
    server/purgethread.cpp
    server/scanjob.cpp
    server/scannerpage.cpp
    sanecpp/sanecpp.cpp
    basic/uuid.cpp
    basic/dictionary.cpp
    basic/fdbuf.cpp
    basic/workerthread.cpp
    web/httpserver.cpp
    web/webpage.cpp
    web/errorpage.cpp
    web/accessfile.cpp
    imageformats/imageencoder.cpp
    imageformats/jpegencoder.cpp
    imageformats/pdfencoder.cpp
    imageformats/pngencoder.cpp
    zeroconf/mdnspublisher.cpp
    zeroconf/hotplugnotifier.cpp
    ${ZEROCONF_FILES}
)
if(CMAKE_SYSTEM_NAME STREQUAL FreeBSD)
target_link_libraries(
    ${PROJECT_NAME}
    Threads::Threads
    sane
    jpeg
    png
    ${ZEROCONF_LIBS}
    usb
    atomic
)
else()
target_link_libraries(
    ${PROJECT_NAME}
    Threads::Threads
    sane
    jpeg
    png
    ${ZEROCONF_LIBS}
    usb-1.0
    atomic
)
endif()

if(APPLE)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION sbin
)
install(FILES launchd/org.simulpiscator.airsaned.plist
    DESTINATION /Library/LaunchDaemons
)

install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/access.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/access.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/ignore.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/ignore.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/options.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/options.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/Gnome-scanner.png)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/Gnome-scanner.png DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")


elseif(CMAKE_SYSTEM_NAME STREQUAL FreeBSD)

if ($ENV{PREFIX})
    set(CMAKE_INSTALL_PREFIX $ENV{PREFIX})
else()
    # default is "/usr/local"
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION sbin
)

install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/access.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/access.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/ignore.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/ignore.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/options.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/options.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/airsane/Gnome-scanner.png)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/Gnome-scanner.png DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/etc/rc.d/airsaned)
        file(INSTALL ${CMAKE_SOURCE_DIR}/freebsd/airsaned DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/rc.d)
    endif()
")

else()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
configure_file(systemd/airsaned.service.in ${CMAKE_BINARY_DIR}/airsaned.service @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/airsaned.service
    DESTINATION /lib/systemd/system
)
install(FILES systemd/airsaned.default
    DESTINATION /etc/default
    RENAME airsane
)

install(CODE "
    if(NOT EXISTS /etc/airsane/access.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/access.conf DESTINATION /etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS /etc/airsane/ignore.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/ignore.conf DESTINATION /etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS /etc/airsane/options.conf)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/options.conf DESTINATION /etc/airsane)
    endif()
")
install(CODE "
    if(NOT EXISTS /etc/airsane/Gnome-scanner.png)
        file(INSTALL ${CMAKE_SOURCE_DIR}/etc/Gnome-scanner.png DESTINATION /etc/airsane)
    endif()
")

endif()
