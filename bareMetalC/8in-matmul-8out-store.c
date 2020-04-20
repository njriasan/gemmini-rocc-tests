// See LICENSE for license details.

// The goal of this test is to verify that sign extended 8-bit multiplications run in hardware will
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

void software_matmul(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t C[DIM][DIM]) {
  for (size_t j = 0; j < DIM; ++j) {
    for (size_t k = 0; k < DIM; ++k) {
      for (size_t i = 0; i < DIM; ++i) {
         elem_t a = A[i][k];
         elem_t b = B[k][j];
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
  elem_t In_1[DIM][DIM] row_align(1);
  elem_t In_2[DIM][DIM] row_align(1);

  // Allocate random values for each of the inputs
  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM; ++j) {
        elem_t top = (rand() % 3) - 1;
        elem_t bottom = (rand() % 3) - 1;
        In_1[i][j] = top;
        In_2[i][j] = bottom;
    }
  }

  // Output matrix used to calculate the results
  elem_t Out[DIM][DIM];

  // Output matrix used to calculate the result in software
  elem_t Out_Software[DIM][DIM];
  memset(Out_Software, 0, DIM * DIM * sizeof(elem_t));

  printf("Calculate the scratchpad addresses of all our matrices\n");
  printf("  Note: The scratchpad is \"row-addressed\", where each address contains one matrix row\n");
  size_t In_1_sp_addr = 0;
  size_t Out_sp_addr = DIM;
  size_t In_2_sp_addr = 2*DIM;

  printf("Set the load bitwidth to 8 (2^3)\n");
  gemmini_config_ld_precision_bits(DIM, 3); // Use 3 because 8 = 2^3

  printf("Move \"In_1\" matrix from main memory into Gemmini's scratchpad\n");
  // Take matrix from in and just move it into scratchpad
  // Replace this instruction with one that can specify bit witdth
  gemmini_mvin(In_1, In_1_sp_addr);

  printf("Move \"In_2\" matrix from main memory into Gemmini's scratchpad\n");
  // Replace this instruction with one that can specify bit witdth
  gemmini_mvin(In_2, In_2_sp_addr);

  printf("Multiply \"In_1\" matrix with \"In_2\" matrix with a bias of 0\n");
  printf("Set the ex bitwidth to 8 (2^3)\n"); 
  gemmini_config_ex_precision_bits(OUTPUT_STATIONARY, 0, 0, 0, 0, 3);
  gemmini_preload_zeros(Out_sp_addr);
  gemmini_compute_preloaded(In_1_sp_addr, In_2_sp_addr);

  printf("Set the load bitwidth to 8 (2^3)\n");
  gemmini_config_st_precision_bits(DIM, 3); // Use 3 because 8 = 2^3


  printf("Move \"Out\" matrix from Gemmini's scratchpad into main memory\n");
  // No need to replace the move out instruction yet. For this test we assume
  // 8 bits. Unclear if we will need to replace this for 4 bit versions 
  gemmini_mvout(Out, Out_sp_addr);

  // Do a software version of the same results
  software_matmul(In_1, In_2, Out_Software);

  printf("Fence till Gemmini completes all memory operations\n");
  gemmini_fence();

  printf("Check whether \"Gemmini\" and \"Software\" matrices are identical\n");
  if (!is_equal(Out_Software, Out)) {
    printf("Gemmini and Software matrices are different!\n");
    printf("Input 1\n");
    printMatrix(In_1);
    printf("Input 2\n");
    printMatrix(In_2);
    printf("\"Gemmini\" matrix:\n");
    printMatrix(Out);
    printf("\"Software\" matrix:\n");
    printMatrix(Out_Software);
    printf("\n");

    exit(1);
  }

  printf("Gemmini and Software matrices are identical, as expected\n");
  exit(0);
}

