if(${NOTEST})
    message(STATUS "Skipping test targets")
else()
    #
    # PARAMETERS
    #
    set(project acquire-driver-webm) # CMAKE_PROJECT_NAME gets overridden if this is a subtree of another project
    set(driver acquire-driver-webm)

    #
    # Tests
    #
    set(tests
        list-devices
        can-load-driver-interface
        storage-get-meta
    )

    foreach(name ${tests})
        set(tgt ${project}-${name})
        add_executable(${tgt} ${name}.cpp)
        target_compile_definitions(${tgt} PUBLIC "TEST=\"${tgt}\"")
        target_include_directories(${tgt} PRIVATE "../src/simcams")
        target_link_libraries(${tgt}
            acquire-core-platform
            acquire-core-logger
            acquire-device-kit
            acquire-device-hal
            acquire-device-properties
        )
        add_test(NAME test-${tgt} COMMAND ${tgt})
        set_tests_properties(test-${tgt} PROPERTIES LABELS "anyplatform;acquire-driver-common")
    endforeach()

    #
    # Copy driver to tests
    #
    list(POP_FRONT tests onename)
    add_custom_target(${project}-copy-${driver}-for-devkit-tests
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${driver}>
        $<TARGET_FILE_DIR:${project}-${onename}>
        DEPENDS ${driver}
        COMMENT "Copying ${driver} to $<TARGET_FILE_DIR:${project}-${onename}>"
    )

    foreach(name ${tests})
        add_dependencies(${tgt} ${project}-copy-${driver}-for-devkit-tests)
    endforeach()
endif()
