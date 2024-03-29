#' @importClassesFrom float float32
#' @importFrom float dbl
#' @importFrom RhpcBLASctl blas_get_num_procs blas_set_num_threads

### Peculiarities about R's matrix-by-vector multiplications (as of v4.0.4)
###
### matmul(Mat, vec)
###     -> If 'Mat' has more than one column, 'vec' is a column vector [n,1]
###     -> If 'Mat' has only one column, 'vec' is a row vector [1,n]
### matmul(vec, Mat)
###     -> If 'Mat' has more than one row, 'vec' is a row vector [1,n]
###     -> If 'Mat' has only one row, 'vec' is a column vector [n,1]
### matmul(vec, vec) -> LHS is a row vector [1,n], RHS is a column vector [n,1]
###
### crossprod(Mat, vec)
###     -> If 'Mat' has more than one row, 'vec' is a column vector [n,1]
###     -> If 'Mat' has one column, 'vec' is a row vector [1,n]
### crossprod(vec, Mat) -> 'vec' is a column vector [n,1]
### crossprod(vec, vec) -> 'vec' is a column vector [n,1]
###
### tcrossprod(Mat, vec)
###    -> If 'Mat' has more than one row and more than one column, will fail
###    -> If 'Mat' has only one row, 'vec' is a row vector [1,n]
###    -> If 'Mat' has only one column, 'vec' is a column vector [n,1]
### tcrossprod(vec, Mat)
###    -> If 'Mat' has more than one column, 'vec' is a row vector [1,n]
###    -> If 'Mat' has only one column, 'vec' is a column vector [n,1]
### tcrossprod(vec, vec): 'vec' is a column vector [n,1]


### TODO: try to make the multiplications preserve the names the same way as base R


