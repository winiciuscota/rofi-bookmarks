cmake_minimum_required(VERSION 3.10)
project(rofi-bookmarks)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB2 REQUIRED glib-2.0)
pkg_search_module(CAIRO REQUIRED cairo)
pkg_get_variable(ROFI_PLUGINS_DIR rofi pluginsdir)

if(NOT ROFI_PLUGINS_DIR)
    set(ROFI_PLUGINS_DIR "${CMAKE_INSTALL_PREFIX}/lib/rofi")
endif()

include_directories(
    ${GLIB2_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
)

add_library(bookmarks SHARED src/rofi-bookmarks.c)
set_target_properties(bookmarks PROPERTIES PREFIX "")

target_link_libraries(bookmarks
    ${GLIB2_LIBRARIES}
    ${CAIRO_LIBRARIES}
)

install(TARGETS bookmarks DESTINATION ${ROFI_PLUGINS_DIR})

message(STATUS "Installing to: ${ROFI_PLUGINS_DIR}")
