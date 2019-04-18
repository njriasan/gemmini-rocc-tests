// See LICENSE for license details.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "include/systolic.h"
#include "util.h"

#define N (2)

void operands(int c, int * a, int * b, int * d) {
  *d = c % N;
  *b = (c / N) % N;
  *a = c / (N*N);
}

int main() {
  static elem_t ZERO[DIM][DIM];

  for (int shift = 0; shift <= 12; shift += 4) {
    static elem_t A[N][DIM][DIM];
    static elem_t B[N][DIM][DIM];
    static elem_t D[N][DIM][DIM];

    // We will try out every combination of A, B, D possible
    static elem_t C[N*N*N][DIM][DIM];
    static int64_t gold_full[N*N*N][DIM][DIM];
    static elem_t gold[N*N*N][DIM][DIM];

    // ...taking into account whether we preload new weights or re-use the old ones
    static int preload[N*N*N] = {1};
    for (int i = 1; i < N*N*N; ++i)
      preload[i] = rand() % 2;

    // ...whether we pass in a D or just use zeros
    static int add_to_zeros[N*N*N];
    for (int i = 0; i < N*N*N; ++i)
      add_to_zeros[i] = rand() % 2;

    // ...and whether we accumulate on top of the previous result
    static int accumulate[N*N*N] = {0};
    for (int i = 1; i < N*N*N; ++i)
      accumulate[i] = rand() % 2;

    static int no_output[N*N*N];
    for (int i = 0; i < N*N*N-1; ++i)
      no_output[i] = accumulate[i+1];
    no_output[N*N*N-1] = 0;

    // Print the sequence out
    printf("Preloads: ");
    for (int i = 0; i < N*N*N; ++i)
      printf("%d, ", preload[i]);
    printf("\n");
    printf("Zeros: ");
    for (int i = 0; i < N*N*N; ++i)
      printf("%d, ", add_to_zeros[i]);
    printf("\n");
    printf("Accumulates: ");
    for (int i = 0; i < N*N*N; ++i)
      printf("%d, ", accumulate[i]);
    printf("\n");
    printf("No outputs: ");
    for (int i = 0; i < N*N*N; ++i)
      printf("%d, ", no_output[i]);
    printf("\n");

    for (size_t n = 0; n < N; ++n) {
      for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM; ++j) {
          A[n][i][j] = (rand() % 64) - 32;
          B[n][i][j] = (rand() % 64) - 32;
          D[n][i][j] = (rand() % 64) - 32;
        }
      }
    }

    for (size_t g = 0; g < N*N*N; ++g) {
      int a, b, d;
      operands(g, &a, &b, &d);

      // We need to find the last B value in case we aren't preloading new weights
      for (int last_g = g; last_g >= 0; --last_g) {
          int tmp_a, tmp_d;
          if (preload[last_g]) {
              operands(last_g, &tmp_a, &b, &tmp_d);
              break;
          }
      }

      if (add_to_zeros[g])
        matmul(A[a], B[b], ZERO, gold_full[g]);
      else
        matmul(A[a], B[b], D[d], gold_full[g]);

      if (accumulate[g])
        matadd(gold_full[g], gold_full[g-1], gold_full[g]);
    }

    for (size_t g = 0; g < N*N*N; ++g) {
      matshift(gold_full[g], gold[g], shift);
      matrelu(gold[g], gold[g]);
    }

    int A_addr = 0;
    int B_addr = N*DIM;
    int D_addr = 2*N*DIM;
    int C_addr = 3*N*DIM;
    int C_addr_acc = 1 << (ADDR_LEN-1);

    // Calculate the proper destination addresses of everything
    int C_addrs[N*N*N];
    for (size_t c = 0; c < N*N*N; ++c)
      C_addrs[c] = C_addr_acc + c*DIM;
    for (size_t c = 0; c < N*N*N; ++c) {
      int last_c;
      for (last_c = c; last_c >= 0; --last_c)
        if (!accumulate[last_c])
          break;
      if (c != last_c)
        C_addrs[c] = C_addrs[last_c] | (1 << (ADDR_LEN-2));
    }

    // printf("Moving in\n");
    for (size_t n = 0; n < N; ++n)
      for (size_t i = 0; i < DIM; ++i)
        matmul_mvin(A[n][i], A_addr + n*DIM + i, 0, 0, 0, 0);

    for (size_t n = 0; n < N; ++n)
      for (size_t i = 0; i < DIM; ++i)
        matmul_mvin(B[n][i], B_addr + n*DIM + i, 0, 0, 0, 0);

    for (size_t n = 0; n < N; ++n)
      for (size_t i = 0; i < DIM; ++i)
        if (n == N-1 && i == DIM-1) {
          matmul_mvin(D[n][i], D_addr + n*DIM + i, 0, 0, 1, 0);
        } else {
          matmul_mvin(D[n][i], D_addr + n*DIM + i, 0, 0, 0, 0);
        }

    // printf("Setting mode\n");
    matmul_setmode(WEIGHT_STATIONARY, shift, 1, 0);

    // printf("Matmulling\n");
    for (size_t c = 0; c < N*N*N; ++c) {
      int a, b, d;
      operands(c, &a, &b, &d);

      uint64_t d_addr = D_addr + d*DIM;
      if (add_to_zeros[c])
        d_addr = GARBAGE_ADDR;

      if (!preload[c]) {
        if (c == N*N*N-1) {
          // matmul_preload_zeros(C_addrs[c], 0, 0, 1, 0);
          matmul_preload_zeros(C_addrs[c], 0, 0, 0, 0);
        } else {
          matmul_preload_zeros(C_addrs[c], 0, 0, 0, 0);
        }
        matmul_compute_accumulated(A_addr + a*DIM, d_addr);
      } else {
        if (c == N*N*N-1) {
          // matmul_preload(B_addr + b*DIM, C_addrs[c], 0, 0, 1, 0);
          matmul_preload(B_addr + b*DIM, C_addrs[c], 0, 0, 0, 0);
        } else {
          matmul_preload(B_addr + b*DIM, C_addrs[c], 0, 0, 0, 0);
        }
        matmul_compute_preloaded(A_addr + a*DIM, d_addr);
      }
    }

    printf("Moving out\n");
    // Useless through-SA move
    for (size_t c = 0; c < N*N*N; ++c) {
      if (c == N*N*N-1) {
        matmul_preload(GARBAGE_ADDR, C_addr + c*DIM, 0, 0, 1, 0);
      } else {
        matmul_preload(GARBAGE_ADDR, C_addr + c*DIM, 0, 0, 0, 0);
      }
      matmul_compute_preloaded(GARBAGE_ADDR, C_addrs[c]);
    }

    for (size_t c = 0; c < N*N*N; ++c)
      for (size_t i = 0; i < DIM; ++i)
        if (c == 0 && i == 0) {
          matmul_mvout(C[c][i], C_addr + c*DIM + i, 0, 0, 0, 1);
        } else {
          matmul_mvout(C[c][i], C_addr + c*DIM + i, 0, 0, 0, 0);
        }

    printf("Moved out\n");
    for (int n = 0; n < N*N*N; ++n) {
      if (!no_output[n]) {
        printf("C:\n");
        printMatrix(C[n]);
        printf("Gold:\n");
        printMatrix(gold[n]);
        printf("\n");
      }
    }

    for (int n = 0; n < N*N*N; ++n)
      if (!no_output[n] && !is_equal(C[n], gold[n]))
          exit(1);
  }

  exit(0);
}