#' @title Multithreaded Sparse-Dense Matrix and Vector Multiplications
#' @description Multithreaded <matrix, matrix> multiplications
#' (`\%*\%`, `crossprod`, and `tcrossprod`)
#' and <matrix, vector> multiplications (`\%*\%`),
#' for <sparse, dense> matrix combinations and <sparse, vector> combinations
#' (See signatures for supported combinations).
#'
#' Objects from the `float` package are also supported for some combinations.
#' @details Will try to use the maximum available number of threads for the computations
#' when appropriate. The number of threads can be controlled through the package options
#' (e.g. `options("MatrixExtra.nthreads" = 1)` - see \link{MatrixExtra-options}) and will
#' be set to 1 after running \link{restore_old_matrix_behavior}.
#' 
#' Be aware that sparse-dense matrix multiplications might suffer from reduced
#' numerical precision, especially when using objects of type `float32`
#' (from the `float` package).
#'
#' Internally, these functions use BLAS level-1 routines, so their speed might depend on
#' the BLAS backend being used (e.g. MKL, OpenBLAS) - that means: they might be quite slow
#' on a default install of R for Windows (see
#' \href{https://github.com/david-cortes/R-openblas-in-windows}{this link} for
#' a tutorial about getting OpenBLAS in R for Windows).
#' 
#' Doing computations in float32 precision depends on the package
#' \href{https://cran.r-project.org/package=float}{float}, and as such comes
#' with some caveats:\itemize{
#' \item On Windows, if installing `float` from CRAN, it will use very unoptimized
#' routines which will likely result in a slowdown compared to using regular
#' double (numeric) type. Getting it to use an optimized BLAS library is not as
#' simple as substituting the Rblas DLL - see the
#' \href{https://github.com/wrathematics/float}{package's README} for details.
#' \item On macOS, it will use static linking for `float`, thus if changing the BLAS
#' library used by R, it will not change the float32 functions, and getting good
#' performance out of it might require compiling it from source with `-march=native`
#' flag.
#' }
#'
#' When multiplying a sparse matrix by a sparse vector, their indices
#' will be sorted in-place (see \link{sort_sparse_indices}).
#'
#' In order to match exactly with base R's behaviors, when passing vectors to these
#' operators, will assume their shape as follows:\itemize{
#' \item MatMult(Matrix, vector): column vector if the matrix has more than one column
#' or is empty, row vector if the matrix has only one column.
#' \item MatMult(vector, Matrix): row vector if the matrix has more than one row,
#' column vector if the matrix has only one row.
#' \item MatMul(vector, vector): LHS is a row vector, RHS is a column vector.
#' \item crossprod(Matrix, vector): column vector if the matrix has more than one row,
#' row vector if the matrix has only one row.
#' \item crossprod(vector, Matrix): column vector.
#' \item crossprod(vector, vector): column vector.
#' \item tcrossprod(Matrix, vector): row vector if the matrix has only one row,
#' column vector if the matrix has only one column, and will throw an error otherwise.
#' \item tcrossprod(vector, Matrix): row vector if the matrix has more than one column,
#' column vector if the matrix has only one column.
#' \item tcrossprod(vector, vector): column vector.
#' }
#'
#' In general, the output returned by these functions will be a dense matrix from base R,
#' or a dense matrix from `float` when one of the inputs is also from the `float` package,
#' with the following exceptions:\itemize{
#' \item MatMult(RsparseMatrix[n,1], vector) -> `dgRMatrix`.
#' \item MatMult(RsparseMatrix[n,1], sparseVector) -> `dgCMatrix`.
#' \item MatMult(float32[n], CsparseMatrix[1,m]) -> `dgCMatrix`.
#' \item tcrossprod(float32[n], RsparseMatrix[m,1]) -> `dgCMatrix`.
#' }
#' @param x,y dense (\code{matrix} / \code{float32})
#' and sparse (\code{RsparseMatrix} / \code{CsparseMatrix}) matrices or vectors
#' (\code{sparseVector}, \code{numeric}, \code{integer}, \code{logical}).
#' @return A dense \code{matrix} object in most cases, with some exceptions which might
#' come in sparse format (see the 'Details' section).
#' @name matmult
#' @rdname matmult
#' @examples
#' library(Matrix)
#' library(MatrixExtra)
#' ### To use all available threads (default)
#' options("MatrixExtra.nthreads" = parallel::detectCores())
#' ### Example will run with only 1 thread (CRAN policy)
#' options("MatrixExtra.nthreads" = 1)
#'
#' ## Generate random matrices
#' set.seed(1)
#' A <- rsparsematrix(5,4,.5)
#' B <- rsparsematrix(4,3,.5)
#'
#' ## Now multiply in some supported combinations
#' as.matrix(A) %*% as.csc.matrix(B)
#' as.csr.matrix(A) %*% as.matrix(B)
#' crossprod(as.matrix(B), as.csc.matrix(B))
#' tcrossprod(as.csr.matrix(A), as.matrix(A))
#'
#' ### Restore the number of threads
#' options("MatrixExtra.nthreads" = parallel::detectCores())
NULL

check_dimensions_match <- function(x, y, matmult=FALSE, crossprod=FALSE, tcrossprod=FALSE) {
    if (matmult) {
        inner_x <- ncol(x)
        inner_y <- nrow(y)
    } else if (crossprod) {
        inner_x <- nrow(x)
        inner_y <- nrow(y)
    } else if (tcrossprod) {
        inner_x <- ncol(x)
        inner_y <- ncol(y)
    } else {
        throw_internal_error()
    }

    if (inner_x != inner_y)
        stop("Matrix dimensions do not match.")
}

set_dimnames <- function(res, x, y, matmult=FALSE, crossprod=FALSE, tcrossprod=FALSE) {
    if (matmult) {
        rnames <- rownames(x)
        cnames <- colnames(y)
    } else if (crossprod) {
        rnames <- colnames(x)
        cnames <- colnames(y)
    } else if (tcrossprod) {
        rnames <- rownames(x)
        cnames <- rownames(y)
    } else {
        throw_internal_error()
    }

    if (!is.null(rnames))
        rownames(res) <- rnames
    if (!is.null(cnames))
        colnames(res) <- cnames
    return(res)
}

