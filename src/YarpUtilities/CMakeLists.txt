# Copyright (C) 2020 Istituto Italiano di Tecnologia (IIT). All rights reserved.
# This software may be modified and distributed under the terms of the
# BSD-3-Clause license.

# set target name
if(FRAMEWORK_COMPILE_YarpUtilities)

  add_bipedal_locomotion_library(
    NAME                   YarpUtilities
    SOURCES                src/Helper.cpp src/RosPublisher.cpp
    PUBLIC_HEADERS         include/BipedalLocomotion/YarpUtilities/Helper.h include/BipedalLocomotion/YarpUtilities/Helper.tpp include/BipedalLocomotion/YarpUtilities/RosPublisher.h
    PUBLIC_LINK_LIBRARIES  ${YARP_LIBRARIES} ${iDynTree_LIBRARIES} BipedalLocomotion::GenericContainer BipedalLocomotion::ParametersHandler BipedalLocomotion::TextLogging
    SUBDIRECTORIES         tests)

  add_bipedal_locomotion_yarp_thrift(
    NAME VectorsCollection
    THRIFT thrifts/BipedalLocomotion/YarpUtilities/VectorsCollection.thrift
    INSTALLATION_FOLDER YarpUtilities)

endif()
