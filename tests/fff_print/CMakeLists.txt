get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_data.cpp
	test_data.hpp
	test_extrusion_entity.cpp
	test_fill.cpp
	test_flow.cpp
	test_gcode.cpp
	test_gcodewriter.cpp
	test_gcodewriter_travel.cpp
	test_model.cpp
	test_print.cpp
	test_printgcode.cpp
	test_printobject.cpp
	test_skirt_brim.cpp
	test_support_material.cpp
	test_support_material_on_wipe_tower.cpp
	test_trianglemesh.cpp
	)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # See tests/libslic3r/CMakeLists.txt for the full rationale; the legacy
    # 1-arg invocation no longer matches bambustudio_copy_dlls's 4-arg signature.
    # bambustudio_copy_dlls(${_TEST_NAME}_tests <config> <postfix> <out>)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})

# EXP-11 regressions: the loop self-intersection check of FillFloatingConcentric and the default value of
# PlateBBoxData::first_layer_time. A separate executable because fff_print_tests above does not compile at this
# revision (test_gcodewriter.cpp still calls the removed GCodeWriter::set_extruder(int) and lift()). The objects of
# libslic3r that the test pulls in reference Http, BBL_Encrypt and nanosvg, which only the GUI target compiles, so
# their sources are built here; native_nanosvg.cpp holds the nanosvg implementation.
# Build: -DSLIC3R_BUILD_TESTS=ON, then cmake --build <build dir> --target floating_determinism_tests
add_executable(floating_determinism_tests
    fff_print_tests.cpp
    test_floating_determinism.cpp
    native_nanosvg.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/Http.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/BBLUtil.cpp
)
# Fill headers include paths relative to the libslic3r source root (FillFloatingConcentric.hpp: "Arachne/...").
target_include_directories(floating_determinism_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/libslic3r)
target_link_libraries(floating_determinism_tests test_common libslic3r libcurl OpenSSL::SSL OpenSSL::Crypto)
set_property(TARGET floating_determinism_tests PROPERTY FOLDER "tests")
add_test(floating_determinism_tests floating_determinism_tests ${CATCH_EXTRA_ARGS})

# filament_prime_volume(_nc) regressions of Print::_make_wipe_tower (a short vector is read with get_at). A separate
# executable for the same reasons as floating_determinism_tests above, built from the same sources, so the link has
# no unresolved symbols. test_data.cpp provides the test meshes.
# Build: -DSLIC3R_BUILD_TESTS=ON, then cmake --build <build dir> --target wipe_tower_prime_volume_tests
add_executable(wipe_tower_prime_volume_tests
    fff_print_tests.cpp
    test_data.cpp
    test_data.hpp
    test_wipe_tower_prime_volume.cpp
    native_nanosvg.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/Http.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/BBLUtil.cpp
)
target_include_directories(wipe_tower_prime_volume_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/libslic3r)
target_link_libraries(wipe_tower_prime_volume_tests test_common libslic3r libcurl OpenSSL::SSL OpenSSL::Crypto)
set_property(TARGET wipe_tower_prime_volume_tests PROPERTY FOLDER "tests")
add_test(wipe_tower_prime_volume_tests wipe_tower_prime_volume_tests ${CATCH_EXTRA_ARGS})

# Short per-filament vectors (a missing 3MF key keeps the one-element default) read with get_at: filament_shrink,
# filament_adhesiveness_category, filament_change_length(_nc), override_process_overhang_speed. Built like
# wipe_tower_prime_volume_tests above.
# Build: -DSLIC3R_BUILD_TESTS=ON, then cmake --build <build dir> --target short_filament_vector_tests
add_executable(short_filament_vector_tests
    fff_print_tests.cpp
    test_data.cpp
    test_data.hpp
    test_short_filament_vectors.cpp
    native_nanosvg.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/Http.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/BBLUtil.cpp
)
target_include_directories(short_filament_vector_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/libslic3r)
target_link_libraries(short_filament_vector_tests test_common libslic3r libcurl OpenSSL::SSL OpenSSL::Crypto)
set_property(TARGET short_filament_vector_tests PROPERTY FOLDER "tests")
add_test(short_filament_vector_tests short_filament_vector_tests ${CATCH_EXTRA_ARGS})

add_executable(support_material_on_wipe_tower_tests
	fff_print_tests.cpp
	test_data.cpp
	test_data.hpp
	test_support_material_on_wipe_tower.cpp
	)
target_link_libraries(support_material_on_wipe_tower_tests test_common libslic3r
	libcurl
	OpenSSL::SSL OpenSSL::Crypto
)
if (MSVC)
	target_link_options(support_material_on_wipe_tower_tests PRIVATE /FORCE:UNRESOLVED)
endif()
set_property(TARGET support_material_on_wipe_tower_tests PROPERTY FOLDER "tests")
add_test(support_material_on_wipe_tower_tests support_material_on_wipe_tower_tests ${CATCH_EXTRA_ARGS})

add_executable(periodic_modifier_tests
	fff_print_tests.cpp
	test_data.cpp
	test_data.hpp
	test_periodic_modifier.cpp
	)
target_link_libraries(periodic_modifier_tests test_common libslic3r
	libcurl
	OpenSSL::SSL OpenSSL::Crypto
)
if (MSVC)
	target_link_options(periodic_modifier_tests PRIVATE /FORCE:UNRESOLVED)
endif()
set_property(TARGET periodic_modifier_tests PROPERTY FOLDER "tests")
add_test(periodic_modifier_tests periodic_modifier_tests ${CATCH_EXTRA_ARGS})

