/** \file bicgstab2.h
 *  \brief BiCGStab(L) preconditioned iterative Krylov solver.
 */

#ifndef SAP_BICGSTAB_2_H
#define SAP_BICGSTAB_2_H

#include <vector>

#ifdef   USE_OLD_CUSP
#include <cusp/blas.h>
#else
#include <cusp/blas/blas.h>
#endif
#include <cusp/multiply.h>
#include <cusp/array1d.h>

#include <sap/monitor.h>


namespace sap {

/// Preconditioned BiCGStab(L) Krylov method
/**
 * \tparam LinearOperator is a functor class for sparse matrix-vector product.
 * \tparam Vector is the vector type for the linear system solution.
 * \tparam Monitor is the convergence test object.
 * \tparam Preconditioner is the preconditioner.
 * \tparam L is the degree of the BiCGStab(L) method.
 */
template <typename LinearOperator, typename Vector, typename Monitor, typename Preconditioner, int L>
void bicgstabl(LinearOperator&  A,
               Vector&          x,
               const Vector&    b,
               Monitor&         monitor,
               Preconditioner&  P)
{
	typedef typename Vector::value_type   ValueType;
	typedef typename Vector::memory_space MemorySpace;

	// Allocate workspace
	int  n = b.size();

    const ValueType eps = 1e-20;

	ValueType rho0  = ValueType(1);
	ValueType alpha = ValueType(0);
	ValueType omega = ValueType(1);
	ValueType rho1;

	cusp::array1d<ValueType,MemorySpace>  r0(n);
	cusp::array1d<ValueType,MemorySpace>  r(n);
	cusp::array1d<ValueType,MemorySpace>  u(n,0);
	cusp::array1d<ValueType,MemorySpace>  xx(n);
	cusp::array1d<ValueType,MemorySpace>  Pv(n);

	std::vector<cusp::array1d<ValueType,MemorySpace> >  rr(L+1);
	std::vector<cusp::array1d<ValueType,MemorySpace> >  uu(L+1);

	for(int k = 0; k <= L; k++) {
		rr[k].resize(n, 0);
		uu[k].resize(n, 0);
	}

	ValueType tao[L+1][L+1];
	ValueType gamma[L+2];
	ValueType gamma_prime[L+2];
	ValueType gamma_primeprime[L+2];
	ValueType sigma[L+2];

	// r0 <- b - A * x
	cusp::multiply(A, x, r0);
	cusp::blas::axpby(b, r0, r0, ValueType(1), ValueType(-1));

	// r <- r0
	cusp::blas::copy(r0, r);

	// uu(0) <- u
	// rr(0) <- r
	// xx <- x
	thrust::copy(thrust::make_zip_iterator(thrust::make_tuple(u.begin(), x.begin(), r.begin())), 
	             thrust::make_zip_iterator(thrust::make_tuple(u.end(), x.end(), r.end())), 
	             thrust::make_zip_iterator(thrust::make_tuple(uu[0].begin(), xx.begin(), rr[0].begin())));

    ValueType r_norm_min = cusp::blas::nrm2(r);
    ValueType r_norm = r_norm_min;
    ValueType r_norm_act = r_norm;

	cusp::array1d<ValueType,MemorySpace>  x_min(n, ValueType(0));

	while(true) {

		rho0 = -omega * rho0;

		monitor.increment(0.25f);

		for(int j = 0; j < L; j++) {
			rho1 = cusp::blas::dotc(rr[j], r0);

			// failure
			if(rho0 == 0) {
				monitor.stop(-10, "rho0 is zero");
                break;
			}

			ValueType beta = alpha * rho1 / rho0;
			rho0 = rho1;

			for(int i = 0; i <= j; i++) {
				// uu(i) = rr(i) - beta * uu(i)
				cusp::blas::axpby(rr[i], uu[i], uu[i], ValueType(1), -beta);
			}

			// uu(j+1) <- A * P^(-1) * uu(j);
			cusp::multiply(P, uu[j], Pv);
			cusp::multiply(A, Pv, uu[j+1]);

			// gamma <- uu(j+1) . r0;
			ValueType gamma = cusp::blas::dotc(uu[j+1], r0);

			if(gamma == 0) {
				monitor.stop(-11, "gamma is zero");
                break;
			}

			alpha = rho0 / gamma;

			for(int i = 0; i <= j; i++) {
				// rr(i) <- rr(i) - alpha * uu(i+1)
				cusp::blas::axpy(uu[i+1], rr[i], ValueType(-alpha));
			}

            r_norm_act = r_norm = cusp::blas::nrm2(rr[0]);

			// rr(j+1) = A * P^(-1) * rr(j)
			cusp::multiply(P, rr[j], Pv);
			cusp::multiply(A, Pv, rr[j+1]);

            if (std::fabs(alpha) * cusp::blas::nrm2(uu[0]) < eps * cusp::blas::nrm2(xx)) {
                monitor.incrementStag();
            } else {
                monitor.resetStag();
            }

			// xx <- xx + alpha * uu(0)
			cusp::blas::axpy(uu[0], xx, alpha);

            if(monitor.needCheckConvergence(r_norm)) {
                cusp::array1d<ValueType,MemorySpace>  Pxx(n);
                cusp::array1d<ValueType,MemorySpace>  APxx(n);

                // APxx <- A * P^{-1} * xx
				cusp::multiply(P, xx, Pxx);
				cusp::multiply(A, Pxx, APxx);

                // rr(0) <- b - APxx
                cusp::blas::axpby(b, APxx, rr[0], ValueType(1), ValueType(-1));
                r_norm_act = cusp::blas::nrm2(rr[0]);

                if (monitor.finished(r_norm_act)) {
                    break;
                }
            }

            if (r_norm_act < r_norm_min) {
                r_norm_min = r_norm_act;
                // x_min <- xx
                cusp::blas::copy(xx, x_min);
            }

            if (monitor.finished()) {
                break;
            }
		}

        if (monitor.finished()) {
            break;
        }


		for(int j = 1; j <= L; j++) {
			for(int i = 1; i < j; i++) {
				tao[i][j] = cusp::blas::dotc(rr[j], rr[i]) / sigma[i];
				cusp::blas::axpy(rr[i], rr[j], -tao[i][j]);
			}
			sigma[j] = cusp::blas::dotc(rr[j], rr[j]);
			if(sigma[j] == 0) {
				monitor.stop(-12, "a sigma value is zero");
                break;
			}
			gamma_prime[j] = cusp::blas::dotc(rr[j], rr[0]) / sigma[j];
		}
        if (monitor.finished()) {
            break;
        }

		gamma[L] = gamma_prime[L];
		omega = gamma[L];

		for(int j = L-1; j > 0; j--) {
			gamma[j] = gamma_prime[j];
			for(int i = j+1; i <= L; i++)
				gamma[j] -= tao[j][i] * gamma[i];
		}

		for(int j = 1; j < L; j++) {
			gamma_primeprime[j] = gamma[j+1];
			for(int i = j+1; i < L; i++)
				gamma_primeprime[j] += tao[j][i] * gamma[i+1];
		}

        if (std::fabs(gamma[1]) * cusp::blas::nrm2(rr[0]) < eps * cusp::blas::nrm2(xx)) {
            monitor.incrementStag();
        } else {
            monitor.resetStag();
        }

		// xx    <- xx    + gamma * rr(0)
		// rr(0) <- rr(0) - gamma'(L) * rr(L)
		// uu(0) <- uu(0) - gamma(L) * uu(L)
		cusp::blas::axpy(rr[0], xx,    gamma[1]);
		cusp::blas::axpy(rr[L], rr[0], -gamma_prime[L]);
		cusp::blas::axpy(uu[L], uu[0], -gamma[L]);

        r_norm_act = r_norm = cusp::blas::nrm2(rr[0]);

		monitor.increment(0.25f);

        if(monitor.needCheckConvergence(r_norm)) {
            cusp::array1d<ValueType,MemorySpace>  Pxx(n);
            cusp::array1d<ValueType,MemorySpace>  APxx(n);

            // APxx <- A * P^{-1} * xx
            cusp::multiply(P, xx, Pxx);
            cusp::multiply(A, Pxx, APxx);

            // rr(0) <- b - APxx
            cusp::blas::axpby(b, APxx, rr[0], ValueType(1), ValueType(-1));
            r_norm_act = cusp::blas::nrm2(rr[0]);

            if (monitor.finished(r_norm_act)) {
                break;
            }
        }

        if (r_norm_act < r_norm_min) {
            r_norm_min = r_norm_act;
            // x_min <- xx
            cusp::blas::copy(xx, x_min);
        }

        if (monitor.finished()) {
            break;
        }

		monitor.increment(0.25f);

		// uu(0) <- uu(0) - sum_j { gamma(j) * uu(j) }
		// xx    <- xx    + sum_j { gamma''(j) * rr(j) }
		// rr(0) <- rr(0) - sum_j { gamma'(j) * rr(j) }
		for(int j = 1; j < L; j++) {
			cusp::blas::axpy(uu[j], uu[0],  -gamma[j]);

            if (std::fabs(gamma_primeprime[j]) * cusp::blas::nrm2(rr[j]) < eps * cusp::blas::nrm2(xx)) {
                monitor.incrementStag();
            } else {
                monitor.resetStag();
            }
			cusp::blas::axpy(rr[j], xx,     gamma_primeprime[j]);
			cusp::blas::axpy(rr[j], rr[0],  -gamma_prime[j]);

            r_norm_act = r_norm = cusp::blas::nrm2(rr[0]);

            if(monitor.needCheckConvergence(r_norm)) {
                cusp::array1d<ValueType,MemorySpace>  Pxx(n);
                cusp::array1d<ValueType,MemorySpace>  APxx(n);

                // APxx <- A * P^{-1} * xx
				cusp::multiply(P, xx, Pxx);
				cusp::multiply(A, Pxx, APxx);

                // rr(0) <- b - APxx
                cusp::blas::axpby(b, APxx, rr[0], ValueType(1), ValueType(-1));
                r_norm_act = cusp::blas::nrm2(rr[0]);

                if (monitor.finished(r_norm_act)) {
                    break;
                }
            }

            if (r_norm_act < r_norm_min) {
                r_norm_min = r_norm_act;
                // x_min <- xx
                cusp::blas::copy(xx, x_min);
            }

            if (monitor.finished()) {
                break;
            }
		}

        if (monitor.finished()) {
            break;
        }

		// u <- uu(0)
		// x <- xx
		// r <- rr(0)
		thrust::copy(thrust::make_zip_iterator(thrust::make_tuple(uu[0].begin(), xx.begin(), rr[0].begin())), 
		             thrust::make_zip_iterator(thrust::make_tuple(uu[0].end(), xx.end(), rr[0].end())), 
		             thrust::make_zip_iterator(thrust::make_tuple(u.begin(), x.begin(), r.begin())));

		monitor.increment(0.25f);
	}

    if (monitor.converged()) {
        // x <- P^{-1} * xx
        cusp::multiply(P, xx, x);
    } else {
        cusp::array1d<ValueType,MemorySpace>  Pxx(n);
        cusp::array1d<ValueType,MemorySpace>  APxx(n);
        cusp::array1d<ValueType,MemorySpace>  Pxmin(n);
        cusp::array1d<ValueType,MemorySpace>  APxmin(n);
        cusp::array1d<ValueType,MemorySpace>  r_comp(n);
        cusp::array1d<ValueType,MemorySpace>  r_comp_min(n);

        // APxx <- A * P^{-1} * xx
        cusp::multiply(P, xx, Pxx);
        cusp::multiply(A, Pxx, APxx);

        // r_comp <- b - APxx
        cusp::blas::axpby(b, APxx, r_comp, ValueType(1), ValueType(-1));

        // APxmin <- P^{-1} * x_min
        cusp::multiply(P, x_min, Pxmin);
        cusp::multiply(A, Pxmin, APxmin);

        // r_comp_min <- b - APxmin
        cusp::blas::axpby(b, APxmin, r_comp_min, ValueType(1), ValueType(-1));

        ValueType r_comp_norm = cusp::blas::nrm2(r_comp);
        ValueType r_comp_min_norm = cusp::blas::nrm2(r_comp_min);

        if (r_comp_norm < r_comp_min_norm) {
            // x <- Pxx
            cusp::blas::copy(Pxx, x);
            monitor.updateResidual(r_comp_norm);
        } else {
            // x <- Pxmin
            cusp::blas::copy(Pxmin, x);
            monitor.updateResidual(r_comp_min_norm);
        }
    }
}

/// Specializations of the generic sap::bicgstabl function for L=1
template <typename LinearOperator, typename Vector, typename Monitor, typename Preconditioner>
void bicgstab1(LinearOperator&  A,
               Vector&          x,
               const Vector&    b,
               Monitor&         monitor,
               Preconditioner&  P)
{
	bicgstabl<LinearOperator, Vector, Monitor, Preconditioner, 1>(A, x, b, monitor, P);
}

/// Specializations of the generic sap::bicgstabl function for L=2
template <typename LinearOperator, typename Vector, typename Monitor, typename Preconditioner>
void bicgstab2(LinearOperator&  A,
               Vector&          x,
               const Vector&    b,
               Monitor&         monitor,
               Preconditioner&  P)
{
	bicgstabl<LinearOperator, Vector, Monitor, Preconditioner, 2>(A, x, b, monitor, P);
}



} // namespace sap



#endif

