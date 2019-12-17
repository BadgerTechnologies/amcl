/**************************************************************************
 * Desc: Eigen-decomposition functions
 * Maintainter: Tyler Buchman (tyler_buchman@jabil.com)
 *************************************************************************/

/* Eigen decomposition code for symmetric 3x3 matrices, copied from the public
   domain Java Matrix library JAMA. */

#include "pf/eig3.h"

#include <math.h>

#include <algorithm>
#include <cmath>

using namespace amcl;

double
EIG3::hypot2(double x, double y) {
  return sqrt(x*x+y*y);
}

// Symmetric Householder reduction to tridiagonal form.

void
EIG3::tred2(std::vector<std::vector<double>> V, std::vector<double> d, std::vector<double> e) {

//  This is derived from the Algol procedures tred2 by
//  Bowdler, Martin, Reinsch, and Wilkinson, Handbook for
//  Auto. Comp., Vol.ii-Linear Algebra, and the corresponding
//  Fortran subroutine in EISPACK.

  int i,j,k;
  double f,g,h,hh;
  for (j = 0; j < N; j++) {
    d[j] = V[N-1][j];
  }

  // Householder reduction to tridiagonal form.

  for (i = N-1; i > 0; i--) {
    // Scale to avoid under/overflow.

    double scale = 0.0;
    double h = 0.0;
    for (k = 0; k < i; k++) {
      scale = scale + fabs(d[k]);
    }
    if (scale == 0.0) {
      e[i] = d[i-1];
      for (j = 0; j < i; j++) {
        d[j] = V[i-1][j];
        V[i][j] = 0.0;
        V[j][i] = 0.0;
      }
    } else {
      // Generate Householder vector.

      for (k = 0; k < i; k++) {
        d[k] /= scale;
        h += d[k] * d[k];
      }
      f = d[i-1];
      g = sqrt(h);
      if (f > 0) {
        g = -g;
      }
      e[i] = scale * g;
      h = h - f * g;
      d[i-1] = f - g;
      for (j = 0; j < i; j++) {
        e[j] = 0.0;
      }

      // Apply similarity transformation to remaining columns.

      for (j = 0; j < i; j++) {
        f = d[j];
        V[j][i] = f;
        g = e[j] + V[j][j] * f;
        for (k = j+1; k <= i-1; k++) {
          g += V[k][j] * d[k];
          e[k] += V[k][j] * f;
        }
        e[j] = g;
      }
      f = 0.0;
      for (j = 0; j < i; j++) {
        e[j] /= h;
        f += e[j] * d[j];
      }
      hh = f / (h + h);
      for (j = 0; j < i; j++) {
        e[j] -= hh * d[j];
      }
      for (j = 0; j < i; j++) {
        f = d[j];
        g = e[j];
        for (k = j; k <= i-1; k++) {
          V[k][j] -= (f * e[k] + g * d[k]);
        }
        d[j] = V[i-1][j];
        V[i][j] = 0.0;
      }
    }
    d[i] = h;
  }

  // Accumulate transformations.

  for (i = 0; i < N-1; i++) {
    V[N-1][i] = V[i][i];
    V[i][i] = 1.0;
    h = d[i+1];
    if (h != 0.0) {
      for (k = 0; k <= i; k++) {
        d[k] = V[k][i+1] / h;
      }
      for (j = 0; j <= i; j++) {
        g = 0.0;
        for (k = 0; k <= i; k++) {
          g += V[k][i+1] * V[k][j];
        }
        for (k = 0; k <= i; k++) {
          V[k][j] -= g * d[k];
        }
      }
    }
    for (k = 0; k <= i; k++) {
      V[k][i+1] = 0.0;
    }
  }
  for (j = 0; j < N; j++) {
    d[j] = V[N-1][j];
    V[N-1][j] = 0.0;
  }
  V[N-1][N-1] = 1.0;
  e[0] = 0.0;
}

// Symmetric tridiagonal QL algorithm.

void
EIG3::tql2(std::vector<std::vector<double>> V, std::vector<double> d, std::vector<double> e) {

//  This is derived from the Algol procedures tql2, by
//  Bowdler, Martin, Reinsch, and Wilkinson, Handbook for
//  Auto. Comp., Vol.ii-Linear Algebra, and the corresponding
//  Fortran subroutine in EISPACK.

  int i,j,m,l,k;
  double g,p,r,dl1,h,f,tst1,eps;
  double c,c2,c3,el1,s,s2;

  for (i = 1; i < N; i++) {
    e[i-1] = e[i];
  }
  e[N-1] = 0.0;

  f = 0.0;
  tst1 = 0.0;
  eps = pow(2.0,-52.0);
  for (l = 0; l < N; l++) {

    // Find small subdiagonal element

    tst1 = std::max(fabs(d[l]) + fabs(e[l]), tst1);
    m = l;
    while (m < N) {
      if (fabs(e[m]) <= eps*tst1) {
        break;
      }
      m++;
    }

    // If m == l, d[l] is an eigenvalue,
    // otherwise, iterate.

    if (m > l) {
      int iter = 0;
      do {
        iter = iter + 1;  // (Could check iteration count here.)

        // Compute implicit shift

        g = d[l];
        p = (d[l+1] - g) / (2.0 * e[l]);
        r = hypot2(p,1.0);
        if (p < 0) {
          r = -r;
        }
        d[l] = e[l] / (p + r);
        d[l+1] = e[l] * (p + r);
        dl1 = d[l+1];
        h = g - d[l];
        for (i = l+2; i < N; i++) {
          d[i] -= h;
        }
        f = f + h;

        // Implicit QL transformation.

        p = d[m];
        c = 1.0;
        c2 = c;
        c3 = c;
        el1 = e[l+1];
        s = 0.0;
        s2 = 0.0;
        for (i = m-1; i >= l; i--) {
          c3 = c2;
          c2 = c;
          s2 = s;
          g = c * e[i];
          h = c * p;
          r = hypot2(p,e[i]);
          e[i+1] = s * r;
          s = e[i] / r;
          c = p / r;
          p = c * d[i] - s * g;
          d[i+1] = h + s * (c * g + s * d[i]);

          // Accumulate transformation.

          for (k = 0; k < N; k++) {
            h = V[k][i+1];
            V[k][i+1] = s * V[k][i] + c * h;
            V[k][i] = c * V[k][i] - s * h;
          }
        }
        p = -s * s2 * c3 * el1 * e[l] / dl1;
        e[l] = s * p;
        d[l] = c * p;

        // Check for convergence.

      } while (fabs(e[l]) > eps*tst1);
    }
    d[l] = d[l] + f;
    e[l] = 0.0;
  }

  // Sort eigenvalues and corresponding vectors.

  for (i = 0; i < N-1; i++) {
    k = i;
    p = d[i];
    for (j = i+1; j < N; j++) {
      if (d[j] < p) {
        k = j;
        p = d[j];
      }
    }
    if (k != i) {
      d[k] = d[i];
      d[i] = p;
      for (j = 0; j < N; j++) {
        p = V[j][i];
        V[j][i] = V[j][k];
        V[j][k] = p;
      }
    }
  }
}

void
EIG3::eigenDecomposition(std::vector<std::vector<double>> A, std::vector<std::vector<double>> V, std::vector<double> d) {
  int i,j;
  std::vector<double> e(N, 0.0);
  for (i = 0; i < N; i++) {
    for (j = 0; j < N; j++) {
      V[i][j] = A[i][j];
    }
  }
  EIG3::tred2(V, d, e);
  EIG3::tql2(V, d, e);
}