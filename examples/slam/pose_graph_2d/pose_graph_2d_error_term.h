#ifndef CERES_EXAMPLES_POSE_GRAPH_2D_POSE_GRAPH_2D_ERROR_TERM_H_
#define CERES_EXAMPLES_POSE_GRAPH_2D_POSE_GRAPH_2D_ERROR_TERM_H_

#include "Eigen/Core"
#include "ceres/sized_cost_function.h"

namespace ceres {
namespace examples {
namespace pose_graph_2d {

enum {
  NUM_RESIDUALS = 3,
  X_POSITION_BLOCK_SIZE = 1,
  Y_POSITION_BLOCK_SIZE = 1,
  YAW_BLOCK_SIZE = 1
};

// Computes the error term for two poses that have a relative pose measurement
// between them. Let the hat variables be the measurement.
//
// residual =  information^{1/2} * [  A_R_G * (G_p_B - G_p_A) - \hat{A_p_B}   ]
//                                 [ Normalize(yaw_B - yaw_A - \hat{A_yaw_B}) ]
//
// where A_R_G is the rotation matrix that rotates a vector represented in the
// global frame into frame A (it is the inverse of the rotation matrix built
// from the yaw angle of A, yaw_A), and Normalize(*) ensures the angles are in
// the range [-pi, pi).
class PoseGraph2dErrorTerm
    : public ceres::SizedCostFunction<
          NUM_RESIDUALS,
          // First pose state sizes.
          X_POSITION_BLOCK_SIZE, Y_POSITION_BLOCK_SIZE, YAW_BLOCK_SIZE,
          // Second pose state sizes.
          X_POSITION_BLOCK_SIZE, Y_POSITION_BLOCK_SIZE, YAW_BLOCK_SIZE> {
 public:
  PoseGraph2dErrorTerm(double A_x_B, double A_y_B, double A_yaw_B_radians,
                       const Eigen::Matrix3d& sqrt_information);

  bool Evaluate(double const* const* parameters, double* residuals_ptr,
                double** jacobians) const;

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

 private:
  // The position of B relative to A in the A frame.
  Eigen::Vector2d A_p_B_;
  // The orientation of frame B relative to frame A.
  double A_yaw_B_radians_;
  // The square root of the measurement information matrix.
  Eigen::Matrix3d sqrt_information_;
};

}  // namespace pose_graph_2d
}  // namespace examples
}  // namespace ceres

#endif  // CERES_EXAMPLES_POSE_GRAPH_2D_POSE_GRAPH_2D_ERROR_TERM_H_
