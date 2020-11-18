/**
 * @file MasImuTest.cpp
 * @authors Stefano Dafarra
 * @copyright 2019 Istituto Italiano di Tecnologia (IIT). This software may be modified and
 * distributed under the terms of the GNU Lesser General Public License v2.1 or any later version.
 */

#include <BipedalLocomotion/MasImuTest.h>
#include <BipedalLocomotion/YarpUtilities/Helper.h>
#include <BipedalLocomotion/Conversions/matioCppConversions.h>
#include <iDynTree/ModelIO/ModelLoader.h>
#include <iDynTree/yarp/YARPConfigurationsLoader.h>
#include <iDynTree/Model/IJoint.h>
#include <iDynTree/Core/SO3Utils.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <yarp/os/LogStream.h>
#include <iostream>
#include <cassert>
#include <cmath>
#include <sstream>

using namespace BipedalLocomotion;
using namespace BipedalLocomotion::GenericContainer;
using namespace BipedalLocomotion::Conversions;


void MasImuTest::MasImuData::reserveData()
{
    m_errorData.reserve(m_commonDataPtr->maxSamples);
    m_rotationFeedbackData.reserve(m_commonDataPtr->maxSamples);
    m_rotationFeedbackInInertialData.reserve(m_commonDataPtr->maxSamples);
    m_rotationFeedbackInInertialYawFilteredData.reserve(m_commonDataPtr->maxSamples);
    m_rotationFromEncodersData.reserve(m_commonDataPtr->maxSamples);
    m_jointsPositionData.reserve(m_commonDataPtr->maxSamples);
    m_rpyImuData.reserve(m_commonDataPtr->maxSamples);
    m_gyroData.reserve(m_commonDataPtr->maxSamples);
    m_accData.reserve(m_commonDataPtr->maxSamples);
}

void MasImuTest::MasImuData::clearData()
{
    m_errorData.clear();
    m_rotationFeedbackData.clear();
    m_rotationFeedbackInInertialData.clear();
    m_rotationFeedbackInInertialYawFilteredData.clear();
    m_rotationFromEncodersData.clear();
    m_jointsPositionData.clear();
    m_rpyImuData.clear();
    m_gyroData.clear();
    m_accData.clear();
}

bool MasImuTest::MasImuData::setupModel()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::setupModel](" + m_testName +") ";

    bool ok = m_group->getParameter("imu_frame", m_frameName);
    if (!ok)
    {
        yError() << errorPrefix << "Setup failed.";
        return false;
    }

    m_frame = m_commonDataPtr->fullModel.getFrameIndex(m_frameName);

    if (m_frame == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << errorPrefix << "The frame " << m_frameName << " does not exists in the robot model."
                 << ". Configuration failed.";
        return false;
    }

    m_link = m_commonDataPtr->fullModel.getFrameLink(m_frame);
    assert(m_link != iDynTree::LINK_INVALID_INDEX);

    m_consideredJointIndexes.clear();
    m_consideredJointNames.clear();

    iDynTree::LinkIndex baseLinkIndex = m_commonDataPtr->traversal.getBaseLink()->getIndex();
    iDynTree::LinkIndex currentLink = m_link;
    while (currentLink != baseLinkIndex) {
        const iDynTree::IJoint* joint = m_commonDataPtr->traversal.getParentJointFromLinkIndex(currentLink);
        assert(joint);
        if (joint->getNrOfDOFs() > 0)
        {
            m_consideredJointIndexes.push_back(joint->getIndex());
            m_consideredJointNames.push_back(m_commonDataPtr->fullModel.getJointName(m_consideredJointIndexes.back()));
        }
        currentLink = m_commonDataPtr->traversal.getParentLinkFromLinkIndex(currentLink)->getIndex();
    }

    iDynTree::ModelLoader  reducedModelLoader;
    ok = reducedModelLoader.loadReducedModelFromFullModel(m_commonDataPtr->fullModel, m_consideredJointNames);

    if (!ok)
    {
        yError() << errorPrefix << "Failed to build the reduced model. Configuration failed.";
        return false;
    }

    ok = m_kinDyn.loadRobotModel(reducedModelLoader.model());

    if (!ok)
    {
        yError() << errorPrefix << "Failed to load the reduced model. Configuration failed.";
        return false;
    }

    return true;
}

