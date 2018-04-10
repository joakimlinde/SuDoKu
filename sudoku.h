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

#ifndef __SUDOKU_H__
#define __SUDOKU_H__

// Configuration parameters

#define MAX_CLUE_LIMIT     77 
#define MAX_SOLUTIONS       1 // 0 = Inifinte


// Macros

#define SET_MASK (0x3FE)
#define NUMBER_TO_SET(number) (1 << (number)) 
#define TAKEN_TO_AVAIL_SET(taken) ((~(taken)) & SET_MASK)


// Datastructures

struct sudoku_board;

struct sudoku_cell {
  struct sudoku_board *board_ref;
  unsigned int row;
  unsigned int col;
  unsigned int tile;
  unsigned int number;
  unsigned int reserved_for_number_set;
  unsigned int *row_taken_set_ref;
  unsigned int *col_taken_set_ref;
  unsigned int *tile_taken_set_ref;
};

struct sudoku_board {
  struct sudoku_cell cells[9][9];
  struct sudoku_cell *tile_ref[9][9];
  unsigned int row_taken_set[9];
  unsigned int col_taken_set[9];
  unsigned int tile_taken_set[9];
  unsigned int undetermined_count;
  int dead;
  unsigned int solutions_count;
  struct sudoku_board *solutions_list;
  struct sudoku_board *next;
  unsigned int nest_level;
  unsigned int debug_level;
};

// Functions

struct sudoku_board* create_board();

void copy_board(struct sudoku_board *src, struct sudoku_board *dest);

void destroy_board(struct sudoku_board **board);

struct sudoku_board* dupilcate_board(struct sudoku_board *board);

void add_to_board_solutions_list(struct sudoku_board *board, struct sudoku_board *solution_board);

void add_list_to_board_solutions_list(struct sudoku_board *board, struct sudoku_board *solution_board_list);

int read_board(struct sudoku_board *board, const char *str);

void print_board(struct sudoku_board *board);

void print_board_simple(struct sudoku_board *board);

void print_board_line(FILE *f, struct sudoku_board *board);

void print_board_latex(struct sudoku_board *board);

int solve(struct sudoku_board *board);

int solve_recursive(struct sudoku_board *board);

int solve_db(struct sudoku_board *board);

void print_solutions(struct sudoku_board *board);

void init();

int run_built_in_tests();

#endif
