# OpenComposite's tests (OC_BUILD_TESTS). See tests/conformance/README.md for the conformance runs.
enable_testing()

# Unit tests: linked against the static libraries the DLL is made from
# The resource data (render models, keyboard layout, font) is part of the DLL's own sources, not OCCore
set(OC_TEST_RESOURCES)
foreach (res ${OVR_RESOURCES})
	list(APPEND OC_TEST_RESOURCES ${CMAKE_SOURCE_DIR}/${res})
endforeach ()
if (NOT WIN32)
	set_property(SOURCE ${OC_TEST_RESOURCES} APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}")
endif ()

# The generated callers of every interface version (conformance/gen_callers.py)
add_custom_command(
	OUTPUT ${GENERATED_DIR}/conformance/callers.gen.cpp
	COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/conformance/gen_callers.py ${GENERATED_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/conformance/interfaces.txt ${CMAKE_CURRENT_SOURCE_DIR}/conformance/unsafe_methods.txt
		${GENERATED_DIR}/conformance/callers.gen.cpp
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/conformance/gen_callers.py ${CMAKE_SOURCE_DIR}/scripts/libparse.py
		${CMAKE_CURRENT_SOURCE_DIR}/conformance/interfaces.txt ${CMAKE_CURRENT_SOURCE_DIR}/conformance/unsafe_methods.txt
		${CMAKE_CURRENT_SOURCE_DIR}/conformance/not_implemented.txt ${GENERATED_DIR}/interfaces/vrtypes.h
	COMMENT "Generating the conformance callers..."
)
add_library(oc_callers STATIC
	conformance/callers_support.h
	conformance/callers_support.cpp
	${GENERATED_DIR}/conformance/callers.gen.cpp
)
target_include_directories(oc_callers PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/conformance
	${CMAKE_SOURCE_DIR}/OpenOVR
	${GENERATED_DIR}
	${GENERATED_DIR}/interfaces
)
target_link_libraries(oc_callers PUBLIC jsoncpp)
if (WIN32)
	target_link_libraries(oc_callers PUBLIC d3d11)
	# The generated file is one large translation unit
	target_compile_options(oc_callers PRIVATE /bigobj)
endif ()
add_dependencies(oc_callers OCCore)
if (NOT MSVC)
	# The split headers' unused version-string constants (IVRIOBuffer_Version and the like)
	target_compile_options(oc_callers PUBLIC -Wno-unused-variable)
endif ()

# The conformance program (conformance/README.md): loads a vrclient DLL and calls everything
add_executable(oc_conformance
	conformance/conformance.h
	conformance/oc_conformance.cpp
	conformance/cases.cpp
)
target_link_libraries(oc_conformance PRIVATE oc_callers)
target_compile_definitions(oc_conformance PRIVATE OC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/unit/data")
if (NOT WIN32)
	target_link_libraries(oc_conformance PRIVATE -ldl -lpthread)
endif ()

add_executable(oc_unit_tests
	unit/main.cpp
	unit/SmokeTests.cpp
	unit/CallersTests.cpp
	${OC_TEST_RESOURCES}
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
	target_link_libraries(oc_unit_tests PRIVATE OCCore DrvOpenXR OpenVR Vulkan ${XrLib} ${glmLib} oc_callers)
else ()
	target_link_libraries(oc_unit_tests PRIVATE -Wl,--start-group OCCore DrvOpenXR -Wl,--end-group OpenVR Vulkan ${XrLib} ${glmLib} oc_callers)
endif ()
add_test(NAME unit COMMAND oc_unit_tests)
set_tests_properties(unit PROPERTIES LABELS unit)

if (WIN32)
	add_subdirectory(runtime)
endif ()
