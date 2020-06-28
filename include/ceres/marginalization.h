// Author: evanlevine138e@gmail.com (Evan Levine)

#ifndef CERES_PUBLIC_MARGINALIZATION_H_
#define CERES_PUBLIC_MARGINALIZATION_H_

#include "ceres/cost_function.h"
#include "ceres/parameter_block.h"
#include "ceres/problem.h"
#include "ceres/marginalization_impl.h"

namespace ceres {

// Class that holds data used to construct the cost function,
// MarginalFactorCostFunction, induced by eliminating a set of parameter
// blocks from a problem by marginalizing them out.
//
// Background
// ==========
//
// Marginalization enables solving a problem for a subset of variables of
// interest at reduced computational cost compared to solving the original
// problem. It also entails making a linear approximation of the residuals with
// respect to the parameters to be marginalized out and the parameters that
// separate these variables from the rest of the graph, called the
// Markov blanket. Marginalization removes a subset of nodes and induces a new
// linear constraint on their Markov blanket. The approach here is based on
// descriptions in [1] and [2]. We have to minimize over the local coordinates
// of the variables to marginalize out, while working with the global
// coordinates the variables in the Markov blanket.
//
// Consider a robustified non-linear least squares problem
//
// min_x 0.5 \sum_{i} rho_i(\|f_i(x_i_1, ..., x_i_k)\|^2)
// s.t. l_j \leq x_j \leq u_j
//
// We can partition the variables into the variables to marginalized out,
// denoted x_m, the variables related to them by error terms (their Markov
// blanket), denoted x_b, and the remaining variables x_r.
//
// min_x 0.5 \sum_{i in dM} rho_i(\|f_i(x_b, x_m)\|^2) +
//       0.5 \sum_{i not in dM} rho_i(\|f_i(x_b, x_r)\|^2),
//
// where dM is the index set of all error terms involving x_m. Let x_b^0 and
// x_m^0 be linearization points for x_b and x_m to be respectively and (+) be
// the oplus operator. We can then make the following linear approximation for
// the first term.
//
// c(x_b, delta_m) = 0.5 \sum_{i in dM} rho_i(\|f_i(x_b, x_m^0(+)delta_m)\|^2)
//                 ~ 0.5 \sum_{i in dM} rho_i(\|f_i(x_b^0, x_m^0) +
//                                            J_i [x_b-x_b^0 ; delta_m]\|^2),
// where J_i = [ df_i/dx_b,  df_i/dx_m dx_m/d_delta_m], ";" denotes vertical
// concatenation, and delta_m is the error state for x_m = x_m^0 (+) delta_m.
//
// c(x_b,delta_m) = (g^T + [x_b-x_b^0; delta_m]^T\Lambda) [x_b-x_b^0; delta_m],
// where g = \sum_i \rho^\prime J_i^T f_i(x_b^0, x_m^0),
// \Lambda = \sum_i \rho^\prime J_i^T J_i.
//
// Partition lambda into the block matrix
// \Lambda = [ \Lambda_{mm} \Lambda_{bm}^T ]
//           [ \Lambda_{bm} \Lambda_{bb}   ].
// and g into the block vector g = [g_{mm}; g_{mb}].
//
// We can minimize c(x_b, delta_m) with respect to delta_m:
//
// argmin_{delta_m} c(x_b, delta_m) =
//   \Lambda_{mm}^-1 (g_{mm} + \Lambda_{mb}(x_b-x_b^0))
//
// Substituting this into c yields
//
// g_t^T(x_b-x_b^0) + 0.5(x_b-x_b^0)\Lambda_t(x_b-x_b^0),
//
// where \Lambda_t = \Lambda_{bb} - \Lambda_{bm}\Lambda_{mm}^{-1}\Lambda_{bm}^T
//             g_t = g_{mb} - \Lambda_{bm}\Lambda_{mm}^{-1}g_{mm}.
//
// We can write this as
//
// \|S^T(x_b-x_b^0 + \Lambda_t^{-1}g_t) \|^2,
//
// where S * S^T = Lambda_t. This is the cost function for the "marginal factor"
// to be added to the graph with the marginalized parameter blocks removed.
//
// [1] Carlevaris-Bianco, Nicholas, Michael Kaess, and Ryan M. Eustice. "Generic
// node removal for factor-graph SLAM." IEEE Transactions on Robotics 30.6
// (2014): 1371-1385.
//
// [2] Eckenhoff, Kevin, Liam Paull, and Guoquan Huang.
// "Decoupled, consistent node removal and edge sparsification for graph-based
// SLAM." 2016 IEEE/RSJ International Conference on Intelligent Robots and
// Systems (IROS). IEEE, 2016.

// Class for the linear cost function induced by marginalization of parameter
// blocks, residual = jacobian * x_b + b.
class CERES_EXPORT MarginalFactorCostFunction : public CostFunction {
 public:
  MarginalFactorCostFunction(const Matrix& jacobian, const Matrix& b,
                             const std::vector<int>& parameter_block_sizes)
      : jacobian_(jacobian), b_(b) {
    set_num_residuals(b_.size());
    for (const int size : parameter_block_sizes) {
      mutable_parameter_block_sizes()->push_back(size);
    }
  }

  virtual bool Evaluate(double const* const* parameters, double* residuals,
                        double** jacobians) const override {
    VectorRef(residuals, num_residuals()) = b_;
    for (int i = 0; i < num_residuals(); ++i) {
      int parameter_block_offset = 0;
      for (int j = 0; j < parameter_block_sizes().size(); ++j) {
        const int block_size = parameter_block_sizes()[j];
        for (int k = 0; k < block_size; ++k) {
          residuals[i] +=
              jacobian_(i, parameter_block_offset + k) * parameters[j][k];
        }
        parameter_block_offset += block_size;
      }
    }

    if (jacobians == NULL) {
      return true;
    }

    for (int i = 0; i < num_residuals(); ++i) {
      int parameter_block_offset = 0;
      for (int j = 0; j < parameter_block_sizes().size(); ++j) {
        const int block_size = parameter_block_sizes()[j];
        for (int k = 0; k < block_size; ++k) {
          jacobians[j][i * block_size + k] =
              jacobian_(i, parameter_block_offset + k);
        }
        parameter_block_offset += block_size;
      }
    }

    return true;
  }

 private:
  Vector b_;
  Matrix jacobian_;
};

class CERES_EXPORT Marginalization {
public:
  explicit Marginalization();
  ~Marginalization();
  
  // Compute the cost function for the marginal factor induced by marginalizing
  // out a subset of variables from the problem.
  bool Compute(const std::set<double*>& parameter_blocks_to_marginalize,
               Problem* problem,
               std::vector<double*>* markov_blanket_parameter_blocks,
               MarginalFactorCostFunction** cost_function);

  // Convenience method that marginalizes out variables, removing
  // them from a problem and adding to the problem a cost function for the
  // marginal factor.
  bool MarginalizeOutVariables(
      const std::set<double*>& parameter_blocks_to_marginalize,
      Problem* problem);

 private:
  std::unique_ptr<internal::MarginalizationImpl> impl_;
};

}  // namespace ceres

#endif  // CERES_PUBLIC_MARGINALIZATION_H_