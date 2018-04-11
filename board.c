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

static struct sudoku_board *free_board_pool = NULL;

static const unsigned int rowcol_to_tile[9][9] = {
  { 0, 0, 0, 1, 1, 1, 2, 2, 2 },
  { 0, 0, 0, 1, 1, 1, 2, 2, 2 },
  { 0, 0, 0, 1, 1, 1, 2, 2, 2 },

  { 3, 3, 3, 4, 4, 4, 5, 5, 5 },
  { 3, 3, 3, 4, 4, 4, 5, 5, 5 },
  { 3, 3, 3, 4, 4, 4, 5, 5, 5 },

  { 6, 6, 6, 7, 7, 7, 8, 8, 8 },
  { 6, 6, 6, 7, 7, 7, 8, 8, 8 },
  { 6, 6, 6, 7, 7, 7, 8, 8, 8 }
};


static
void init_board(struct sudoku_board *board)
{
  int row, col, i;
  struct sudoku_cell *cell;
  int tile_ref_next_free_idx[9];

  for (i=0; i<9; i++)
    tile_ref_next_free_idx[i] = 0;

  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      cell = &board->cells[row][col];
      cell->board_ref = board;
      cell->row = row;
      cell->col = col;
      cell->tile =rowcol_to_tile[row][col];
      cell->number = 0;
      cell->reserved_for_number_set = 0;
      cell->row_taken_set_ref = &board->row_taken_set[row];
      cell->col_taken_set_ref = &board->col_taken_set[col];
      cell->tile_taken_set_ref = &board->tile_taken_set[rowcol_to_tile[row][col]];

      board->tile_ref[cell->tile][tile_ref_next_free_idx[cell->tile]++] = cell;
    }
  }

  for (i=0; i<9; i++) {
    board->row_taken_set[i] = 0;
    board->col_taken_set[i] = 0;
    board->tile_taken_set[i] = 0;
  }

  board->undetermined_count = (9*9);
  board->dead = 0;
  board->solutions_count = 0;
  board->solutions_list = NULL;
  board->next = NULL;
  board->nest_level = 0;
  board->debug_level = 0;
}


static
void init_board_from_orig(struct sudoku_board *board, struct sudoku_board *orig_board)
{
  int row, col, i;
  struct sudoku_cell *cell, *orig_cell;
  int tile_ref_next_free_idx[9];

  for (i=0; i<9; i++)
    tile_ref_next_free_idx[i] = 0;

  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      cell = &board->cells[row][col];
      orig_cell = &orig_board->cells[row][col];
      cell->board_ref = board;
      cell->row = row;
      cell->col = col;
      cell->tile =rowcol_to_tile[row][col];
      cell->number = orig_cell->number;
      cell->reserved_for_number_set = orig_cell->reserved_for_number_set;
      cell->row_taken_set_ref = &board->row_taken_set[row];
      cell->col_taken_set_ref = &board->col_taken_set[col];
      cell->tile_taken_set_ref = &board->tile_taken_set[rowcol_to_tile[row][col]];

      board->tile_ref[cell->tile][tile_ref_next_free_idx[cell->tile]++] = cell;
    }
  }

  for (i=0; i<9; i++) {
    board->row_taken_set[i] = orig_board->row_taken_set[i];
    board->col_taken_set[i] = orig_board->col_taken_set[i];
    board->tile_taken_set[i] = orig_board->tile_taken_set[i];
  }

  board->undetermined_count = orig_board->undetermined_count;
  board->dead = orig_board->dead;
  board->solutions_count = 0;
  board->solutions_list = NULL;
  board->next = NULL;
  board->nest_level = orig_board->nest_level;
  board->debug_level = orig_board->debug_level;
}


void copy_board(struct sudoku_board *src, struct sudoku_board *dest)
{
  int row, col, i;
  struct sudoku_cell *src_cell, *dest_cell;

  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      src_cell = &src->cells[row][col];
      dest_cell = &dest->cells[row][col];
      
      dest_cell->number = src_cell->number;
      dest_cell->reserved_for_number_set = src_cell->reserved_for_number_set;
    }
  }

  for (i=0; i<9; i++) {
    dest->row_taken_set[i] = src->row_taken_set[i];
    dest->col_taken_set[i] = src->col_taken_set[i];
    dest->tile_taken_set[i] = src->tile_taken_set[i];
  }

  dest->undetermined_count = src->undetermined_count;
  dest->dead = src->dead;
  dest->nest_level = src->nest_level;
  dest->debug_level = src->debug_level;
}