bool MasImuTest::MasImuData::setupIMUDriver()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::setupIMUDriver](" + m_testName +") ";
    std::string remote;
    bool ok = m_group->getParameter("remote", remote);
    if (!ok)
    {
        yError() << errorPrefix << "Setup failed.";
        return false;
    }

    yarp::os::Property inertialClientProperty;
    inertialClientProperty.put("remote", "/" + m_commonDataPtr->robotName + "/" + remote);
    inertialClientProperty.put("local", "/" + m_commonDataPtr->prefix + "/" + remote);
    inertialClientProperty.put("timeout", m_commonDataPtr->masTimeout);
    inertialClientProperty.put("device","multipleanalogsensorsclient");

    if (!m_orientationDriver.open(inertialClientProperty))
    {
        yError() << errorPrefix << "Failed to open multipleanalogsensorsclient on remote "
                 << remote << ". Setup failed.";
        return false;
    }

    return true;
}

bool MasImuTest::MasImuData::setupOrientationSensor()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::setupOrientationSensors](" + m_testName +") ";

    bool ok = m_group->getParameter("imu_sensor_name", m_imuName);
    if (!ok)
    {
        yError() << errorPrefix << "Setup failed.";
        return false;
    }

    if (!m_orientationDriver.view(m_orientationInterface) || !m_orientationInterface)
    {
        yError() << errorPrefix << "Failed to view orientation interface. Setup failed.";
        return false;
    }

    m_imuSensorIndex = 0;
    std::string name;
    bool found = false;
    do
    {
        bool ok = m_orientationInterface->getOrientationSensorFrameName(m_imuSensorIndex, name);
        if (ok)
        {
            found = name == m_imuName;

            if (!found)
            {
                m_imuSensorIndex++;
            }
        }
    }
    while (ok && (m_imuSensorIndex < m_orientationInterface->getNrOfOrientationSensors()) && !found);

    if (!found)
    {
        yError() << errorPrefix << "The interface contains no orientation sensors on frame "
                 << m_imuName << ". Available orientation sensor frame names (" << m_orientationInterface->getNrOfOrientationSensors() << "):";
        ok = true;
        size_t index = 0;
        while (ok && (index < m_orientationInterface->getNrOfOrientationSensors()))
        {
            bool ok = m_orientationInterface->getOrientationSensorFrameName(index, name);
            if (ok)
            {
                yError() << "       - " << name;
                index++;
            }
        }

        return false;
    }

    m_rpyInDeg.resize(3);

    return true;
}

bool MasImuTest::MasImuData::setupGyro()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::setupGyro](" + m_testName +") ";

    bool ok = m_group->getParameter("gyro_sensor_name", m_gyroName);
    if (!ok)
    {
        yError() << errorPrefix << "Setup failed.";
        return false;
    }

    if (!m_orientationDriver.view(m_gyroInterface) || !m_gyroInterface)
    {
        yError() << errorPrefix << "Failed to view gyro interface. Setup failed.";
        return false;
    }

    m_gyroSensorIndex = 0;
    std::string name;
    bool found = false;
    do
    {
        bool ok = m_gyroInterface->getThreeAxisGyroscopeFrameName(m_gyroSensorIndex, name);
        if (ok)
        {
            found = name == m_gyroName;

            if (!found)
            {
                m_gyroSensorIndex++;
            }
        }
    }
    while (ok && (m_gyroSensorIndex < m_gyroInterface->getNrOfThreeAxisGyroscopes()) && !found);

    if (!found)
    {
        yError() << errorPrefix << "The interface contains no gyro sensors on frame "
                 << m_gyroName << ". Available gyro sensor frame names (" << m_gyroInterface->getNrOfThreeAxisGyroscopes() << "):";
        ok = true;
        size_t index = 0;
        while (ok && (index < m_gyroInterface->getNrOfThreeAxisGyroscopes()))
        {
            bool ok = m_gyroInterface->getThreeAxisGyroscopeFrameName(index, name);
            if (ok)
            {
                yError() << "       - " << name;
                index++;
            }
        }

        return false;
    }

    m_gyroInDeg_s.resize(3);

    return true;
}

bool MasImuTest::MasImuData::setupAccelerometer()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::setupAccelerometer](" + m_testName +") ";

    bool ok = m_group->getParameter("acc_sensor_name", m_accName);
    if (!ok)
    {
        yError() << errorPrefix << "Setup failed.";
        return false;
    }

    if (!m_orientationDriver.view(m_accInterface) || !m_accInterface)
    {
        yError() << errorPrefix << "Failed to view accelerometers interface. Setup failed.";
        return false;
    }

    m_accSensorIndex = 0;
    std::string name;
    bool found = false;
    do
    {
        bool ok = m_accInterface->getThreeAxisLinearAccelerometerFrameName(m_accSensorIndex, name);
        if (ok)
        {
            found = name == m_accName;

            if (!found)
            {
                m_accSensorIndex++;
            }
        }
    }
    while (ok && (m_accSensorIndex < m_accInterface->getNrOfThreeAxisLinearAccelerometers()) && !found);

    if (!found)
    {
        yError() << errorPrefix << "The interface contains no accelerometers on frame "
                 << m_accName << ". Available accelerometer frame names (" << m_accInterface->getNrOfThreeAxisLinearAccelerometers() << "):";
        ok = true;
        size_t index = 0;
        while (ok && (index < m_accInterface->getNrOfThreeAxisLinearAccelerometers()))
        {
            bool ok = m_accInterface->getThreeAxisLinearAccelerometerFrameName(index, name);
            if (ok)
            {
                yError() << "       - " << name;
                index++;
            }
        }

        return false;
    }

    m_acc.resize(3);

    return true;
}

