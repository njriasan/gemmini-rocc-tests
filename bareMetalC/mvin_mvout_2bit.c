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

int main() {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif

  // printf("Flush\n");
  gemmini_flush(0);

  static elem_t In[DIM][DIM/4] row_align(1);
  static elem_t Out[4][DIM][DIM/4] row_align(1);
  // Matrix used to make sure the mvout doesn't write more than DIM * DIM/2 bytes
  static elem_t OneTwentyThree[DIM][DIM/4] row_align(1);

  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM/4; ++j) {
      In[i][j] = (rand() % 256);
      Out[3][i][j] = 123;
      OneTwentyThree[i][j] = 123;
    }
  }


  printf("Set the load bitwidth to 2 (2^1)\n");
  gemmini_config_ld_precision_bits(DIM / 4, 1); // Use 1 because 2 = 2^1

  printf("Move In the data\n");
  gemmini_mvin(In, DIM);
  gemmini_fence();

  printf("Set the store bitwidth to 2 (2^1)\n");
  gemmini_config_st_precision_bits(DIM / 4, 1); // Use 1 because 2 = 2^1

  printf("Move Out the data\n");
  gemmini_mvout(Out[0], DIM);
  gemmini_fence();
  gemmini_mvout(Out[1], DIM);
  gemmini_fence();
  gemmini_mvout(Out[2], DIM);

  printf("Fence\n");
  gemmini_fence();

  int exitcode = 0;
  if (!is_equal_2bit(In, Out[0])) {
    printf("Input and Output Matrix do not match\n");
    printf("Matrix Input:\n");
    printMatrix_2bit(In);
    printf("Matrix Output:\n");
    printMatrix_2bit(Out[0]);
    printf("Matrix Output:\n");
    printMatrix_2bit(Out[1]);
    printf("Matrix Output:\n");
    printMatrix_2bit(Out[2]);
    printf("\n");
    exitcode = 1;
  }
  if (!is_equal_2bit(OneTwentyThree, Out[3])) {
    printf("Output matrix had more than DIM * DIM/2 Bytes changed\n");
    printf("Expected Leftover Bytes:\n");
    printMatrix_2bit(OneTwentyThree);
    printf("Actual Leftover Output:\n");
    printMatrix_2bit(Out[2]);
    printf("\n");
    exitcode = 1; 
  }

  if (exitcode == 0) {
    printf("Success!\n");
  }
  exit(exitcode);
}