struct sudoku_board* create_board()
{
  struct sudoku_board *board;

  if (free_board_pool) {
    board = free_board_pool;
    free_board_pool = board->next;
    board->next = NULL;
  } else {
    board = (struct sudoku_board*) malloc(sizeof(struct sudoku_board));
  }
  
  if (board)
    init_board(board);
  
  return board;
}


void destroy_board(struct sudoku_board **board)
{
  struct sudoku_board *current, *next;

  assert((*board)->next == NULL);

  current = (*board)->solutions_list;
  while (current) {
    next = current->next;
    free(current);
    current = next;
  }

  (*board)->next = free_board_pool;
  free_board_pool = (*board); 
  *board = NULL;
}


struct sudoku_board* dupilcate_board(struct sudoku_board *board)
{
  struct sudoku_board *dup;

  if (free_board_pool) {
    dup = free_board_pool;
    free_board_pool = dup->next;
    board->next = NULL;
  } else {
    dup = (struct sudoku_board*) malloc(sizeof(struct sudoku_board));
  }
  
  if (dup) 
    init_board_from_orig(dup, board);
  
  return dup;
}


static
int same_solution_boards(struct sudoku_board *board_a, struct sudoku_board *board_b)
{
 int row, col;

  for (row=0; row<9; row++)
    for (col=0; col<9; col++) 
      if (board_a->cells[row][col].number != board_b->cells[row][col].number)
        return 0;
          
  return 1;
}


void add_to_board_solutions_list(struct sudoku_board *board, struct sudoku_board *solution_board)
{
  struct sudoku_board *current;
  
  assert(solution_board->next == NULL);
  solution_board->next = NULL;
  
  current = board->solutions_list;
  if (current == NULL) {
    board->solutions_list = solution_board;
    assert(board->solutions_count == 0);
    board->solutions_count = 1;
  } else {
    while (current) {
      if (same_solution_boards(current, solution_board)) {
        destroy_board(&solution_board);
        return;
      }
      current = current->next;
    }
    solution_board->next = board->solutions_list;
    board->solutions_list = solution_board;
    board->solutions_count++;   
  }
}


void add_list_to_board_solutions_list(struct sudoku_board *board, struct sudoku_board *solution_board_list)
{
  struct sudoku_board *current, *next;
    
  current = solution_board_list;
  while (current) {
    next = current->next;
    current->next = NULL;
    add_to_board_solutions_list(board, current);
    current = next;
  }
}


void print_board(struct sudoku_board *board)
{
  int row, col;
  unsigned int number;

  for (row=0; row<9; row++) {    
    for (col=0; col<9; col++) {
      number = board->cells[row][col].number;

      printf(" %c", ( (number > 0) ? ('0' + number) : '.' ) );
      if ((col == 2) || (col == 5))
          printf(" |");
    }
    printf("\n");
    if ((row == 2) || (row == 5))
      printf("-------+-------+-------\n");
  }
}


void print_board_simple(struct sudoku_board *board)
{
  int row, col;

  for (row=0; row<9; row++) {    
    for (col=0; col<9; col++) {
      printf("%c", '0' + board->cells[row][col].number);
    }
    printf("\n");
  }
}


void print_board_line(FILE *f, struct sudoku_board *board)
{
  int row, col;

  for (row=0; row<9; row++) {    
    for (col=0; col<9; col++) {
      fprintf(f, "%c", '0' + board->cells[row][col].number);
    }
  }
  fprintf(f, "\n");
}


void print_board_latex(struct sudoku_board *board)
{
  int row, col;
  char ch;

  for (row=0; row<9; row++) {    
    printf("\\setrow ");
    for (col=0; col<9; col++) {
      ch = ('0' + board->cells[row][col].number);
      printf("{%c}", ((ch == '0') ? ' ' : ch));
      if ((col == 2) || (col == 5))
          printf("  ");
    }
    printf("\n");
    if ((row == 2) || (row == 5))
      printf("\n");
  }
}