bool MasImuTest::MasImuData::setupEncoders()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::setupEncoders](" + m_testName +") ";

    std::vector<std::string> inputControlBoards;
    bool ok = m_group->getParameter("remote_control_boards", inputControlBoards);
    if (!ok)
    {
        yError() << errorPrefix << "Setup failed.";
        return false;
    }

    // open the remotecontrolboardremepper YARP device
    yarp::os::Property remapperOptions;
    remapperOptions.put("device", "remotecontrolboardremapper");

    YarpUtilities::addVectorOfStringToProperty(remapperOptions, "axesNames", m_consideredJointNames);

    // prepare the remotecontrolboards
    yarp::os::Bottle remoteControlBoardsYarp;
    yarp::os::Bottle& remoteControlBoardsYarpList = remoteControlBoardsYarp.addList();
    for (auto& rcb : inputControlBoards)
        remoteControlBoardsYarpList.addString("/" + m_commonDataPtr->robotName + "/" + rcb);

    remapperOptions.put("remoteControlBoards", remoteControlBoardsYarp.get(0));
    remapperOptions.put("localPortPrefix", "/" + m_commonDataPtr->prefix + "/remoteControlBoard");

    // open the device
    if (!m_robotDriver.open(remapperOptions))
    {
        yError() << errorPrefix << "Could not open remotecontrolboardremapper object. Setup failed.";
        return false;
    }

    if(!m_robotDriver.view(m_encodersInterface) || !m_encodersInterface)
    {
        yError() << errorPrefix << "Cannot obtain IEncoders interface. Setup failed.";
        return false;
    }

    m_positionFeedbackDeg.resize(m_consideredJointNames.size());
    m_positionFeedbackInRad.resize(m_consideredJointNames.size());
    m_previousPositionFeedbackInRad.resize(m_consideredJointNames.size());
    m_dummyVelocity.resize(m_consideredJointNames.size());
    m_dummyVelocity.zero();

    return true;
}

bool MasImuTest::MasImuData::getFeedback()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::getFeedback](" + m_testName +") ";

    size_t maxAttempts = 100;

    size_t attempt = 0;
    bool okEncoders = false;
    bool okIMU = false;
    bool okGyro = false;
    bool okAcc = false;

    do
    {
        if (!okEncoders)
            okEncoders = m_encodersInterface->getEncoders(m_positionFeedbackDeg.data());

        if (!okIMU)
        {
            yarp::dev::MAS_status status = m_orientationInterface->getOrientationSensorStatus(m_imuSensorIndex);
            if (status == yarp::dev::MAS_status::MAS_OK)
            {
                double timestamp;
                okIMU = m_orientationInterface->getOrientationSensorMeasureAsRollPitchYaw(m_imuSensorIndex, m_rpyInDeg, timestamp);
            }
        }

        if (!okGyro)
        {
            yarp::dev::MAS_status status = m_gyroInterface->getThreeAxisGyroscopeStatus(m_gyroSensorIndex);
            if (status == yarp::dev::MAS_status::MAS_OK)
            {
                double timestamp;
                okGyro = m_gyroInterface->getThreeAxisGyroscopeMeasure(m_gyroSensorIndex, m_gyroInDeg_s, timestamp);
            }
        }

        if (!okAcc)
        {
            yarp::dev::MAS_status status = m_accInterface->getThreeAxisLinearAccelerometerStatus(m_accSensorIndex);
            if (status == yarp::dev::MAS_status::MAS_OK)
            {
                double timestamp;
                okAcc = m_accInterface->getThreeAxisLinearAccelerometerMeasure(m_accSensorIndex, m_acc, timestamp);
            }
        }

        if (okEncoders && okIMU && okGyro && okAcc)
        {
            for(unsigned j = 0 ; j < m_positionFeedbackDeg.size(); j++)
            {
                m_positionFeedbackInRad(j) = iDynTree::deg2rad(m_positionFeedbackDeg(j));
            }

            m_rpyInRad(0) = iDynTree::deg2rad(m_rpyInDeg[0]);
            m_rpyInRad(1) = iDynTree::deg2rad(m_rpyInDeg[1]);
            m_rpyInRad(2) = iDynTree::deg2rad(m_rpyInDeg[2]);

            m_rotationFeedback = iDynTree::Rotation::RPY(m_rpyInRad(0), m_rpyInRad(1), m_rpyInRad(2));

            return true;
        }

        yarp::os::Time::delay(0.001);
        attempt++;
    }
    while(attempt < maxAttempts);

    yError() << errorPrefix << "The following readings failed:";
    if(!okEncoders)
        yError() << "\t - Position encoders";

    if (!okIMU)
        yError() << "\t - IMU";

    if (!okGyro)
        yError() << "\t - Gyro";

    if (!okAcc)
        yError() << "\t - Accelerometer";

    return false;
}

