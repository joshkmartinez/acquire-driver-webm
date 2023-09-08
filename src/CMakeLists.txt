if(NOT TARGET acquire-device-kit)
        add_subdirectory(acquire-core-libs)
endif()

# Libvpx
add_library(libvpx STATIC IMPORTED)

target_include_directories(libvpx INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../libs/libvpx/
)

set_target_properties(libvpx PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../libs/libvpx/libvpx.a
)

# Libwebm
add_library(libwebm STATIC IMPORTED)

target_include_directories(libwebm INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../libs/libwebm/
)

# Can't find std libs?
set_target_properties(libwebm PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../libs/libwebm/build/libwebm.a
)

set(tgt acquire-driver-webm)
add_library(${tgt} MODULE
        webm.cpp
        webm.driver.c
)
target_enable_simd(${tgt})



target_link_libraries(${tgt} PRIVATE
        acquire-core-logger
        acquire-core-platform
        acquire-device-kit
        libvpx
        libwebm
)

install(TARGETS ${tgt} LIBRARY DESTINATION lib)


