# OpenComposite's tests (OC_BUILD_TESTS). See tests/conformance/README.md for the conformance runs.
enable_testing()

# Unit tests: linked against the static libraries the DLL is made from
add_executable(oc_unit_tests
	unit/main.cpp
	unit/SmokeTests.cpp
)
target_include_directories(oc_unit_tests PRIVATE
	${CMAKE_SOURCE_DIR}/libs/doctest
	${CMAKE_SOURCE_DIR}/OpenOVR
	${CMAKE_SOURCE_DIR}/BundledLibs
	${CMAKE_SOURCE_DIR}/OpenVRHeaders
	${CMAKE_SOURCE_DIR}/DrvOpenXR
	${CMAKE_BINARY_DIR}
)
# The standard headers rather than doctest's forward declarations (MSVC 19.5x warns about its std::tuple one)
target_compile_definitions(oc_unit_tests PRIVATE ${GRAPHICS_API_SUPPORT_FLAGS} DOCTEST_CONFIG_USE_STD_HEADERS)
if (WIN32)
	target_link_libraries(oc_unit_tests PRIVATE OCCore DrvOpenXR OpenVR Vulkan ${XrLib} ${glmLib})
else ()
	target_link_libraries(oc_unit_tests PRIVATE -Wl,--start-group OCCore DrvOpenXR -Wl,--end-group OpenVR Vulkan ${XrLib} ${glmLib})
endif ()
add_test(NAME unit COMMAND oc_unit_tests)
set_tests_properties(unit PROPERTIES LABELS unit)

if (WIN32)
	add_subdirectory(runtime)
endif ()