#### Matrices ----

gemm_dense_csc <- function(x, y) {
    check_dimensions_match(x, y, matmult=TRUE)

    # restore on exit
    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))

    # set num threads to 1 in order to avoid thread contention between BLAS and openmp threads
    if (nthreads > 1L) RhpcBLASctl::blas_set_num_threads(1L)

    if (typeof(x) != "double") mode(x) <- "double"
    y <- as.csc.matrix(y)
    check_valid_matrix(y)

    res <- matmul_dense_csc_numeric(
        x,
        y@p,
        y@i,
        y@x,
        nthreads
    )

    res <- set_dimnames(res, x, y, matmult=TRUE)
    return(res)
}

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="matrix", y="CsparseMatrix"), gemm_dense_csc)

gemm_f32_csc <- function(x, y) {

    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))
    if (nthreads > 1) RhpcBLASctl::blas_set_num_threads(1L)

    if (is.vector(x@Data)) {

        if (inherits(y, "symmetricMatrix") ||
            (.hasSlot(y, "diag") && y@diag != "N") ||
            (.hasSlot(y, "x") && !inherits(y, "dsparseMatrix"))
        ) {
            y <- as.csc.matrix(y)
        }
        check_valid_matrix(y)

        ### To match base R, if 'y' has more than one row, 'x' is [1,n], otherwise [n,1]
        if (nrow(y) == 1L) {

            if (!inherits(y, "dsparseMatrix"))
                y <- as.csr.matrix(y)
            check_valid_matrix(y)

            res <- matmul_colvec_by_scolvecascsr_f32(
                x@Data,
                y@p,
                y@i,
                y@x
            )
            out <- new("dgCMatrix")
            out@p <- res$indptr
            out@i <- res$indices
            out@x <- res$values
            out@Dim <- as.integer(c(length(x@Data), ncol(y)))
            if (!is.null(y@Dimnames[[2L]]))
                out@Dimnames[[2L]] <- y@Dimnames[[2L]]
            if ("names" %in% names(attributes(x@Data)))
                colnames(out) <- names(x@Data)
            return(out)
        } else {
            if (nrow(y) != length(x@Data))
                stop("(row) vector-Matrix multiplication dimensions do not match.")

            if (.hasSlot(y, "x")) {
                res <- matmul_rowvec_by_csc(
                    x@Data,
                    y@p,
                    y@i,
                    y@x
                )
            } else {
                res <- matmul_rowvec_by_cscbin(
                    x@Data,
                    y@p,
                    y@i
                )
            }
            return(new("float32", Data=res))
        }
    }

    y <- as.csc.matrix(y)
    check_valid_matrix(y)

    res <- matmul_dense_csc_float32(
        x@Data,
        y@p,
        y@i,
        y@x,
        nthreads
    )

    res <- set_dimnames(res, x, y, matmult=TRUE)
    return(new("float32", Data=res))
}

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="float32", y="CsparseMatrix"), gemm_f32_csc)

tcrossprod_dense_csr <- function(x, y) {
    check_dimensions_match(x, y, tcrossprod=TRUE)
    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))
    if (nthreads > 1) RhpcBLASctl::blas_set_num_threads(1L)

    if (typeof(x) != "double") mode(x) <- "double"
    y <- as.csr.matrix(y)
    check_valid_matrix(y)

    res <- tcrossprod_dense_csr_numeric(
        x,
        y@p,
        y@j,
        y@x,
        nthreads, ncol(y)
    )
    res <- set_dimnames(res, x, y, tcrossprod=TRUE)
    return(res)
}

#' @rdname matmult
#' @export
setMethod("tcrossprod", signature(x="matrix", y="RsparseMatrix"), tcrossprod_dense_csr)

