// See LICENSE for license details.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini.h"

#if (N*DIM) > (BANK_NUM*BANK_ROWS)
#error not enough scratchpad space
#endif

void compress_matrix(elem_t src[DIM][DIM], elem_t dst[DIM][DIM/2]) {
  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM/2; ++j) {
        elem_t low = src[i][2 * j];
        if (low < -8) {
            low = -8;
        } else if (low > 7) {
            low = 7;
        }
        low = low & 0X0F;
        elem_t high = src[i][(2 * j) + 1];
        if (high < -8) {
            high = -8;
        } else if (high > 7) {
            high = 7;
        }
        high = high & 0X0F;
        elem_t result = ((high << 4) & 0xF0) | low;
        dst[i][j] = result;
    }
  }
}

int main() {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif

  // printf("Flush\n");
  gemmini_flush(0);

  static elem_t In[DIM][DIM] row_align(1);
  static elem_t In_compressed[DIM][DIM/2] row_align(1);
  static elem_t Out[2][DIM][DIM/2] row_align(1);
  // Matrix used to make sure the mvout doesn't write more than DIM * DIM/2 bytes
  static elem_t OneTwentyThree[DIM][DIM/2] row_align(1);

  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM; ++j) {
      In[i][j] = (rand() % 256);
      Out[1][i][j/2] = 123;
      OneTwentyThree[i][j/2] = 123;
    }
  }


  printf("Set the load bitwidth to 8 (2^3)\n");
  gemmini_config_ld_precision_bits(DIM, 3); // Use 3 because 8 = 2^3

  printf("Move In the data\n");
  gemmini_mvin(In, DIM);

  printf("Set the store bitwidth to 4 (2^2)\n");
  gemmini_config_st_precision_bits(DIM / 2, 2); // Use 2 because 4 = 2^2

  printf("Move Out the data\n");
  gemmini_mvout(Out[0], DIM);

  printf("Fence\n");
  gemmini_fence();

  compress_matrix(In, In_compressed);
  int exitcode = 0;
  if (!is_equal_4bit(In_compressed, Out[0])) {
    printf("Input and Output Matrix do not match\n");
    printf("Matrix Input:\n");
    printMatrix_4bit(In_compressed);
    printf("Matrix Output:\n");
    printMatrix_4bit(Out[0]);
    printf("\n");
    exitcode = 1;
  }
  if (!is_equal_4bit(OneTwentyThree, Out[1])) {
    printf("Output matrix had more than DIM * DIM/2 Bytes changed\n");
    printf("Expected Leftover Bytes:\n");
    printMatrix_4bit(OneTwentyThree);
    printf("Actual Leftover Output:\n");
    printMatrix_4bit(Out[1]);
    printf("\n");
    exitcode = 1; 
  }

  if (exitcode == 0) {
    printf("Success!\n");
  }
  exit(exitcode);
}

