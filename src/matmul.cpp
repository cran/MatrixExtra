#include "MatrixExtra.h"

#ifdef __clang__
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wpass-failed"
#endif

constexpr const int one = 1;
// extern "C" void F77_CALL(saxpy)(const int *, const float*, const float *, const int*, float*, const int*);
// extern "C" void F77_CALL(scopy)(const int *, const float*, const int *, float *, const int *);

/* Note: it'd be faster to take these functions from the 'float' package (which will end up linking
   to BLAS), but it gives too many headaches, especailly in some platforms like windows, and at
   the time of writing, the maintainer of the 'float' package had mostly abandoned it and was
   not replying to PRs fixing linkage issues. If some other package comes out offering single-precision
   BLAS, should be safe to just switch to it instead. */
static inline void F77_CALL(saxpy)(const int *n, const float *a, const float *x, const int *incx, float *y, const int *incy)
{
    const int n_ = *n;
    const int inc_x = *incx;
    const int inc_y = *incy;
    const float alpha = *a;
    if (inc_x == 1 && inc_y == 1 && alpha == 1.0f) {
        for (int ix = 0; ix < n_; ix++) {
            y[ix] += x[ix];
        }
    }
    else {
        for (int ix = 0; ix < n_; ix++) {
            y[ix*inc_y] += alpha * x[ix*inc_x];
        }
    }
}

static inline void F77_CALL(scopy)(const int *n, const float *x, const int *incx, float *y, const int *incy)
{
    for (int ix = 0; ix < *n; ix++) {
        y[ix * (*incy)] = x[ix * (*incx)];
    }
}


static inline void axpy(const int *n, const double* alpha, const double *x, const int* incx, double* y, const int* incy)
{
    F77_CALL(daxpy)(n, alpha, x, incx, y, incy);
}

static inline void axpy(const int *n, const double alpha, const double *x, const int* incx, double* y, const int* incy)
{
    F77_CALL(daxpy)(n, &alpha, x, incx, y, incy);
}

static inline void axpy(const int *n, const double* alpha, const float *x, const int* incx, float* y, const int* incy)
{
    const float alpha_ = *alpha;
    F77_CALL(saxpy)(n, &alpha_, x, incx, y, incy);
}

static inline void axpy(const int *n, const float alpha, const float *x, const int* incx, float* y, const int* incy)
{
    F77_CALL(saxpy)(n, &alpha, x, incx, y, incy);
}

static inline void axpy(const int *n, const double alpha, const float *x, const int* incx, float* y, const int* incy)
{
    const float alpha_ = alpha;
    F77_CALL(saxpy)(n, &alpha_, x, incx, y, incy);
}

static inline void tcopy(const int *n, const double* x, const int *incx, double *y, const int *incy)
{
    F77_CALL(dcopy)(n, x, incx, y, incy);
}

static inline void tcopy(const int *n, const float* x, const int *incx, float *y, const int *incy)
{
    F77_CALL(scopy)(n, x, incx, y, incy);
}

/* Mental map to figure out what should be called where:

matmul(x,y) -> x %*% y
    = ColMajor(Xc * Yc)
    = RowMajor(Yr * Xr)

crossprod(x,y) -> t(x) %*% y
    = ColMajor(Xc.T * Yc)
    = ColMajor(Xr * Yc)
    = RowMajor(Yr * Xc)

tcrossprod(x,y) -> x %*% t(y)
    = ColMajor(Xc * Yc.T)
    = ColMajor(Xc * Yr)
    = RowMajor(Yc * Xr)

The required functions are:
    matmul(CSR, dense)
    tcrossprod(CSR, dense)
    tcrossprod(dense, CSR)
    matmul(dense, CSC)
    crossprod(dense, CSC)

    with all the dense ones being column-major
*/

