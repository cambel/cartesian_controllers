// -- BEGIN LICENSE BLOCK -----------------------------------------------------
// -- END LICENSE BLOCK -------------------------------------------------------

//-----------------------------------------------------------------------------
/*!\file    cartesian_compliance_controller.hpp
 *
 * \author  Stefan Scherzinger <scherzin@fzi.de>
 * \date    2017/07/27
 *
 */
//-----------------------------------------------------------------------------

#ifndef CARTESIAN_COMPLIANCE_CONTROLLER_HPP_INCLUDED
#define CARTESIAN_COMPLIANCE_CONTROLLER_HPP_INCLUDED

// Project
#include <cartesian_compliance_controller/cartesian_compliance_controller.h>

// Other
#include <boost/algorithm/clamp.hpp>
#include <map>

namespace cartesian_compliance_controller
{

template <class HardwareInterface>
CartesianComplianceController<HardwareInterface>::
CartesianComplianceController()
// Base constructor won't be called in diamond inheritance, so call that
// explicitly
: Base::CartesianControllerBase(),
  MotionBase::CartesianMotionController(),
  ForceBase::CartesianForceController()
{
}

template <class HardwareInterface>
bool CartesianComplianceController<HardwareInterface>::
init(HardwareInterface* hw, ros::NodeHandle& nh)
{
  // Only one of them will call Base::init(hw,nh);
  MotionBase::init(hw,nh);
  ForceBase::init(hw,nh);

  if (!nh.getParam("compliance_ref_link",m_compliance_ref_link))
  {
    ROS_ERROR_STREAM("Failed to load " << nh.getNamespace() + "/compliance_ref_link" << " from parameter server");
    return false;
  }

  // Compliance control w. r. t. the compliance reference link
  ForceBase::m_ft_sensor_target_ref_link = m_compliance_ref_link;

  // Connect dynamic reconfigure and overwrite the default values with values
  // on the parameter server. This is done automatically if parameters with
  // the according names exist.
  m_callback_type = boost::bind(
      &CartesianComplianceController<HardwareInterface>::dynamicReconfigureCallback, this, _1, _2);

  m_dyn_conf_server.reset(
      new dynamic_reconfigure::Server<ComplianceConfig>(
        ros::NodeHandle(nh.getNamespace() + "/stiffness")));
  m_dyn_conf_server->setCallback(m_callback_type);

  return true;
}

template <class HardwareInterface>
void CartesianComplianceController<HardwareInterface>::
starting(const ros::Time& time)
{
  // Base::starting(time) will get called twice,
  // but that's fine.
  MotionBase::starting(time);
  ForceBase::starting(time);
}

template <class HardwareInterface>
void CartesianComplianceController<HardwareInterface>::
stopping(const ros::Time& time)
{
  MotionBase::stopping(time);
  ForceBase::stopping(time);
}

template <class HardwareInterface>
void CartesianComplianceController<HardwareInterface>::
update(const ros::Time& time, const ros::Duration& period)
{
  // Control the robot motion in such a way that the resulting net force
  // vanishes. This internal control needs some simulation time steps.
  const int steps = 10;
  for (int i = 0; i < steps; ++i)
  {
    // The internal 'simulation time' is deliberately independent of the outer
    // control cycle.
    ros::Duration internal_period(0.02);

    // Compute the net force
    ctrl::Vector6D error = computeComplianceError();

    // Turn Cartesian error into joint motion
    Base::computeJointControlCmds(error,internal_period);
  }

  // Write final commands to the hardware interface
  Base::writeJointControlCmds();
}

template <>
void CartesianComplianceController<hardware_interface::VelocityJointInterface>::
update(const ros::Time& time, const ros::Duration& period)
{
  // Simulate only one step forward.
  // The constant simulation time adds to solver stability.
  ros::Duration internal_period(0.02);

  ctrl::Vector6D error = computeComplianceError();

  Base::computeJointControlCmds(error,internal_period);

  Base::writeJointControlCmds();
}

template <class HardwareInterface>
ctrl::Vector6D CartesianComplianceController<HardwareInterface>::
computeComplianceError()
{
  ctrl::Vector6D net_force =

    // Spring force in base orientation
    Base::displayInBaseLink(m_stiffness,m_compliance_ref_link) * MotionBase::computeMotionError()

    // Sensor and target force in base orientation
    + ForceBase::computeForceError();

  return net_force;
}

template <class HardwareInterface>
void CartesianComplianceController<HardwareInterface>::
dynamicReconfigureCallback(ComplianceConfig& config, uint32_t level)
{
  ctrl::Vector6D tmp;
  tmp[0] = config.trans_x;
  tmp[1] = config.trans_y;
  tmp[2] = config.trans_z;
  tmp[3] = config.rot_x;
  tmp[4] = config.rot_y;
  tmp[5] = config.rot_z;
  m_stiffness = tmp.asDiagonal();
}

} // namespace

#endif
