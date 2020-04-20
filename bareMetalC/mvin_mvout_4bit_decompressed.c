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

elem_t extract_4bit_signed(elem_t num, elem_t high) {
    elem_t mask = 0b1111;
    if (high) {
        num = num >> 4;
    }
    num = num & mask;
    elem_t extend = (num >> 3) & 1;
    if (extend) {
        return num | 0xf0;
    } else {
        return num;
    }
}

void decompress_matrix(elem_t src[DIM][DIM/2], elem_t dst[DIM][DIM]) {
  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM; ++j) {
        dst[i][j] = extract_4bit_signed(src[i][j/2], j % 2);
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

  static elem_t In[DIM][DIM/2] row_align(1);
  static elem_t In_decompressed[DIM][DIM] row_align(1);
  static elem_t Out[2][DIM][DIM] row_align(1);
  // Matrix used to make sure the mvout doesn't write more than DIM * DIM/2 bytes
  static elem_t OneTwentyThree[DIM][DIM] row_align(1);

  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM; ++j) {
      In[i][j/2] = (rand() % 256);
      Out[1][i][j] = 123;
      OneTwentyThree[i][j] = 123;
    }
  }


  printf("Set the load bitwidth to 4 (2^2)\n");
  gemmini_config_ld_precision_bits(DIM /2, 2); // Use 2 because 4 = 2^2

  printf("Move In the data\n");
  gemmini_mvin(In, DIM);

  printf("Set the store bitwidth to 8 (2^3)\n");
  gemmini_config_st_precision_bits(DIM, 3); // Use 3 because 8 = 2^3

  printf("Move Out the data\n");
  gemmini_mvout(Out[0], DIM);

  printf("Fence\n");
  gemmini_fence();

  decompress_matrix(In, In_decompressed);
  int exitcode = 0;
  if (!is_equal(In_decompressed, Out[0])) {
    printf("Input and Output Matrix do not match\n");
    printf("Matrix Input:\n");
    printMatrix(In_decompressed);
    printf("Matrix Output:\n");
    printMatrix(Out[0]);
    printf("\n");
    exitcode = 1;
  }
  if (!is_equal(OneTwentyThree, Out[1])) {
    printf("Output matrix had more than DIM * DIM/2 Bytes changed\n");
    printf("Expected Leftover Bytes:\n");
    printMatrix(OneTwentyThree);
    printf("Actual Leftover Output:\n");
    printMatrix(Out[1]);
    printf("\n");
    exitcode = 1; 
  }

  if (exitcode == 0) {
    printf("Success!\n");
  }
  exit(exitcode);
}