/* X <- A*B + X | A(m,k) is sparse CSR, B(k,n) is dense row-major, X(m,n) is dense row-major

   Equivalences:
    X <- A*t(B) + X    | A(m,k) CSR, B(n,k) column-major, X(m,n) row-major
    X <- t(A)*B + X    | A(k,m) CSC, B(k,n) row-major, X(m,n) row-major
    X <- t(A)*t(B) + X | A(k,m) CSC, B(n,k) column-major, X(m,n) row-major

    When X is zeroed-out:
    X <- B*t(A) | A(k,m) CSR, B(n,k) column-major, X(n,m) column-major
    X <- B*A    | A(k,m) CSC, B(n,k) column-major, X(n,m) column-major

*/
template <class real_t>
void gemm_csr_drm_as_drm
(
    const int m, const int n,
    const int *restrict indptr, const int *restrict indices, const double *restrict values,
    const real_t *restrict DenseMat, const size_t ldb,
    real_t *restrict OutputMat, const size_t ldc,
    int nthreads
)
{
    if (m <= 0 || indptr[0] == indptr[m])
        return;
    real_t *restrict row_ptr;
    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic) num_threads(nthreads) \
            shared(OutputMat, DenseMat, indptr, indices, values) \
            private(row_ptr)
    #endif
    for (int row = 0; row < m; row++)
    {
        row_ptr = OutputMat + (size_t)row*ldc;
        for (int col = indptr[row]; col < indptr[row+1]; col++)
            axpy(&n, values + col, DenseMat + (size_t)indices[col]*ldb, &one, row_ptr, &one);
    }
}

/* X <- A*B | A(m,k) is sparse CSR, B(k,n) is dense row-major, X(m,n) is dense column-major

   When X is zeroed-out:
   X <- A*t(B) | A(m,k) CSR, B(n,k) column-major, X(m,n) column-major

*/
template <class real_t>
void gemm_csr_drm_as_dcm
(
    const int m, const int n,
    const int *restrict indptr, const int *restrict indices, const double *restrict values,
    const real_t *restrict DenseMat, const size_t ldb,
    real_t *restrict OutputMat, const int ldc,
    int nthreads
)
{
    if (m <= 0 || indptr[0] == indptr[m])
        return;
    real_t *restrict write_ptr;
    nthreads = std::min(nthreads, m);
    std::unique_ptr<real_t[]> temp_arr;
    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic) num_threads(nthreads) \
            shared(OutputMat, DenseMat, indptr, indices, values) \
            private(write_ptr, temp_arr)
    #endif
    for (int row = 0; row < m; row++)
    {
        if (indptr[row] < indptr[row+1])
        {
            write_ptr = temp_arr.get();
            if (!write_ptr) {
                temp_arr = std::unique_ptr<real_t[]>(new real_t[ldc]);
                write_ptr = temp_arr.get();
            }
            memset(write_ptr, 0, ldb*sizeof(real_t));
            for (int ix = indptr[row]; ix < indptr[row+1]; ix++)
                axpy(&n, values + ix, DenseMat + (size_t)indices[ix]*ldb, &one, write_ptr, &one);
            tcopy(&n, write_ptr, &one, OutputMat + row, &ldc);
        }
    }
}

