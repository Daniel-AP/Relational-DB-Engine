set(DANDB_TEST_SOURCES
    core/resultTests.cpp
    core/statusTests.cpp
    core/versionTests.cpp
)

if(TARGET Catch2::Catch2WithMain AND DANDB_TEST_SOURCES)
    add_executable(DanDBTests
        ${DANDB_TEST_SOURCES}
    )

    target_link_libraries(DanDBTests
        PRIVATE
            DanDBLib
            Catch2::Catch2WithMain
    )

    dandb_apply_compiler_warnings(DanDBTests)
    dandb_enable_sanitizers(DanDBTests)

    include(CTest)
    include(Catch)
    catch_discover_tests(DanDBTests)
else()
    add_custom_target(DanDBTests
        COMMAND ${CMAKE_COMMAND} -E echo "DanDBTests is a placeholder. Vendor Catch2 and add test .cpp files to tests/CMakeLists.txt to build real tests."
        VERBATIM
    )
endif()