tcrossprod_f32_csr <- function(x, y) {

    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))
    if (nthreads > 1) RhpcBLASctl::blas_set_num_threads(1L)

    if (is.vector(x@Data)) {

        if (inherits(y, "symmetricMatrix") ||
            (.hasSlot(y, "diag") && y@diag != "N") ||
            (.hasSlot(y, "x") && !inherits(y, "dsparseMatrix"))
        ) {
            y <- as.csr.matrix(y)
        }
        check_valid_matrix(y)

        ### To match with base R, if 'y' has only one column, x is [n,1], otherwise [1,n]
        if (ncol(y) == 1L) {

            if (!inherits(y, "dsparseMatrix"))
                y <- as.csr.matrix(y)
            check_valid_matrix(y)

            res <- matmul_colvec_by_scolvecascsr_f32(
                x@Data,
                y@p,
                y@j,
                y@x
            )
            out <- new("dgCMatrix")
            out@p <- res$indptr
            out@i <- res$indices
            out@x <- res$values
            out@Dim <- as.integer(c(length(x@Data), nrow(y)))
            if (!is.null(y@Dimnames[[2L]]))
                out@Dimnames[[2L]] <- y@Dimnames[[2L]]
            if ("names" %in% names(attributes(x@Data)))
                colnames(out) <- names(x@Data)
            return(out)

        } else {
            if (.hasSlot(y, "x")) {
                res <- matmul_rowvec_by_csc(
                    x@Data,
                    y@p,
                    y@j,
                    y@x
                )
            } else {
                res <- matmul_rowvec_by_cscbin(
                    x@Data,
                    y@p,
                    y@j
                )
            }
            return(new("float32", Data=res))
        }
    }

    y <- as.csr.matrix(y)
    check_valid_matrix(y)

    res <- tcrossprod_dense_csr_float32(
        x@Data,
        y@p,
        y@j,
        y@x,
        nthreads, ncol(y)
    )
    res <- set_dimnames(res, x, y, tcrossprod=TRUE)
    return(new("float32", Data=res))
}

#' @rdname matmult
#' @export
setMethod("tcrossprod", signature(x="float32", y="RsparseMatrix"), tcrossprod_f32_csr)

crossprod_dense_csc <- function(x, y) {
    return(gemm_dense_csc(t(x), y))
}

#' @rdname matmult
#' @export
setMethod("crossprod", signature(x="matrix", y="CsparseMatrix"), crossprod_dense_csc)

crossprod_f32_csc <- function(x, y) {

    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))
    if (nthreads > 1) RhpcBLASctl::blas_set_num_threads(1L)

    if (is.vector(x@Data)) {
        if (length(x@Data) != nrow(y))
            stop("(column) vector-Matrix crossprod dimensions do not match.")
        if (inherits(y, "symmetricMatrix") ||
            (.hasSlot(y, "diag") && y@diag != "N") ||
            (.hasSlot(y, "x") && !inherits(y, "dsparseMatrix"))
        ) {
            y <- as.csc.matrix(y)
        }
        check_valid_matrix(y)
        if (.hasSlot(y, "x")) {
            res <- matmul_rowvec_by_csc(
                x@Data,
                y@p,
                y@i,
                y@x
            )
        } else {
            res <- matmul_rowvec_by_cscbin(
                x@Data,
                y@p,
                y@i
            )
        }
        return(new("float32", Data=res))
    }

    return(t(x) %*% y)
}

#' @rdname matmult
#' @export
setMethod("crossprod", signature(x="float32", y="CsparseMatrix"), crossprod_f32_csc)

tcrossprod_csr_dense <- function(x, y) {
    check_dimensions_match(x, y, tcrossprod=TRUE)
    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))
    if (nthreads > 1) RhpcBLASctl::blas_set_num_threads(1L)

    if (typeof(y) != "double") mode(y) <- "double"
    x <- as.csr.matrix(x)
    check_valid_matrix(x)

    res <- tcrossprod_csr_dense_numeric(
        x@p,
        x@j,
        x@x,
        y,
        nthreads
    )

    res <- set_dimnames(res, x, y, tcrossprod=TRUE)
    return(res)
}

#' @rdname matmult
#' @export
setMethod("tcrossprod", signature(x="RsparseMatrix", y="matrix"), tcrossprod_csr_dense)

