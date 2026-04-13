cmake_minimum_required(VERSION 3.16)

project(open-pipe-media-player LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2")

option(HWACC "ENABLE HWACC" OFF)
option(BUILD_LEGACY_DRAFTS "Build historical draft targets for reference only" OFF)

if(HWACC)
    add_definitions(-DHWACC_ENABLED)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

add_executable(${PROJECT_NAME}
    src/main.c
    src/app.c
    src/player.c
    src/pipeline.c
    src/ui.c
)

add_definitions(${GTK3_CFLAGS_OTHER})
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${GST_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GTK3_INCLUDE_DIRS}
)
target_compile_options(${PROJECT_NAME} PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GLIB_CFLAGS_OTHER}
    ${GTK3_CFLAGS_OTHER}
)
target_link_directories(${PROJECT_NAME} PRIVATE
    ${GST_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${GTK3_LIBRARY_DIRS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    pthread
    ${GST_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GTK3_LIBRARIES}
)

if(BUILD_LEGACY_DRAFTS)
    add_executable(${PROJECT_NAME}-1-2 ./drafts/draft1-2.c)
    add_executable(${PROJECT_NAME}-3 ./drafts/draft3.c)

    target_include_directories(${PROJECT_NAME}-1-2 PRIVATE
        ${GST_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
    )
    target_include_directories(${PROJECT_NAME}-3 PRIVATE
        ${GST_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
    )

    target_compile_options(${PROJECT_NAME}-1-2 PRIVATE
        ${GST_CFLAGS_OTHER}
        ${GLIB_CFLAGS_OTHER}
    )
    target_compile_options(${PROJECT_NAME}-3 PRIVATE
        ${GST_CFLAGS_OTHER}
        ${GLIB_CFLAGS_OTHER}
    )

    target_link_directories(${PROJECT_NAME}-1-2 PRIVATE
        ${GST_LIBRARY_DIRS}
        ${GLIB_LIBRARY_DIRS}
    )
    target_link_directories(${PROJECT_NAME}-3 PRIVATE
        ${GST_LIBRARY_DIRS}
        ${GLIB_LIBRARY_DIRS}
    )

    target_link_libraries(${PROJECT_NAME}-1-2 PRIVATE
        ${GST_LIBRARIES}
        ${GLIB_LIBRARIES}
    )

    target_link_libraries(${PROJECT_NAME}-3 PRIVATE
        ${GST_LIBRARIES}
        ${GLIB_LIBRARIES}
    )
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