/* x %*% y */
template <class RcppMatrix>
RcppMatrix matmul_dense_csc(RcppMatrix X_colmajor,
                            Rcpp::IntegerVector Y_csc_indptr,
                            Rcpp::IntegerVector Y_csc_indices,
                            Rcpp::NumericVector Y_csc_values,
                            int nthreads)
{
    int nrows_X = X_colmajor.nrow();
    int ncols_Y = Y_csc_indptr.size() - 1;
    RcppMatrix out_colmajor(nrows_X, ncols_Y);
    int nrows = out_colmajor.nrow();
    int ncols = out_colmajor.ncol();

    if (std::is_same<RcppMatrix, Rcpp::NumericMatrix>::value)
        gemm_csr_drm_as_drm<double>(
            ncols, nrows,
            INTEGER(Y_csc_indptr), INTEGER(Y_csc_indices), REAL(Y_csc_values),
            REAL(X_colmajor), nrows,
            REAL(out_colmajor), nrows,
            nthreads
        );
    else
        gemm_csr_drm_as_drm<float>(
            ncols, nrows,
            INTEGER(Y_csc_indptr), INTEGER(Y_csc_indices), REAL(Y_csc_values),
            (float*)INTEGER(X_colmajor), nrows,
            (float*)INTEGER(out_colmajor), nrows,
            nthreads
        );

    return out_colmajor;
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericMatrix matmul_dense_csc_numeric(Rcpp::NumericMatrix X_colmajor,
                                             Rcpp::IntegerVector Y_csc_indptr,
                                             Rcpp::IntegerVector Y_csc_indices,
                                             Rcpp::NumericVector Y_csc_values,
                                             int nthreads)
{
    return matmul_dense_csc<Rcpp::NumericMatrix>(
        X_colmajor,
        Y_csc_indptr,
        Y_csc_indices,
        Y_csc_values,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::IntegerMatrix matmul_dense_csc_float32(Rcpp::IntegerMatrix X_colmajor,
                                             Rcpp::IntegerVector Y_csc_indptr,
                                             Rcpp::IntegerVector Y_csc_indices,
                                             Rcpp::NumericVector Y_csc_values,
                                             int nthreads)
{
    return matmul_dense_csc<Rcpp::IntegerMatrix>(
        X_colmajor,
        Y_csc_indptr,
        Y_csc_indices,
        Y_csc_values,
        nthreads
    );
}

/* x %*% t(y) */
template <class RcppMatrix>
RcppMatrix tcrossprod_dense_csr(RcppMatrix X_colmajor,
                                Rcpp::IntegerVector Y_csr_indptr,
                                Rcpp::IntegerVector Y_csr_indices,
                                Rcpp::NumericVector Y_csr_values,
                                int nthreads, int ncols_Y)
{
    RcppMatrix out_colmajor(X_colmajor.nrow(), Y_csr_indptr.size()-1);

    if (std::is_same<RcppMatrix, Rcpp::NumericMatrix>::value)
        gemm_csr_drm_as_drm<double>(
            out_colmajor.ncol(), out_colmajor.nrow(),
            INTEGER(Y_csr_indptr), INTEGER(Y_csr_indices), REAL(Y_csr_values),
            REAL(X_colmajor), X_colmajor.nrow(),
            REAL(out_colmajor), out_colmajor.nrow(),
            nthreads
        );
    else
        gemm_csr_drm_as_drm<float>(
            out_colmajor.ncol(), out_colmajor.nrow(),
            INTEGER(Y_csr_indptr), INTEGER(Y_csr_indices), REAL(Y_csr_values),
            (float*)INTEGER(X_colmajor), X_colmajor.nrow(),
            (float*)INTEGER(out_colmajor), out_colmajor.nrow(),
            nthreads
        );

    return out_colmajor;
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericMatrix tcrossprod_dense_csr_numeric(Rcpp::NumericMatrix X_colmajor,
                                                 Rcpp::IntegerVector Y_csr_indptr,
                                                 Rcpp::IntegerVector Y_csr_indices,
                                                 Rcpp::NumericVector Y_csr_values,
                                                 int nthreads, int ncols_Y)
{
    return tcrossprod_dense_csr<Rcpp::NumericMatrix>(
        X_colmajor,
        Y_csr_indptr,
        Y_csr_indices,
        Y_csr_values,
        nthreads, ncols_Y
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::IntegerMatrix tcrossprod_dense_csr_float32(Rcpp::IntegerMatrix X_colmajor,
                                                 Rcpp::IntegerVector Y_csr_indptr,
                                                 Rcpp::IntegerVector Y_csr_indices,
                                                 Rcpp::NumericVector Y_csr_values,
                                                 int nthreads, int ncols_Y)
{
    return tcrossprod_dense_csr<Rcpp::IntegerMatrix>(
        X_colmajor,
        Y_csr_indptr,
        Y_csr_indices,
        Y_csr_values,
        nthreads, ncols_Y
    );
}

/* x %*% t(y) */
template <class RcppMatrix>
RcppMatrix tcrossprod_csr_dense(Rcpp::IntegerVector X_csr_indptr,
                                Rcpp::IntegerVector X_csr_indices,
                                Rcpp::NumericVector X_csr_values,
                                RcppMatrix Y_colmajor,
                                int nthreads)
{
    RcppMatrix out_colmajor(X_csr_indptr.size()-1, Y_colmajor.nrow());

    if (std::is_same<RcppMatrix, Rcpp::NumericMatrix>::value)
        gemm_csr_drm_as_dcm<double>(
            out_colmajor.nrow(), out_colmajor.ncol(),
            INTEGER(X_csr_indptr), INTEGER(X_csr_indices), REAL(X_csr_values),
            REAL(Y_colmajor), Y_colmajor.nrow(),
            REAL(out_colmajor), out_colmajor.nrow(),
            nthreads
        );
    else
        gemm_csr_drm_as_dcm<float>(
            out_colmajor.nrow(), out_colmajor.ncol(),
            INTEGER(X_csr_indptr), INTEGER(X_csr_indices), REAL(X_csr_values),
            (float*)INTEGER(Y_colmajor), Y_colmajor.nrow(),
            (float*)INTEGER(out_colmajor), out_colmajor.nrow(),
            nthreads
        );

    return out_colmajor;
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericMatrix tcrossprod_csr_dense_numeric(Rcpp::IntegerVector X_csr_indptr,
                                                 Rcpp::IntegerVector X_csr_indices,
                                                 Rcpp::NumericVector X_csr_values,
                                                 Rcpp::NumericMatrix Y_colmajor,
                                                 int nthreads)
{
    return tcrossprod_csr_dense<Rcpp::NumericMatrix>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        Y_colmajor,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::IntegerMatrix tcrossprod_csr_dense_float32(Rcpp::IntegerVector X_csr_indptr,
                                                 Rcpp::IntegerVector X_csr_indices,
                                                 Rcpp::NumericVector X_csr_values,
                                                 Rcpp::IntegerMatrix Y_colmajor,
                                                 int nthreads)
{
    return tcrossprod_csr_dense<Rcpp::IntegerMatrix>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        Y_colmajor,
        nthreads
    );
}

/* TODO: these matrix-by-vector multiplications could be done more
   efficiently for symmetric matrices and for unit diagonal */

/* x %*% y */
template <class RcppVector, class OutputVector, class OutputDType>
OutputVector matmul_csr_dvec(Rcpp::IntegerVector X_csr_indptr,
                             Rcpp::IntegerVector X_csr_indices,
                             Rcpp::NumericVector X_csr_values,
                             RcppVector y_dense,
                             int nthreads)
{
    // Rcpp::NumericVector out(X_csr_indptr.size()-1);
    OutputVector out_(X_csr_indptr.size()-1);
    OutputDType *restrict out;
    if (std::is_same<OutputDType, float>::value)
        out = (OutputDType*)INTEGER(out_);
    else
        out = (OutputDType*)REAL(out_);
    const int nrows = out_.size();

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic) num_threads(nthreads) \
            shared(X_csr_indptr, X_csr_indices, X_csr_values, y_dense)
    #endif
    for (int row = 0; row < nrows; row++)
    {
        OutputDType val = 0;
        for (int ix = X_csr_indptr[row]; ix < X_csr_indptr[row+1]; ix++)
        {
            if (std::is_same<RcppVector, Rcpp::IntegerVector>::value)
                val += (y_dense[X_csr_indices[ix]] == NA_INTEGER)?
                        NA_REAL : X_csr_values[ix] * y_dense[X_csr_indices[ix]];
            else if (std::is_same<RcppVector, Rcpp::LogicalVector>::value)
                val += (y_dense[X_csr_indices[ix]] == NA_LOGICAL)?
                        NA_REAL : X_csr_values[ix] * (bool)y_dense[X_csr_indices[ix]];
            else
                val += X_csr_values[ix] * y_dense[X_csr_indices[ix]];
        }
        out[row] = val;
    }

    return out_;
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_dvec_numeric(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::NumericVector y_dense,
                                            int nthreads)
{
    return matmul_csr_dvec<Rcpp::NumericVector, Rcpp::NumericVector, double>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_dense,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_dvec_integer(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::IntegerVector y_dense,
                                            int nthreads)
{
    return matmul_csr_dvec<Rcpp::IntegerVector, Rcpp::NumericVector, double>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_dense,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_dvec_logical(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::LogicalVector y_dense,
                                            int nthreads)
{
    return matmul_csr_dvec<Rcpp::LogicalVector, Rcpp::NumericVector, double>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_dense,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::IntegerVector matmul_csr_dvec_float32(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::IntegerVector y_dense,
                                            int nthreads)
{
    return matmul_csr_dvec<float*, Rcpp::IntegerVector, float>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        (float*)INTEGER(y_dense),
        nthreads
    );
}

/* x %*% y */
template <class RcppVector>
Rcpp::NumericVector matmul_csr_svec(Rcpp::IntegerVector X_csr_indptr,
                                    Rcpp::IntegerVector X_csr_indices,
                                    Rcpp::NumericVector X_csr_values,
                                    Rcpp::IntegerVector y_indices_base1,
                                    RcppVector y_values,
                                    int nthreads)
{
    Rcpp::NumericVector out(X_csr_indptr.size()-1);
    if (!y_indices_base1.size())
        return out;
    const int nrows = out.size();

    int *restrict ptr_X_indices = INTEGER(X_csr_indices);
    int *restrict ptr_y_indices = INTEGER(y_indices_base1);
    int *restrict end_y = ptr_y_indices + y_indices_base1.size();
    int *ptr1, *ptr2, *end1;

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic) num_threads(nthreads) \
            shared(X_csr_indptr, X_csr_indices, X_csr_values, \
                   ptr_X_indices, ptr_y_indices, end_y, out) \
            private(ptr1, ptr2, end1)
    #endif
    for (int row = 0; row < nrows; row++)
    {
        ptr1 = ptr_X_indices + X_csr_indptr[row];
        end1 = ptr_X_indices + X_csr_indptr[row+1];
        ptr2 = ptr_y_indices;
        
        while (true)
        {
            if (ptr1 >= end1 || ptr2 >= end_y) {
                goto next_row;
            }

            else if (*ptr1 == (*ptr2)-1) {
                if (std::is_same<RcppVector, Rcpp::IntegerVector>::value)
                    out[row] += (y_values[ptr2 - ptr_y_indices] == NA_INTEGER)?
                                 NA_REAL : X_csr_values[ptr1 - ptr_X_indices] * y_values[ptr2 - ptr_y_indices];
                else if (std::is_same<RcppVector, Rcpp::LogicalVector>::value)
                    out[row] += (y_values[ptr2 - ptr_y_indices] == NA_LOGICAL)?
                                 NA_REAL : X_csr_values[ptr1 - ptr_X_indices] * (bool)y_values[ptr2 - ptr_y_indices];
                else if (std::is_same<RcppVector, char*>::value)
                    out[row] += X_csr_values[ptr1 - ptr_X_indices];
                else
                    out[row] += X_csr_values[ptr1 - ptr_X_indices] * y_values[ptr2 - ptr_y_indices];
                ptr1++;
                ptr2++;
            }

            else if (*ptr2-1 > *ptr1) {
                ptr1 = std::lower_bound(ptr1, end1, *ptr2-1);
            }

            else {
                ptr2 = std::lower_bound(ptr2, end_y, *ptr1+1);
            }
        }

        next_row:
        {}
    }

    return out;
}


// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_svec_numeric(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::IntegerVector y_indices_base1,
                                            Rcpp::NumericVector y_values,
                                            int nthreads)
{
    return matmul_csr_svec<Rcpp::NumericVector>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        y_values,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_svec_integer(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::IntegerVector y_indices_base1,
                                            Rcpp::IntegerVector y_values,
                                            int nthreads)
{
    return matmul_csr_svec<Rcpp::IntegerVector>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        y_values,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_svec_logical(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::IntegerVector y_indices_base1,
                                            Rcpp::LogicalVector y_values,
                                            int nthreads)
{
    return matmul_csr_svec<Rcpp::LogicalVector>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        y_values,
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_svec_binary(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::IntegerVector y_indices_base1,
                                            int nthreads)
{
    return matmul_csr_svec<char*>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        (char*)INTEGER(y_indices_base1), /* <- placeholder */
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::NumericVector matmul_csr_svec_float32(Rcpp::IntegerVector X_csr_indptr,
                                            Rcpp::IntegerVector X_csr_indices,
                                            Rcpp::NumericVector X_csr_values,
                                            Rcpp::IntegerVector y_indices_base1,
                                            Rcpp::IntegerVector y_values,
                                            int nthreads)
{
    return matmul_csr_svec<float*>(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        (float*)INTEGER(y_values),
        nthreads
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::IntegerMatrix matmul_rowvec_by_csc
(
    Rcpp::IntegerVector rowvec_,
    Rcpp::IntegerVector indptr,
    Rcpp::IntegerVector indices,
    Rcpp::NumericVector values
)
{
    float *restrict rowvec = (float*)INTEGER(rowvec_);
    size_t ncols_Y = indptr.size()-1;
    Rcpp::IntegerMatrix out_(1, ncols_Y);
    float *restrict out = (float*)INTEGER(out_);
    for (size_t col = 0; col < ncols_Y; col++)
    {
        for (int ix = indptr[col]; ix < indptr[col+1]; ix++)
            out[col] += values[ix] * rowvec[indices[ix]];
    }

    return out_;
}

// [[Rcpp::export(rng = false)]]
Rcpp::IntegerMatrix matmul_rowvec_by_cscbin
(
    Rcpp::IntegerVector rowvec_,
    Rcpp::IntegerVector indptr,
    Rcpp::IntegerVector indices
)
{
    float *restrict rowvec = (float*)INTEGER(rowvec_);
    size_t ncols_Y = indptr.size()-1;
    Rcpp::IntegerMatrix out_(1, ncols_Y);
    float *restrict out = (float*)INTEGER(out_);
    for (size_t col = 0; col < ncols_Y; col++)
    {
        for (int ix = indptr[col]; ix < indptr[col+1]; ix++)
            out[col] += rowvec[indices[ix]];
    }

    return out_;
}

template <class real_t, class RcppVector>
Rcpp::List matmul_colvec_by_scolvecascsr_template
(
    RcppVector colvec_,
    Rcpp::IntegerVector indptr,
    Rcpp::IntegerVector indices,
    Rcpp::NumericVector values_
)
{
    const int dim = colvec_.size();
    const size_t dim2 = indptr.size()-1;
    const size_t nnz = indices.size();
    const size_t nnz_out = nnz * (size_t)dim;
    Rcpp::IntegerVector out_csr_indptr(dim2+1);
    Rcpp::IntegerVector out_csr_indices_(nnz_out);
    Rcpp::NumericVector out_csr_values_(nnz_out);
    std::unique_ptr<real_t[]> out_csr_values__;
    if (std::is_same<real_t, float>::value)
        out_csr_values__ = std::unique_ptr<real_t[]>(new real_t[nnz_out]());

    real_t *restrict out_csr_values = nullptr;
    real_t *restrict colvec = nullptr;

    if (std::is_same<real_t, double>::value) {
        out_csr_values = (real_t*)REAL(out_csr_values_);
        colvec = (real_t*)REAL(colvec_);
    }
    else {
        out_csr_values = (real_t*)out_csr_values__.get();
        colvec = (real_t*)INTEGER(colvec_);
    }

    int *restrict out_csr_indices = INTEGER(out_csr_indices_);
    const double *restrict values = REAL(values_);
    size_t ncurr = 0;

    for (size_t ix = 0; ix < dim2; ix++)
    {
        if (indptr[ix] < indptr[ix+1])
        {
            out_csr_indptr[ix+1] = dim;
            axpy(&dim, values[indptr[ix]], colvec, &one, out_csr_values + ncurr, &one);
            std::iota(out_csr_indices + ncurr, out_csr_indices + ncurr + dim, 0);
            ncurr += dim;
        }
    }

    for (size_t ix = 0; ix < dim2; ix++)
        out_csr_indptr[ix+1] += out_csr_indptr[ix];

    if (std::is_same<real_t, float>::value)
        for (size_t ix = 0; ix < nnz_out; ix++)
            out_csr_values_[ix] = (double)out_csr_values[ix];

    return Rcpp::List::create(
        Rcpp::_["indptr"] = out_csr_indptr,
        Rcpp::_["indices"] = out_csr_indices_,
        Rcpp::_["values"] = out_csr_values_
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::List matmul_colvec_by_scolvecascsr_f32
(
    Rcpp::IntegerVector colvec_,
    Rcpp::IntegerVector indptr,
    Rcpp::IntegerVector indices,
    Rcpp::NumericVector values_
)
{
    return matmul_colvec_by_scolvecascsr_template<float, Rcpp::IntegerVector>(
        colvec_,
        indptr,
        indices,
        values_
    );
}

/* TODO: some the functions above should output sparse matrices */

// [[Rcpp::export(rng = false)]]
Rcpp::List matmul_colvec_by_scolvecascsr
(
    Rcpp::NumericVector colvec_,
    Rcpp::IntegerVector indptr,
    Rcpp::IntegerVector indices,
    Rcpp::NumericVector values_
)
{
    return matmul_colvec_by_scolvecascsr_template<double, Rcpp::NumericVector>(
        colvec_,
        indptr,
        indices,
        values_
    );
}

template <class InputDType>
Rcpp::List matmul_spcolvec_by_scolvecascsr
(
    Rcpp::IntegerVector X_csr_indptr,
    Rcpp::IntegerVector X_csr_indices,
    Rcpp::NumericVector X_csr_values,
    Rcpp::IntegerVector y_indices_base1,
    InputDType *restrict y_values,
    int y_length
)
{
    Rcpp::IntegerVector out_csc_indptr(y_length+1);
    std::vector<int> out_csc_indices;
    std::vector<double> out_csc_values;

    const size_t dim2 = X_csr_indptr.size()-1;
    const size_t nnz_y = y_indices_base1.size();
    InputDType yval;
    size_t ncurr;
    int col;

    for (size_t ix = 0; ix < nnz_y; ix++)
    {
        col = y_indices_base1[ix] - 1;
        if (!std::is_same<InputDType, char>::value)
            yval = y_values[col];
        ncurr = 0;

        for (size_t row = 0; row < dim2; row++)
        {
            if (X_csr_indptr[row] < X_csr_indptr[row+1])
            {
                if (std::is_same<InputDType, int>::value) {
                    out_csc_values.push_back(
                        (yval == NA_INTEGER)?
                        NA_REAL : (yval * X_csr_values[X_csr_indptr[row]])
                    );
                }

                else if (std::is_same<InputDType, char>::value) {
                    out_csc_values.push_back(X_csr_values[X_csr_indptr[row]]);
                }

                else {
                    out_csc_values.push_back(yval * X_csr_values[X_csr_indptr[row]]);
                }
                
                out_csc_indices.push_back(row);
                ncurr++;
            }
        }

        out_csc_indptr[col+1] = ncurr;
    }

    for (int ix = 0; ix < y_length; ix++)
        out_csc_indptr[ix+1] += out_csc_indptr[ix];

    VectorConstructorArgs args;
    args.as_integer = true; args.from_cpp_vec = true; args.int_vec_from = &out_csc_indices;
    Rcpp::IntegerVector out_csc_indices_ = Rcpp::unwindProtect(SafeRcppVector, (void*)&args);
    out_csc_indices.clear();
    out_csc_indices.shrink_to_fit();

    args.as_integer = false; args.from_cpp_vec = true; args.num_vec_from = &out_csc_values;
    Rcpp::NumericVector out_csc_values_ = Rcpp::unwindProtect(SafeRcppVector, (void*)&args);

    return Rcpp::List::create(
        Rcpp::_["indptr"] = out_csc_indptr,
        Rcpp::_["indices"] = out_csc_indices_,
        Rcpp::_["values"] = out_csc_values_
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::List matmul_spcolvec_by_scolvecascsr_numeric
(
    Rcpp::IntegerVector X_csr_indptr,
    Rcpp::IntegerVector X_csr_indices,
    Rcpp::NumericVector X_csr_values,
    Rcpp::IntegerVector y_indices_base1,
    Rcpp::NumericVector y_values,
    int y_length
)
{
    return matmul_spcolvec_by_scolvecascsr(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        REAL(y_values),
        y_length
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::List matmul_spcolvec_by_scolvecascsr_integer
(
    Rcpp::IntegerVector X_csr_indptr,
    Rcpp::IntegerVector X_csr_indices,
    Rcpp::NumericVector X_csr_values,
    Rcpp::IntegerVector y_indices_base1,
    Rcpp::IntegerVector y_values,
    int y_length
)
{
    return matmul_spcolvec_by_scolvecascsr(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        INTEGER(y_values),
        y_length
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::List matmul_spcolvec_by_scolvecascsr_logical
(
    Rcpp::IntegerVector X_csr_indptr,
    Rcpp::IntegerVector X_csr_indices,
    Rcpp::NumericVector X_csr_values,
    Rcpp::IntegerVector y_indices_base1,
    Rcpp::LogicalVector y_values,
    int y_length
)
{
    return matmul_spcolvec_by_scolvecascsr(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        LOGICAL(y_values),
        y_length
    );
}

// [[Rcpp::export(rng = false)]]
Rcpp::List matmul_spcolvec_by_scolvecascsr_binary
(
    Rcpp::IntegerVector X_csr_indptr,
    Rcpp::IntegerVector X_csr_indices,
    Rcpp::NumericVector X_csr_values,
    Rcpp::IntegerVector y_indices_base1,
    int y_length
)
{
    return matmul_spcolvec_by_scolvecascsr(
        X_csr_indptr,
        X_csr_indices,
        X_csr_values,
        y_indices_base1,
        (char*)nullptr,
        y_length
    );
}

#ifdef __clang__
#   pragma clang diagnostic pop
#endif
