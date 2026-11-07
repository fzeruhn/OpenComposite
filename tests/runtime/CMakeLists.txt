# oc_test_runtime: a stand-in OpenXR runtime for the tests, scripted with OC_TEST_RUNTIME_SCRIPT and recording what
# its client did in OC_TEST_RUNTIME_DUMP. Windows only for now (D3D11).
add_library(oc_test_runtime SHARED
	runtime.cpp
	runtime_internal.h
	swapchain_d3d11.cpp
	script.cpp
	script.h
)
target_include_directories(oc_test_runtime PRIVATE
	${CMAKE_SOURCE_DIR}/libs/openxr-sdk/include
	${CMAKE_SOURCE_DIR}/libs/openxr-sdk/src/common
)
target_compile_definitions(oc_test_runtime PRIVATE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
target_link_libraries(oc_test_runtime PRIVATE jsoncpp d3d11 dxgi)
set_target_properties(oc_test_runtime PROPERTIES PREFIX "")

set(OC_TEST_RUNTIME_PATH "$<TARGET_FILE:oc_test_runtime>")
set(OC_TEST_RUNTIME_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/oc_test_runtime.json)
file(READ oc_test_runtime.json.in manifest_template)
string(REPLACE "@OC_TEST_RUNTIME_PATH@" "${OC_TEST_RUNTIME_PATH}" manifest_template "${manifest_template}")
file(GENERATE OUTPUT ${OC_TEST_RUNTIME_MANIFEST} CONTENT "${manifest_template}")
set(OC_TEST_RUNTIME_MANIFEST ${OC_TEST_RUNTIME_MANIFEST} PARENT_SCOPE)

add_executable(runtime_smoke runtime_smoke.cpp)
target_link_libraries(runtime_smoke PRIVATE ${XrLib} jsoncpp d3d11 dxgi)
add_dependencies(runtime_smoke oc_test_runtime)

add_test(NAME runtime_smoke COMMAND ${Python_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/unelevated.py
	--env XR_RUNTIME_JSON=${OC_TEST_RUNTIME_MANIFEST}
	--env OC_TEST_RUNTIME_DUMP=${CMAKE_CURRENT_BINARY_DIR}/runtime_smoke_dump.json
	-- $<TARGET_FILE:runtime_smoke>)
set_tests_properties(runtime_smoke PROPERTIES LABELS runtime)