gemm_csr_dense <- function(x, y) {
    return(tcrossprod_csr_dense(x, t(y)))
}

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="RsparseMatrix", y="matrix"), gemm_csr_dense)

gemm_csr_f32 <- function(x, y) {

    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))
    if (nthreads > 1) RhpcBLASctl::blas_set_num_threads(1L)

    if (is.vector(y@Data)) {
        if (ncol(x) == 1L) {
            if (!inherits(x, "dsparseMatrix") ||
                inherits(x, "symmetricMatrix") ||
                (.hasSlot(x, "diag") && x@diag != "N")) {
                x <- as.csr.matrix(x)
            }
            check_valid_matrix(x)
            res <- matmul_colvec_by_scolvecascsr_f32(
                y@Data,
                x@p,
                x@j,
                x@x
            )
            out <- new("dgRMatrix")
            out@p <- res$indptr
            out@j <- res$indices
            out@x <- res$values
            out@Dim <- as.integer(c(nrow(x), length(y)))
            if (!is.null(rownames(x)))
                out@Dimnames[[1L]] <- rownames(x)
            if ("names" %in% names(attributes(y)))
                colnames(out) <- names(y)
            return(out)
        } else {
            return(gemv_csr_vec(x, y))
        }
    }

    return(tcrossprod(x, t(y)))
}

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="RsparseMatrix", y="float32"), gemm_csr_f32)

tcrossprod_csr_f32 <- function(x, y) {
    check_dimensions_match(x, y, tcrossprod=TRUE)
    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    nthreads <- max(as.integer(nthreads), 1L)
    on.exit(RhpcBLASctl::blas_set_num_threads(RhpcBLASctl::blas_get_num_procs()))
    if (nthreads > 1) RhpcBLASctl::blas_set_num_threads(1L)

    x <- as.csr.matrix(x)
    check_valid_matrix(x)

    res <- tcrossprod_csr_dense_float32(
        x@p,
        x@j,
        x@x,
        y@Data,
        nthreads
    )

    res <- set_dimnames(res, x, y, tcrossprod=TRUE)
    return(new("float32", Data=res))
}

#' @rdname matmult
#' @export
setMethod("tcrossprod", signature(x="RsparseMatrix", y="float32"), tcrossprod_csr_f32)

#### Vectors ----

### TODO: these matrix-by-vector multiplications could be done more
### efficiently for symmetric matrices and for unit diagonal

gemv_csr_vec <- function(x, y) {
    if (ncol(x) != length(y))
        stop("Matrix-vector dimensions do not match.")
    nthreads <- getOption("MatrixExtra.nthreads", default=parallel::detectCores())
    check_valid_matrix(x)

    if (!inherits(y, "sparseVector")) {

        ### dense vectors from base R
        x <- as.csr.matrix(x)

        if (typeof(y) == "double") {
            res <- matmul_csr_dvec_numeric(
                x@p,
                x@j,
                x@x,
                y,
                nthreads
            )
        } else if (typeof(y) == "integer") {
            res <- matmul_csr_dvec_integer(
                x@p,
                x@j,
                x@x,
                y,
                nthreads
            )
        } else if (typeof(y) == "logical") {
            res <- matmul_csr_dvec_logical(
                x@p,
                x@j,
                x@x,
                y,
                nthreads
            )
        } else if (inherits(y, "float32")) {
            res <- matmul_csr_dvec_float32(
                x@p,
                x@j,
                x@x,
                y@Data,
                nthreads
            )
        } else {
            y <- as.numeric(y)
            if (typeof(y) != "double")
                mode(y) <- "double"
            return(x %*% y)
        }

    } else {

        ### sparse vectors from matrix
        inplace_sort <- getOption("MatrixExtra.inplace_sort", default=FALSE)
        if (inplace_sort)
            x <- deepcopy_before_sort(x)
        x <- as.csr.matrix(x)
        x <- sort_sparse_indices(x, copy=!inplace_sort)
        y <- sort_sparse_indices(y, copy=!inplace_sort)

        if (inherits(y, "dsparseVector")) {
            res <- matmul_csr_svec_numeric(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                y@x,
                nthreads
            )
        } else if (inherits(y, "isparseVector")) {
            res <- matmul_csr_svec_integer(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                y@x,
                nthreads
            )
        } else if (inherits(y, "lsparseVector")) {
            res <- matmul_csr_svec_logical(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                y@x,
                nthreads
            )
        } else if (inherits(y, "nsparseVector")) {
            res <- matmul_csr_svec_binary(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                nthreads
            )
        } else {
            y <- as.numeric(y)
            if (typeof(y) != "double")
                mode(y) <- "double"
            return(x %*% y)
        }
    }

    if (!is.null(rownames(x)))
        names(res) <- rownames(x)

    if (!inherits(y, "float32")) {
        return(matrix(res, ncol=1))
    } else {
        res <- new("float32", Data=matrix(res, ncol=1))
        return(res)
    }
}

