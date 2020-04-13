// See LICENSE for license details.

// The goal of this test is to verify that sign extended 4-bit multiplications run in hardware will
// match the result run in gemmini. We assume only signed integers for this example.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini.h"


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

void software_matmul_halfdim(elem_t A[DIM][DIM/2], elem_t B[DIM][DIM/2], elem_t C[DIM][DIM]) {
  for (size_t j = 0; j < DIM; ++j) {
    for (size_t k = 0; k < DIM; ++k) {
      for (size_t i = 0; i < DIM; ++i) {
         elem_t a = extract_4bit_signed(A[i][k /2], k % 2);
         elem_t b = extract_4bit_signed(B[k][j /2], j % 2);
         C[i][j] += a * b;
      }
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

  printf("Flush Gemmini TLB of stale virtual addresses\n");
  gemmini_flush(0);

  printf("Initialize our input and output matrices in main memory\n");
  // Allocate space for two matrices that fill half the array each
  // Assume DIM is always divisble by two
  elem_t In_1[DIM][DIM/2] row_align(1);
  elem_t In_2[DIM][DIM/2] row_align(1);

  // Allocate random values for each of the inputs
  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM/2; ++j) {
        elem_t top_1 = (rand() % 3) - 1;
        elem_t bottom_1 = (rand() % 3) - 1;
        elem_t top_2 = (rand() % 3) - 1;
        elem_t bottom_2 = (rand() % 3) - 1;
        In_1[i][j] = ((top_1 << 4) & 0xF0) | (bottom_1 & 0x0F);
        In_2[i][j] = ((top_2 << 4) & 0xF0) | (bottom_2 & 0x0F);
    }
  }

  // Output matrix used to calculate the results
  elem_t Out[DIM][DIM/2];

  // Output matrix used to calculate the temporary results to avoid overflow
  elem_t Temp_Software[DIM][DIM];
  // Output matrix used to calculate the result in software
  elem_t Out_Software[DIM][DIM/2];

  printf("Calculate the scratchpad addresses of all our matrices\n");
  printf("  Note: The scratchpad is \"row-addressed\", where each address contains one matrix row\n");
  size_t In_1_sp_addr = 0;
  size_t Out_sp_addr = DIM;
  size_t In_2_sp_addr = 2*DIM;

  printf("Set the load bitwidth to 4 (2^2)");
  gemmini_config_ld_precision_bits(DIM / 2, 2); // Use 2 because 4 = 2^2

  printf("Move \"In_1\" matrix from main memory into Gemmini's scratchpad\n");
  // Take matrix from in and just move it into scratchpad
  // Replace this instruction with one that can specify bit witdth
  gemmini_mvin(In_1, In_1_sp_addr);

  printf("Move \"In_2\" matrix from main memory into Gemmini's scratchpad\n");
  // Replace this instruction with one that can specify bit witdth
  gemmini_mvin(In_2, In_2_sp_addr);

  printf("Multiply \"In_1\" matrix with \"In_2\" matrix with a bias of 0\n");
  printf("Set the store bitwidth to 4 (2^2)");
  gemmini_config_ex_precision_bits(OUTPUT_STATIONARY, 0, 0, 0, 0, 2);
  gemmini_preload_zeros(Out_sp_addr);
  gemmini_compute_preloaded(In_1_sp_addr, In_2_sp_addr);

  printf("Move \"Out\" matrix from Gemmini's scratchpad into main memory\n");
  gemmini_mvout(Out, Out_sp_addr);


  printf("Fence till Gemmini completes all memory operations\n");
  gemmini_fence();
  // Clear software in case too much mem is transferred.
  memset(Temp_Software, 0, DIM * DIM * sizeof(elem_t));
  memset(Out_Software, 0, DIM * DIM / 2 * sizeof(elem_t));
  // Do a software version of the same results
  software_matmul_halfdim(In_1, In_2, Temp_Software);
  compress_matrix(Temp_Software, Out_Software);

  printf("Check whether \"Gemmini\" and \"Software\" matrices are identical\n");
  if (!is_equal_4bit(Out_Software, Out)) {
    printf("Gemmini and Software matrices are different!\n");
    printf("Input 1\n");
    printMatrix_4bit(In_1);
    printf("Input 2\n");
    printMatrix_4bit(In_2);
    printf("\"Gemmini\" matrix:\n");
    printMatrix_4bit(Out);
    printf("\"Software\" matrix:\n");
    printMatrix_4bit(Out_Software);
    printf("\"Software Intermediate\" matrix:\n");
    printMatrix(Temp_Software);
    printf("\n");

    exit(1);
  }

  printf("Gemmini and Software matrices are identical, as expected\n");
  exit(0);
}

