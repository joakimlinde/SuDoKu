//
// sudoku - A SuDoKu solver
//
// Copyright (c) 2018  Linde Labs, LLC
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "sudoku.h"


static char *test_boards[] = {
  // TODO: Add your built-in test boards here. One string per board.
};


int run_built_in_tests()
{
  struct sudoku_board *board;
  int i, solutions_count;

  for(i = 0; i < (sizeof(test_boards)/sizeof(*test_boards)); i++) {
    printf("\n===== Test board %i =====\n\n", i);
    board = create_board();
    board->debug_level = 1;
    read_board(board, test_boards[i]);
   printf("-------- Input --------\n");
    print_board(board);
    printf("---Solve---\n");
    solutions_count = solve(board);
    if (solutions_count)
      printf("Found %i solution(s)\n", solutions_count);
    else
      printf("No solution found\n");
    printf("-------- Output -------\n");
    print_solutions(board);
    assert(solutions_count);    
    destroy_board(&board);

    if (solutions_count == 0)
      return -1;
  }

  printf("\nAll %i built-in tests PASS\n\n", i);

  return 0;
};
