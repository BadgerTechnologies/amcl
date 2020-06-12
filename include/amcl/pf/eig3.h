/* Eigen-decomposition for symmetric 3x3 real matrices.
   Public domain, copied from the public domain Java library JAMA. */

#ifndef AMCL_PF_EIG3_H
#define AMCL_PF_EIG3_H

/* Symmetric matrix A => eigenvectors in columns of V, corresponding
   eigenvalues in d. */

#include <Eigen/Dense>

namespace badger_amcl
{
class EIG3
{
public:
  static void eigenDecomposition(const Eigen::Matrix3d& A, Eigen::Matrix3d& V, Eigen::Vector3d& d);

private:
  static constexpr int N = 3;
  static void tred2(Eigen::Matrix3d& V, Eigen::Vector3d& d, Eigen::Vector3d& e);
  static void tql2(Eigen::Matrix3d& V, Eigen::Vector3d& d, Eigen::Vector3d& e);
};

}  // namespace amcl

#endif  // AMCL_PF_EIG3_H
