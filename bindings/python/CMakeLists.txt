# Copyright (C) 2020 Istituto Italiano di Tecnologia (IIT). All rights reserved.
# This software may be modified and distributed under the terms of the
# BSD-3-Clause license.

add_subdirectory(type_caster)
add_subdirectory(System)
add_subdirectory(ParametersHandler)
add_subdirectory(Contacts)
add_subdirectory(Planners)
add_subdirectory(RobotInterface)
add_subdirectory(Math)
add_subdirectory(FloatingBaseEstimators)
add_subdirectory(IK)
add_subdirectory(TSID)
add_subdirectory(TextLogging)
add_subdirectory(Conversions)
add_subdirectory(YarpUtilities)
add_subdirectory(ContinuousDynamicalSystem)
add_subdirectory(ReducedModelControllers)
add_subdirectory(ML)

include(ConfigureFileWithCMakeIfTarget)

configure_file_with_cmakeif_target(${CMAKE_CURRENT_SOURCE_DIR}/bipedal_locomotion_framework.cpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/bipedal_locomotion_framework.cpp
  @ONLY)

get_property(pybind_headers GLOBAL PROPERTY pybind_headers)
get_property(pybind_sources GLOBAL PROPERTY pybind_sources)
get_property(pybind_include_dirs GLOBAL PROPERTY pybind_include_dirs)
get_property(pybind_link_libraries GLOBAL PROPERTY pybind_link_libraries)

pybind11_add_module(pybind11_blf MODULE
  ${CMAKE_CURRENT_BINARY_DIR}/bipedal_locomotion_framework.cpp
  ${pybind_sources}
  ${pybind_headers}
  )

target_compile_features(pybind11_blf PUBLIC cxx_std_17)
target_include_directories(pybind11_blf PUBLIC "$<BUILD_INTERFACE:${pybind_include_dirs}>")

target_link_libraries(pybind11_blf PRIVATE
  ${pybind_link_libraries})

# # The generated Python dynamic module must have the same name as the pybind11
# # module, i.e. `bindings`.
set_target_properties(pybind11_blf PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${BLF_PYTHON_PACKAGE}"
    OUTPUT_NAME "bindings")

if(FRAMEWORK_TEST_PYTHON_BINDINGS)
    add_subdirectory(tests)

    # copy the utils.py file for the tests
    add_custom_command(TARGET pybind11_blf POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
      ${CMAKE_CURRENT_SOURCE_DIR}/utils.py
      ${BLF_PYTHON_PACKAGE}/utils.py)


    # For testing on Windows we copy the blf .dll in the same
    # directory of the bindings
    if(WIN32)
        add_custom_command(TARGET pybind11_blf POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            $<TARGET_FILE_DIR:BipedalLocomotion::Math>
            $<TARGET_FILE_DIR:pybind11_blf>)
    endif()
endif()

# Output package is:
#
# bipedal_locomotion
# |-- __init__.py (generated from main bindings CMake file)
# `-- bindings.<cpython_extension>
# `-- utils.py

install(TARGETS pybind11_blf DESTINATION ${PYTHON_INSTDIR})

# Install the __init__.py file
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/all.py"
  DESTINATION ${PYTHON_INSTDIR})

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/utils.py"
  DESTINATION ${PYTHON_INSTDIR})
