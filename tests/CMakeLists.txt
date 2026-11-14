# OpenLamina's tests (OC_BUILD_TESTS). See tests/conformance/README.md for the conformance runs.
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
	# The custom interfaces (IVRPaths), which include "generated/interfaces/..."
	${CMAKE_SOURCE_DIR}/OpenVRHeaders
	${CMAKE_BINARY_DIR}
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
	conformance/crash_report.cpp
)
target_link_libraries(oc_conformance PRIVATE oc_callers)
# The program the applications case launches (next to oc_conformance)
add_executable(oc_echo_app conformance/echo_app.cpp)
add_dependencies(oc_conformance oc_echo_app)
if (WIN32)
	target_link_libraries(oc_conformance PRIVATE dbghelp)
endif ()
target_compile_definitions(oc_conformance PRIVATE OC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/unit/data")
if (NOT WIN32)
	target_link_libraries(oc_conformance PRIVATE -ldl -lpthread)
endif ()

add_executable(oc_unit_tests
	unit/main.cpp
	unit/SmokeTests.cpp
	unit/CallersTests.cpp
	unit/SoftFailTests.cpp
	unit/SessionLockTests.cpp
	unit/JsonFileTests.cpp
	unit/SteamVRPathsTests.cpp
	unit/SharedStoreTests.cpp
	unit/DeviceIdentityTests.cpp
	unit/SettingsTests.cpp
	unit/ApplicationsTests.cpp
	unit/ChaperoneTests.cpp
	unit/SkeletonTests.cpp
	unit/BindingFileTests.cpp
	unit/ModesTests.cpp
	unit/EvaluatorTests.cpp
	unit/BindingSearchTests.cpp
	unit/ConversionTests.cpp
	unit/LegacyBindingsTests.cpp
	unit/LibMonadoTests.cpp
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
# A stand-in for Monado's libmonado (LibMonadoTests.cpp)
add_library(fake_libmonado SHARED fakes/fake_libmonado.c)
set_target_properties(fake_libmonado PROPERTIES C_VISIBILITY_PRESET hidden)
if (MSVC)
	target_compile_options(fake_libmonado PRIVATE /wd4996) # sscanf
endif ()
add_dependencies(oc_unit_tests fake_libmonado)
target_compile_definitions(oc_unit_tests PRIVATE OC_TEST_FAKE_LIBMONADO="$<TARGET_FILE:fake_libmonado>")

# The standard headers rather than doctest's forward declarations (MSVC 19.5x warns about its std::tuple one)
target_compile_definitions(oc_unit_tests PRIVATE ${GRAPHICS_API_SUPPORT_FLAGS} DOCTEST_CONFIG_USE_STD_HEADERS
	OC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/unit/data" OC_ECHO_APP="$<TARGET_FILE:oc_echo_app>")
# The program the launch test starts
add_dependencies(oc_unit_tests oc_echo_app)
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