outerprod_csrsinglecol_by_dvec <- function(x, y) {
    if (ncol(x) != 1L)
        throw_internal_error()

    if (!inherits(x, "dsparseMatrix") ||
        inherits(x, "symmetricMatrix") ||
        (.hasSlot(x, "diag") && x@diag != "N")
    ) {
        x <- as.csr.matrix(x)
    }
    check_valid_matrix(x)

    if (inherits(y, "sparseVector")) {
        inplace_sort <- getOption("MatrixExtra.inplace_sort", default=FALSE)
        y <- sort_sparse_indices(y, copy=!inplace_sort)

        if (inherits(y, "dsparseVector")) {
            res <- matmul_spcolvec_by_scolvecascsr_numeric(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                y@x,
                as.integer(y@length)
            )
        } else if (inherits(y, "isparseVector")) {
            res <- matmul_spcolvec_by_scolvecascsr_integer(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                y@x,
                as.integer(y@length)
            )
        } else if (inherits(y, "lsparseVector")) {
            res <- matmul_spcolvec_by_scolvecascsr_logical(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                y@x,
                as.integer(y@length)
            )
        } else if (inherits(y, "nsparseVector")) {
            res <- matmul_spcolvec_by_scolvecascsr_binary(
                x@p,
                x@j,
                x@x,
                as.integer(y@i),
                as.integer(y@length)
            )
        } else {
            y <- as(y, "dsparseVector")
            return(outerprod_csrsinglecol_by_dvec(x, y))
        }
        out <- new("dgCMatrix")
        out@p <- res$indptr
        out@i <- res$indices
        out@x <- res$values
        out@Dim <- as.integer(c(nrow(x), y@length))
        out@Dimnames <- list(rownames(x), NULL)
        return(out)
    } else {

        if (typeof(y) != "double")
            mode(y) <- "double"

        res <- matmul_colvec_by_scolvecascsr(
            y,
            x@p,
            x@j,
            x@x
        )
        out <- new("dgRMatrix")
        out@p <- res$indptr
        out@j <- res$indices
        out@x <- res$values
        out@Dim <- as.integer(c(nrow(x), length(y)))
        if (!is.null(rownames(x)))
            rownames(out) <- rownames(x)
        if ("names" %in% names(attributes(y)))
            colnames(out) <- names(y)
        return(out)
    }

}

matmul_csr_vec <- function(x, y) {
    if (ncol(x) == 1L)
        return(outerprod_csrsinglecol_by_dvec(x, y))
    else
        return(gemv_csr_vec(x, y))
}

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="RsparseMatrix", y="numeric"), matmul_csr_vec)

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="RsparseMatrix", y="logical"), matmul_csr_vec)

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="RsparseMatrix", y="integer"), matmul_csr_vec)

#' @rdname matmult
#' @export
setMethod("%*%", signature(x="RsparseMatrix", y="sparseVector"), matmul_csr_vec)

### TODO: is CSC %*% vector in 'Matrix' implemented efficiently?
