/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include "cachelab.h"
#include <stdio.h>

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded.
 */
char transpose_submit_desc[] = "Transpose submission";

void transpose_submit(int M, int N, int A[N][M], int B[M][N]) {
  int i, j, ii, jj, t0, t1, t2, t3, t4, t5, t6, t7;

  if (M == 32) {
    for (j = 0; j < M; j += 8) {
      for (i = 0; i < N; i += 8) {
        for (jj = 0; jj + j < M && jj < 8; ++jj) {
//8X8 block, easy pass
          t0 = A[0 + i][jj + j];
          t1 = A[1 + i][jj + j];
          t2 = A[2 + i][jj + j];
          t3 = A[3 + i][jj + j];
          t4 = A[4 + i][jj + j];
          t5 = A[5 + i][jj + j];
          t6 = A[6 + i][jj + j];
          t7 = A[7 + i][jj + j];
          B[jj + j][0 + i] = t0;
          B[jj + j][1 + i] = t1;
          B[jj + j][2 + i] = t2;
          B[jj + j][3 + i] = t3;
          B[jj + j][4 + i] = t4;
          B[jj + j][5 + i] = t5;
          B[jj + j][6 + i] = t6;
          B[jj + j][7 + i] = t7;
        }
      }
    }
  } else if (M == 64 && N == 64) {
    for (j = 0; j < N; j += 8) {
      i = j;
      for (ii = 0; ii < 4; ++ii) {
        //+---+---+---+---+
        //|A1 |A2 |A5 |A6 |
        //+---+---+---+---+
        //|A3 |A4 |A7 |A8 |
        //+---+---+---+---+

        //+---+---+---+---+
        //|B1 |B2 |A1T|A2T|
        //+---+---+---+---+
        //|B3 |B4 |B7 |B8 |
        //+---+---+---+---+

        //+---+---+---+---+
        //|B1 |B2 |A1T|A3T|
        //+---+---+---+---+
        //|B3 |B4 |A2T|A4T|
        //+---+---+---+---+

        //+---+---+---+---+
        //|A1T|A3T|A1T|A3T|
        //+---+---+---+---+
        //|A2T|A4T|A2T|A4T|
        //+---+---+---+---+


        t0 = A[i + ii][j];
        t1 = A[i + ii][j + 1];
        t2 = A[i + ii][j + 2];
        t3 = A[i + ii][j + 3];
        t4 = A[i + ii][j + 4];
        t5 = A[i + ii][j + 5];
        t6 = A[i + ii][j + 6];
        t7 = A[i + ii][j + 7];

        B[j][(j + 8) % 64 + ii] = t0;
        B[j + 1][(j + 8) % 64 + ii] = t1;
        B[j + 2][(j + 8) % 64 + ii] = t2;
        B[j + 3][(j + 8) % 64 + ii] = t3;
        B[j][(j + 8) % 64 + 4 + ii] = t4;
        B[j + 1][(j + 8) % 64 + 4 + ii] = t5;
        B[j + 2][(j + 8) % 64 + 4 + ii] = t6;
        B[j + 3][(j + 8) % 64 + 4 + ii] = t7;
      }
      for (jj = 0; jj < 4; ++jj) {
        t0 = A[i + 4][j + jj];
        t1 = A[i + 5][j + jj];
        t2 = A[i + 6][j + jj];
        t3 = A[i + 7][j + jj];
        t4 = B[j + jj][(j + 8) % 64 + 4];
        t5 = B[j + jj][(j + 8) % 64 + 5];
        t6 = B[j + jj][(j + 8) % 64 + 6];
        t7 = B[j + jj][(j + 8) % 64 + 7];

        B[j + jj][(j + 8) % 64 + 4] = t0;
        B[j + jj][(j + 8) % 64 + 5] = t1;
        B[j + jj][(j + 8) % 64 + 6] = t2;
        B[j + jj][(j + 8) % 64 + 7] = t3;
        B[j + jj + 4][(j + 8) % 64] = t4;
        B[j + jj + 4][(j + 8) % 64 + 1] = t5;
        B[j + jj + 4][(j + 8) % 64 + 2] = t6;
        B[j + jj + 4][(j + 8) % 64 + 3] = t7;
      }
      for (ii = 4; ii < 8; ++ii) {
        t1 = A[i + ii][j + 4];
        t2 = A[i + ii][j + 5];
        t3 = A[i + ii][j + 6];
        t4 = A[i + ii][j + 7];
        B[j + 4][(j + 8) % 64 + ii] = t1;
        B[j + 5][(j + 8) % 64 + ii] = t2;
        B[j + 6][(j + 8) % 64 + ii] = t3;
        B[j + 7][(j + 8) % 64 + ii] = t4;
      }
      for (jj = 4; jj < 8; ++jj) {
        for (ii = 0; ii < 8; ++ii) {
          B[j + jj][i + ii] = B[j + jj][(j + 8) % 64 + ii];
        }
      }
      for (jj = 0; jj < 4; ++jj) {
        for (ii = 0; ii < 8; ++ii) {
          B[j + jj][i + ii] = B[j + jj][(j + 8) % 64 + ii];
        }
      }

      for (i = (j + 8) % 64; i != j; i = (i + 8) % 64) {
        //+---+---+
        //|A1 |A2 |
        //+---+---+
        //|A3 |A4 |
        //+---+---+

        //+---+---+
        //|A1T|A2T|
        //+---+---+
        //|B3 |B4 |
        //+---+---+

        //+---+---+
        //|A1T|A3T|
        //+---+---+
        //|A2T|B4 |
        //+---+---+

        //+---+---+
        //|A1T|A3T|
        //+---+---+
        //|A2T|A4T|
        //+---+---+
        for (ii = i; ii < i + 4; ++ii) {
          t0 = A[ii][j];
          t1 = A[ii][j + 1];
          t2 = A[ii][j + 2];
          t3 = A[ii][j + 3];
          t4 = A[ii][j + 4];
          t5 = A[ii][j + 5];
          t6 = A[ii][j + 6];
          t7 = A[ii][j + 7];

          B[j][ii] = t0;
          B[j + 1][ii] = t1;
          B[j + 2][ii] = t2;
          B[j + 3][ii] = t3;
          B[j][ii + 4] = t4;
          B[j + 1][ii + 4] = t5;
          B[j + 2][ii + 4] = t6;
          B[j + 3][ii + 4] = t7;
        }
        for (jj = j; jj < j + 4; ++jj) {
          t0 = A[i + 4][jj];
          t1 = A[i + 5][jj];
          t2 = A[i + 6][jj];
          t3 = A[i + 7][jj];
          t4 = B[jj][i + 4];
          t5 = B[jj][i + 5];
          t6 = B[jj][i + 6];
          t7 = B[jj][i + 7];

          B[jj][i + 4] = t0;
          B[jj][i + 5] = t1;
          B[jj][i + 6] = t2;
          B[jj][i + 7] = t3;
          B[jj + 4][i] = t4;
          B[jj + 4][i + 1] = t5;
          B[jj + 4][i + 2] = t6;
          B[jj + 4][i + 3] = t7;
        }
        for (ii = i + 4; ii < i + 8; ++ii) {
          t1 = A[ii][j + 4];
          t2 = A[ii][j + 5];
          t3 = A[ii][j + 6];
          t4 = A[ii][j + 7];
          B[j + 4][ii] = t1;
          B[j + 5][ii] = t2;
          B[j + 6][ii] = t3;
          B[j + 7][ii] = t4;
        }
      }
    }

  } else {
    //8X8 block, easy pass
    for (j = 0; j < M; j += 8) {
      for (i = 0; i < N; i += 8) {
        for (ii = 0; ii + i < N && ii < 8; ++ii) {
          for (jj = 0; jj + j < M && jj < 8; ++jj) {
            t0 = A[ii + i][jj + j];
            B[jj + j][ii + i] = t0;
          }
        }
      }
    }
  }
}

/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

/*
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";

void trans(int M, int N, int A[N][M], int B[M][N]) {
  int i, j, tmp;

  for (i = 0; i < N; i++) {
    for (j = 0; j < M; j++) {
      tmp = A[i][j];
      B[j][i] = tmp;
    }
  }
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions() {
  /* Register your solution function */
  registerTransFunction(transpose_submit, transpose_submit_desc);

  /* Register any additional transpose functions */
  registerTransFunction(trans, trans_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N]) {
  int i, j;

  for (i = 0; i < N; i++) {
    for (j = 0; j < M; ++j) {
      if (A[i][j] != B[j][i]) {
        return 0;
      }
    }
  }
  return 1;
}