bool MasImuTest::MasImuData::updateRotationFromEncoders()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::updateRotationFromEncoders](" + m_testName +") ";

    iDynTree::Twist dummy;
    dummy.zero();

    iDynTree::Vector3 gravity;
    gravity(0) = 0.0;
    gravity(1) = 0.0;
    gravity(2) = -9.81;

    bool ok = m_kinDyn.setRobotState(m_commonDataPtr->baseTransform, m_positionFeedbackInRad, dummy, m_dummyVelocity, gravity);

    if (!ok)
    {
        yError() << errorPrefix << "Failed to set the state in kinDyn object.";
        return false;
    }

    iDynTree::Transform frameTransform = m_kinDyn.getWorldTransform(m_frame);
    m_rotationFromEncoders = frameTransform.getRotation();

    return true;
}

double MasImuTest::MasImuData::maxVariation()
{
    // clear the std::pair
    double maxVariation = 0;
    double jointVariation;

    for (unsigned int i = 0; i < m_positionFeedbackInRad.size(); i++)
    {
        jointVariation = std::fabs(m_positionFeedbackInRad(i) - m_previousPositionFeedbackInRad(i));
        if (jointVariation > maxVariation)
        {
            maxVariation = jointVariation;
        }
    }

    return maxVariation;
}

bool MasImuTest::MasImuData::setup(const std::string &testName, ParametersHandler::YarpImplementation::shared_ptr group,
                                   std::shared_ptr<CommonData> commonDataPtr, const std::string& logPrefix)
{
    m_testName = testName;
    m_logPrefix = logPrefix;
    m_commonDataPtr = commonDataPtr;
    m_group = group;

    reserveData();

    std::string errorPrefix = "[MasImuTest::MasImuData::setup](" + m_testName +") ";

    bool ok = setupModel();
    if (!ok)
    {
        yError() << errorPrefix << "setupModel failed.";
        return false;
    }

    ok = setupIMUDriver();
    if (!ok)
    {
        yError() << errorPrefix << "setupDriver failed.";
        return false;
    }

    ok = setupOrientationSensor();
    if (!ok)
    {
        yError() << errorPrefix << "setupOrientationSensors failed.";
        return false;
    }

    ok = setupGyro();
    if (!ok)
    {
        yError() << errorPrefix << "setupGyro failed.";
        return false;
    }

    ok = setupAccelerometer();
    if (!ok)
    {
        yError() << errorPrefix << "setupAccelerometer failed.";
        return false;
    }

    ok = setupEncoders();
    if (!ok)
    {
        yError() << errorPrefix << "setupEncoders failed.";
        return false;
    }

    return true;
}

bool MasImuTest::MasImuData::firstRun()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::firstRun](" + m_testName +") ";

    bool ok = getFeedback();
    if (!ok)
    {
        yError() << errorPrefix << "Failed to get feedbacks.";
        return false;
    }

    ok = updateRotationFromEncoders();
    if (!ok)
    {
        yError() << errorPrefix << "updateRotationFromEncoders() failed.";
        return false;
    }

    m_imuWorld = m_rotationFromEncoders * m_rotationFeedback.inverse();

    m_previousPositionFeedbackInRad = m_positionFeedbackInRad;

    return true;
}

