# oc_test_runtime: a stand-in OpenXR runtime for the tests, scripted with OC_TEST_RUNTIME_SCRIPT and recording what
# its client did in OC_TEST_RUNTIME_DUMP. Windows only for now (D3D11, D3D12, Vulkan and OpenGL).
add_library(oc_test_runtime SHARED
	runtime.cpp
	runtime_internal.h
	actions.cpp
	extensions.cpp
	swapchain_d3d11.cpp
	swapchain_d3d12.cpp
	swapchain_vk.cpp
	swapchain_gl.cpp
	probe.cpp
	probe.h
	script.cpp
	script.h
	oc_test_runtime.def
)
target_include_directories(oc_test_runtime PRIVATE
	${CMAKE_SOURCE_DIR}/libs/openxr-sdk/include
	${CMAKE_SOURCE_DIR}/libs/openxr-sdk/src/common
	${CMAKE_SOURCE_DIR}/RuntimeExtensions
	# Vulkan's headers only: its functions are looked up at run time
	${CMAKE_SOURCE_DIR}/libs/vulkan/Include
)
target_compile_definitions(oc_test_runtime PRIVATE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
	XR_USE_GRAPHICS_API_D3D12 XR_USE_GRAPHICS_API_VULKAN XR_USE_GRAPHICS_API_OPENGL VK_NO_PROTOTYPES)
target_link_libraries(oc_test_runtime PRIVATE jsoncpp d3d11 d3d12 dxgi opengl32)
set_target_properties(oc_test_runtime PROPERTIES PREFIX "")

set(OC_TEST_RUNTIME_PATH "$<TARGET_FILE:oc_test_runtime>")
set(OC_TEST_RUNTIME_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/oc_test_runtime.json)
file(READ oc_test_runtime.json.in manifest_template)
string(REPLACE "@OC_TEST_RUNTIME_PATH@" "${OC_TEST_RUNTIME_PATH}" manifest_template "${manifest_template}")
file(GENERATE OUTPUT ${OC_TEST_RUNTIME_MANIFEST} CONTENT "${manifest_template}")
set(OC_TEST_RUNTIME_MANIFEST ${OC_TEST_RUNTIME_MANIFEST} PARENT_SCOPE)

add_executable(runtime_smoke runtime_smoke.cpp)
target_include_directories(runtime_smoke PRIVATE ${CMAKE_SOURCE_DIR}/libs/doctest ${CMAKE_SOURCE_DIR}/libs/vulkan/Include)
target_compile_definitions(runtime_smoke PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS VK_NO_PROTOTYPES)
target_link_libraries(runtime_smoke PRIVATE ${XrLib} jsoncpp d3d11 d3d12 dxgi opengl32)
add_dependencies(runtime_smoke oc_test_runtime)

add_test(NAME runtime_smoke COMMAND ${Python_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/unelevated.py
	--env XR_RUNTIME_JSON=${OC_TEST_RUNTIME_MANIFEST}
	--env OC_TEST_RUNTIME_DUMP=${CMAKE_CURRENT_BINARY_DIR}/runtime_smoke_dump.json
	-- $<TARGET_FILE:runtime_smoke> --test-case-exclude=script_*,*_red_frame,*_session_checks)
# The script's swapchain formats and poses: a process of its own, since the script is read once per process
add_test(NAME runtime_smoke_script COMMAND ${Python_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/unelevated.py
	--env XR_RUNTIME_JSON=${OC_TEST_RUNTIME_MANIFEST}
	--env OC_TEST_RUNTIME_DUMP=${CMAKE_CURRENT_BINARY_DIR}/runtime_smoke_script_dump.json
	--env OC_TEST_RUNTIME_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/formats_poses.json
	-- $<TARGET_FILE:runtime_smoke> --test-case=script_formats_and_poses)
# A frame on each API (D3D12 on WARP without a GPU; Vulkan and OpenGL skipped without a driver for them), and
# the session checks
add_test(NAME runtime_smoke_apis COMMAND ${Python_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/unelevated.py
	--env XR_RUNTIME_JSON=${OC_TEST_RUNTIME_MANIFEST}
	--env OC_TEST_RUNTIME_DUMP=${CMAKE_CURRENT_BINARY_DIR}/runtime_smoke_apis_dump.json
	--env OC_TEST_RUNTIME_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/all_apis.json
	-- $<TARGET_FILE:runtime_smoke> --test-case=*_red_frame,*_session_checks)
set_tests_properties(runtime_smoke runtime_smoke_script runtime_smoke_apis PROPERTIES LABELS runtime)

add_executable(runtime_input_smoke runtime_input_smoke.cpp)
target_link_libraries(runtime_input_smoke PRIVATE ${XrLib} d3d11 dxgi)
add_dependencies(runtime_input_smoke oc_test_runtime)
add_test(NAME runtime_input_noext COMMAND ${Python_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/unelevated.py
	--env XR_RUNTIME_JSON=${OC_TEST_RUNTIME_MANIFEST}
	-- $<TARGET_FILE:runtime_input_smoke> noext)
add_test(NAME runtime_input COMMAND ${Python_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/unelevated.py
	--env XR_RUNTIME_JSON=${OC_TEST_RUNTIME_MANIFEST}
	--env OC_TEST_RUNTIME_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/input_smoke.json
	-- $<TARGET_FILE:runtime_input_smoke> input)
set_tests_properties(runtime_input_noext runtime_input PROPERTIES LABELS runtime)