bool MasImuTest::MasImuData::addSample()
{
    if (isCompleted())
    {
        return true;
    }

    std::string errorPrefix = "[MasImuTest::MasImuData::addSample](" + m_testName +") ";

    bool ok = getFeedback();
    if (!ok)
    {
        yError() << errorPrefix << "Failed to get feedbacks.";
        return false;
    }

    if (maxVariation() < m_commonDataPtr->minJointVariationRad)
    {
        return true;
    }

    ok = updateRotationFromEncoders();
    if (!ok)
    {
        yError() << errorPrefix << "updateRotationFromEncoders() failed.";
        return false;
    }

    m_rpyImuData.push_back(m_rpyInDeg);
    m_jointsPositionData.push_back(m_positionFeedbackInRad);
    m_rotationFromEncodersData.push_back(m_rotationFromEncoders);
    m_rotationFeedbackData.push_back(m_rotationFeedback);
    m_gyroData.push_back(m_gyroInDeg_s);
    m_accData.push_back(m_acc);

    iDynTree::Rotation measuredImu = m_imuWorld * m_rotationFeedback;

    m_rotationFeedbackInInertialData.push_back(measuredImu);

    if (m_commonDataPtr->filterYaw)
    {
        double measuredRoll, measuredPitch, measuredYaw;
        measuredImu.getRPY(measuredRoll, measuredPitch, measuredYaw);

        double estimatedRoll, estimatedPitch, estimatedYaw;
        m_rotationFromEncoders.getRPY(estimatedRoll, estimatedPitch, estimatedYaw);

        measuredImu = iDynTree::Rotation::RPY(measuredRoll, measuredPitch, estimatedYaw);
    }

    m_rotationFeedbackInInertialYawFilteredData.push_back(measuredImu);

    iDynTree::Rotation error = m_rotationFromEncoders.inverse() * measuredImu;

    m_errorData.push_back(error);

    m_previousPositionFeedbackInRad = m_positionFeedbackInRad;

    yInfo() << errorPrefix << "Sample " << addedSamples() << "/" << m_commonDataPtr->maxSamples;

    if (static_cast<int>(addedSamples()) >= m_commonDataPtr->maxSamples)
    {
        setCompleted();
    }

    return true;
}

size_t MasImuTest::MasImuData::addedSamples() const
{
    return m_errorData.size();
}

bool MasImuTest::MasImuData::isCompleted() const
{
    return m_completed;
}

void MasImuTest::MasImuData::setCompleted()
{
    m_completed = true;
}

std::string MasImuTest::MasImuData::printResults()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::printResults](" + m_testName +") ";

    std::stringstream outputStream;

    auto rpyPrinter = [](const iDynTree::Rotation& rot)->std::string
    {
        std::string output;
        iDynTree::Vector3 rpy = rot.asRPY();

        output = "RPY [deg]: (" + std::to_string(iDynTree::rad2deg(rpy[0])) + ", " + std::to_string(iDynTree::rad2deg(rpy[1])) + ", " + std::to_string(iDynTree::rad2deg(rpy[2])) + ")";
        return output;
    };

    if (!m_errorData.size())
    {
        outputStream << errorPrefix << "Inertial calibration matrix:" << std::endl
                << "--------------------------------------" << std::endl
                << m_imuWorld.toString()
                << rpyPrinter(m_imuWorld) << std::endl
                << "--------------------------------------" << std::endl
                << "Results ("<<m_errorData.size() << " samples) :" << std::endl
                << "--------------------------------------" << std::endl
                << "--------------------------------------" << std::endl;
        m_output = outputStream.str();
        return m_output;
    }

    iDynTree::Rotation meanError;

    iDynTree::GeodesicL2MeanOptions options;
    options.maxIterations = 1000; //If it takes so many steps to converge, it is probably in a loop. Hence, this parameter can be safely hardcoded.

    bool ok = iDynTree::geodesicL2MeanRotation(m_errorData, meanError, options);
    if (!ok)
    {
        outputStream << errorPrefix << "Failed to compute the mean rotation.";
        m_output = outputStream.str();
        return m_output;
    }

    double minError = - 1;
    double maxError = -1;
    size_t minIndex, maxIndex;

    for (size_t i = 0; i < m_errorData.size(); ++i)
    {
        double error = iDynTree::geodesicL2Distance(iDynTree::Rotation::Identity(), m_errorData[i]);
        if ((i > 0) && ((minError < 0) || (error < minError))) //Avoiding to pick the very first data point as it may be too close to the initialization
        {
            minError = error;
            minIndex = i;
        }

        if ((maxError < 0) || (error > maxError))
        {
            maxError = error;
            maxIndex = i;
        }
    }

    outputStream << errorPrefix << "Inertial calibration matrix:" << std::endl
                 << "--------------------------------------" << std::endl
                 << m_imuWorld.toString()
                 << rpyPrinter(m_imuWorld) << std::endl
                 << "--------------------------------------" << std::endl
                 << "Results ("<<m_errorData.size() << " samples) :" << std::endl
                 << "--------------------------------------" << std::endl
                 << "--------------Mean Rotation-----------" << std::endl
                 << meanError.toString()
                 << rpyPrinter(meanError) << std::endl
                 << "----------------Min Error-------------" << std::endl
                 << "Index: " << minIndex  << std::endl
                 << m_errorData[minIndex].toString()
                 << rpyPrinter(m_errorData[minIndex]) << std::endl
                 << "----------------Max Error-------------" << std::endl
                 << "Index: " << maxIndex  << std::endl
                 << m_errorData[maxIndex].toString()
                 << rpyPrinter(m_errorData[maxIndex]) << std::endl
                 << "--------------------------------------" << std::endl;

    m_output = outputStream.str();
    return m_output;
}

bool MasImuTest::MasImuData::saveResults()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::saveResults](" + m_testName +") ";

    matioCpp::StructArray dataArray(m_logPrefix + "_data", {m_errorData.size(), 1}, {"RotationError",
                                                                                     "RotationFromIMU",
                                                                                     "RotationFromIMUInInertial",
                                                                                     "RotationFromIMUInInertialYawFiltered",
                                                                                     "RotationFromEncoders",
                                                                                     "JointPositions_rad",
                                                                                     "RPYfromIMUinDeg",
                                                                                     "AngularVelocity_deg_s",
                                                                                     "Accelerometer"});

    for (size_t i = 0; i < m_errorData.size(); ++i)
    {
        matioCpp::StructArrayElement el = dataArray[{i, 0}];
        if (!el.setField(tomatioCpp(m_errorData[i], "RotationError")))
        {
            yError() << errorPrefix << "Failed to set the field RotationError.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_rotationFeedbackData[i], "RotationFromIMU")))
        {
            yError() << errorPrefix << "Failed to set the field RotationError.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_rotationFeedbackInInertialData[i], "RotationFromIMUInInertial")))
        {
            yError() << errorPrefix << "Failed to set the field RotationError.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_rotationFeedbackInInertialYawFilteredData[i], "RotationFromIMUInInertialYawFiltered")))
        {
            yError() << errorPrefix << "Failed to set the field RotationError.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_rotationFromEncodersData[i], "RotationFromEncoders")))
        {
            yError() << errorPrefix << "Failed to set the field RotationError.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_jointsPositionData[i], "JointPositions_rad")))
        {
            yError() << errorPrefix << "Failed to set the field RotationError.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_rpyImuData[i], "RPYfromIMUinDeg")))
        {
            yError() << errorPrefix << "Failed to set the field RotationError.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_gyroData[i], "AngularVelocity_deg_s")))
        {
            yError() << errorPrefix << "Failed to set the field AngularVelocity_deg_s.";
            return false;
        }

        if(!el.setField(tomatioCpp(m_accData[i], "Accelerometer")))
        {
            yError() << errorPrefix << "Failed to set the field Accelerometer.";
            return false;
        }
    }

    std::vector<matioCpp::Variable> options;
    options.push_back(tomatioCpp(m_imuName, "IMUSensorName"));
    options.push_back(tomatioCpp(m_gyroName, "GyroName"));
    options.push_back(tomatioCpp(m_accName, "AccelerometerName"));
    options.push_back(tomatioCpp(m_frameName, "FrameName"));
    options.push_back(tomatioCpp(m_consideredJointNames, "ConsideredJoints"));
    options.push_back(tomatioCpp(m_imuWorld, "I_R_world"));

    matioCpp::Struct optionsStruct(m_logPrefix + "_options", options);

    if (!optionsStruct.isValid())
    {
        yError() << errorPrefix << "Failed to create the options struct.";
        return false;
    }

    matioCpp::String outputString(m_logPrefix + "_outputString", m_output);

    yInfo() << errorPrefix << "Saving data";

    matioCpp::File outputFile(m_commonDataPtr->outputFile);
    if (!(outputFile.isOpen()))
    {
        yError() << errorPrefix << "Failed to open file.";
        return false;
    }

    if (!outputFile.write(dataArray))
    {
        yError() << errorPrefix << "Failed to write the data array to file.";
        return false;
    }

    if (!outputFile.write(optionsStruct))
    {
        yError() << errorPrefix << "Failed to write the options struct to file.";
        return false;
    }

    if (!outputFile.write(outputString))
    {
        yError() << errorPrefix << "Failed to write the output string to file.";
        return false;
    }

    return true;
}

void MasImuTest::MasImuData::reset()
{
    clearData();
    m_completed = false;
}

bool MasImuTest::MasImuData::close()
{
    std::string errorPrefix = "[MasImuTest::MasImuData::close](" + m_testName +") ";

    if (!saveResults())
    {
        yError() << errorPrefix << "Unable to save the results.";
        return false;
    }

    if(!m_orientationDriver.close())
    {
        yError() << errorPrefix << "Unable to close the orientation driver.";
        return false;
    }

    if(!m_robotDriver.close())
    {
        yError() << errorPrefix << "Unable to close the robot driver.";
        return false;
    }

    return true;
}


void MasImuTest::reset()
{
    m_state = State::PREPARED;
    m_leftIMU.reset();
    m_rightIMU.reset();
}

void MasImuTest::printResultsPrivate()
{
    yInfo() << m_leftIMU.printResults();
    yInfo() << m_rightIMU.printResults();
}

double MasImuTest::getPeriod()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_period;
}

bool MasImuTest::updateModule()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state == State::RUNNING)
    {

        bool ok = m_leftIMU.addSample();
        if (!ok)
        {
            yError() << "[MasImuTest::updateModule] Failed to add data. Marking it as completed.";
            m_leftIMU.setCompleted();
        }

        ok = m_rightIMU.addSample();
        if (!ok)
        {
            yError() << "[MasImuTest::updateModule] Failed to add data. Marking it as completed.";
            m_rightIMU.setCompleted();
        }

        if (m_leftIMU.isCompleted() && m_rightIMU.isCompleted())
        {
            printResultsPrivate();
            m_state = State::PREPARED;
        }

    }

    if (m_state == State::FIRST_RUN)
    {
        bool okL = m_leftIMU.firstRun();
        bool okR = m_rightIMU.firstRun();

        if (!okL || !okR)
        {
            yError() << "[MasImuTest::updateModule] Failed to perform first run.";
            m_state = State::PREPARED;
            return true;
        }

        m_state = State::RUNNING;
    }

    return true;
}

bool MasImuTest::configure(yarp::os::ResourceFinder &rf)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_parametersPtr = std::make_shared<BipedalLocomotion::ParametersHandler::YarpImplementation>(rf);
    m_commonDataPtr = std::make_shared<CommonData>();

    bool ok = m_parametersPtr->getParameter("name", m_commonDataPtr->prefix);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    ok = m_parametersPtr->getParameter("period", m_period);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    if (m_period < 0)
    {
        yError() << "[MasImuTest::configure] The period cannot be negative. Configuration failed.";
        return false;
    }

    ok = m_parametersPtr->getParameter("robot", m_commonDataPtr->robotName);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }
    std::string robotModelName;
    ok = m_parametersPtr->getParameter("model", robotModelName);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }
    std::string pathToModel = yarp::os::ResourceFinder::getResourceFinderSingleton().findFileByName(robotModelName);
    yInfo() << "[MasImuTest::configure] Robot model path: " << pathToModel;
    iDynTree::ModelLoader modelLoader;
    if (!modelLoader.loadModelFromFile(pathToModel))
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    m_commonDataPtr->fullModel = modelLoader.model();

    std::string baseLink;
    ok = m_parametersPtr->getParameter("base_link", baseLink);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    iDynTree::LinkIndex baseLinkIndex = m_commonDataPtr->fullModel.getLinkIndex(baseLink);
    if (baseLinkIndex == iDynTree::LINK_INVALID_INDEX)
    {
        yError() << "[MasImuTest::configure] The link " << baseLink << " does not exists in " << robotModelName
                 << ". Configuration failed.";
        return false;
    }

    ok = m_commonDataPtr->fullModel.computeFullTreeTraversal(m_commonDataPtr->traversal, baseLinkIndex);

    if (!ok)
    {
        yError() << "[MasImuTest::configure] Failed to build the traversal. Configuration failed.";
        return false;
    }

    iDynTree::Rotation baseRotation;
    if(!iDynTree::parseRotationMatrix(rf, "base_rotation", baseRotation))
    {
        baseRotation = iDynTree::Rotation::Identity();
        yInfo() << "[MasImuTest::configure] Using the identity as desired rotation for the additional frame";
    }

    ok = iDynTree::isValidRotationMatrix(baseRotation);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] The specified base rotation is not a rotation matrix.";
        return false;
    }

    m_commonDataPtr->baseTransform = iDynTree::Transform::Identity();
    m_commonDataPtr->baseTransform.setRotation(baseRotation);

    ok = m_parametersPtr->getParameter("filter_yaw", m_commonDataPtr->filterYaw);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    double minJointVariationInDeg;
    ok = m_parametersPtr->getParameter("min_joint_variation_deg", minJointVariationInDeg);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }
    m_commonDataPtr->minJointVariationRad = iDynTree::deg2rad(minJointVariationInDeg);

    ok = m_parametersPtr->getParameter("max_samples", m_commonDataPtr->maxSamples);
    if (!ok || m_commonDataPtr->maxSamples < 0)
    {
        yError() << "[MasImuTest::configure] Configuration failed while reading \"max_samples\".";
        return false;
    }

    ok = m_parametersPtr->getParameter("mas_timeout", m_commonDataPtr->masTimeout);
    if (!ok || m_commonDataPtr->masTimeout < 0)
    {
        yError() << "[MasImuTest::configure] Configuration failed while reading \"mas_timeout\".";
        return false;
    }

    std::string outputFileName;
    ok = m_parametersPtr->getParameter("file_name", outputFileName);
    if (!ok || m_commonDataPtr->masTimeout < 0)
    {
        yError() << "[MasImuTest::configure] Configuration failed while reading \"file_name\".";
        return false;
    }

    auto leftLegGroup = m_parametersPtr->getGroup("LEFT_LEG").lock();
    if (!leftLegGroup)
    {
        yError() << "[MasImuTest::configure] LEFT_LEG group not available. Configuration failed.";
        return false;
    }

    ok = m_leftIMU.setup("Left IMU Test", leftLegGroup, m_commonDataPtr, "left");
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    auto rightLegGroup = m_parametersPtr->getGroup("RIGHT_LEG").lock();
    if (!leftLegGroup)
    {
        yError() << "[MasImuTest::configure] RIGHT_LEG group not available. Configuration failed.";
        return false;
    }

    ok = m_rightIMU.setup("Right IMU Test", rightLegGroup, m_commonDataPtr, "right");
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    matioCpp::File::Delete(outputFileName);

    matioCpp::File outputFile = matioCpp::File::Create(outputFileName);
    m_commonDataPtr->outputFile = outputFileName;

    if (!(outputFile.isOpen()))
    {
        yError() << "[MasImuTest::configure] Failed to create new file with the name " << outputFileName <<".";
        return false;
    }

    std::vector<matioCpp::Variable> settings;
    settings.push_back(tomatioCpp(m_commonDataPtr->robotName, "robot_name"));
    settings.push_back(tomatioCpp(m_period, "period"));
    settings.push_back(tomatioCpp(pathToModel, "model"));
    settings.push_back(tomatioCpp(baseLink, "base_link"));
    settings.push_back(tomatioCpp(baseRotation, "base_rotation"));
    settings.push_back(tomatioCpp(m_commonDataPtr->filterYaw, "filter_yaw"));
    settings.push_back(tomatioCpp(minJointVariationInDeg, "min_joint_variation_deg"));
    settings.push_back(tomatioCpp(m_commonDataPtr->maxSamples, "max_samples"));
    settings.push_back(tomatioCpp(m_commonDataPtr->masTimeout, "mas_timeout"));

    if (!outputFile.write(matioCpp::Struct("settings", settings)))
    {
        yError() << "[MasImuTest::configure] Error while writing the settings to file";
        return false;
    }

    // open RPC port for external command
    std::string rpcPortName = "/" + m_commonDataPtr->prefix + "/rpc";
    this->yarp().attachAsServer(this->m_rpcPort);
    if(!m_rpcPort.open(rpcPortName))
    {
        yError() << "[MasImuTest::configure] Could not open" << rpcPortName << " RPC port.";
        return false;
    }

    bool autoStart;
    ok = m_parametersPtr->getParameter("auto_start", autoStart);
    if (!ok)
    {
        yError() << "[MasImuTest::configure] Configuration failed.";
        return false;
    }

    m_state = State::PREPARED;

    yInfo() << "[MasImuTest::configure] Ready!";


    if (autoStart)
    {
        m_state = State::FIRST_RUN;

        yInfo() << "[MasImuTest::startTest] Started!";
    }

    return true;
}

bool MasImuTest::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_rpcPort.close();

    bool okL = m_leftIMU.close();
    if (!okL)
    {
        yError() << "[MasImuTest::close] Failed to close left leg part.";
    }
    bool okR = m_rightIMU.close();
    if (!okR)
    {
        yError() << "[MasImuTest::close] Failed to close right leg part.";
    }

    return okL && okR;

}

bool MasImuTest::startTest()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::PREPARED)
    {
        reset();
        m_state = State::FIRST_RUN;

        yInfo() << "[MasImuTest::startTest] Started!";

        return true;
    }

    yError() << "[MasImuTest::startTest] The module is not ready yet.";

    return false;

}

bool MasImuTest::stopTest()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::RUNNING)
    {
        printResultsPrivate();
        m_state = State::PREPARED;
        return true;
    }

    yError() << "[MasImuTest::startTest] The test is not ready yet.";

    return false;
}

bool MasImuTest::printResults()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::PREPARED)
    {
        printResultsPrivate();
        return true;
    }

    yError() << "[MasImuTest::startTest] The results can be printed only after stopping the test.";

    return false;
}

void MasImuTest::quit()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    stopModule();
}

