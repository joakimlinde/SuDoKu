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
#include <stdbool.h>
#include <assert.h>
#include "sudoku.h"


// Defines

#define DINDENT "      "

#define NUMBER_SET_TO_NUMBER(number_set) (number_set_to_number[number_set])


// Constants and alike

static unsigned int number_set_to_number[NUMBER_TO_SET(10)+1];

static unsigned int bit_count[NUMBER_TO_SET(10)+1];

static struct {
  unsigned int remaining_set;
  unsigned int index;
} set_to_index[NUMBER_TO_SET(10)+1];

static const unsigned int index_tile_mask[] = {
  0x007, // 000 000 111
  0x038, // 000 111 000
  0x1C0  // 111 000 000
};

static const unsigned int index_row_mask[] = {
  0x007, // 000 000 111
  0x038, // 000 111 000
  0x1C0  // 111 000 000
};

static const unsigned int index_col_mask[] = {
  0x049, // 001 001 001
  0x092, // 010 010 010
  0x124  // 100 100 100
};


// Struct & types

typedef int (*reserve_func_t)(struct sudoku_cell *possible_cell, 
                              unsigned int number_set);

typedef int (*reserve_with_index_set_func_t)(struct sudoku_cell *possible_cell, 
                                             unsigned int possible_index_set, 
                                             unsigned int number_set);


// Forward declarations

static void print_index_set(unsigned int number_set, const char *postfix);

static void print_number_set(unsigned int number_set, const char *postfix);

static void print_reserved_set_for_cell(struct sudoku_cell *cell);

static void print_possible(struct sudoku_board *board, const char *prefix);

int solve(struct sudoku_board *board);


// Functions

static inline
int bit_count_func(unsigned int number)
{
  int count = 0;

  while (number) {
    count += (number & 1);
    number >>= 1;
  }
  return count;
}


static inline
unsigned int set_to_index_func(unsigned int *set)
{
  int index = 0;

  if (*set == 0)
    return 0;

  while (index<10) {
    if (*set & (1 << index)) {
      *set &=  ~(1 << index);
      return index;
    }
    index++;
  }
  return 0;
}


void init()
{
  int i;

  for (i=0; i<=NUMBER_TO_SET(10); i++)
    number_set_to_number[i] = 0;

  for (i=1; i<=9; i++)
    number_set_to_number[NUMBER_TO_SET(i)] = i;

  for (i=0; i<NUMBER_TO_SET(10); i++) {
    bit_count[i] = bit_count_func(i);
    set_to_index[i].remaining_set = i;
    set_to_index[i].index = set_to_index_func(&(set_to_index[i].remaining_set));    
  }
}


static inline 
void zero_array_9(unsigned int a[9])
{
  a[0] = 0;
  a[1] = 0;
  a[2] = 0;
  a[3] = 0;
  a[4] = 0;
  a[5] = 0;
  a[6] = 0;
  a[7] = 0;
  a[8] = 0;
}


static inline
unsigned int get_next_index_from_set(unsigned int *set)
{
  unsigned int index; 

  assert(*set);
  assert(*set < NUMBER_TO_SET(10));
  index = set_to_index[*set].index; 
  *set = set_to_index[*set].remaining_set;
  assert(index<10);
  
  return index; 
}


static inline
int is_board_solved(struct sudoku_board *board)
{
  return ((board->undetermined_count == 0) || (board->solutions_count > 0));
}


static inline
void set_board_dead(struct sudoku_board *board, const char *func_name)
{
  if (board->debug_level)
    printf("Board is declared dead! (function: %s)\n", func_name);

  board->dead = 1;
}


static inline
int is_board_done(struct sudoku_board *board)
{
  return (board->dead || (board->undetermined_count == 0) || (board->solutions_count > 0));
}


static inline
void mark_board_not_dirty(struct sudoku_board *board)
{
  board->row_dirty_set = 0;
  board->col_dirty_set = 0;
  board->tile_dirty_set = 0;
}


static inline
void mark_cell_dirty(struct sudoku_cell *cell)
{
  struct sudoku_board *board = cell->board_ref;

  board->row_dirty_set |= INDEX_TO_SET(cell->row);
  board->col_dirty_set |= INDEX_TO_SET(cell->col);
  board->tile_dirty_set |= INDEX_TO_SET(cell->tile);
}


static inline 
int is_board_dirty(struct sudoku_board *board) {
  // No need to check col and tiles - row always marked dirty
  return board->row_dirty_set;
};


static inline
void mark_cell_not_empty(struct sudoku_cell *cell)
{
  struct sudoku_board *board = cell->board_ref;

  *cell->row_cell_empty_set_ref &= ~(INDEX_TO_SET(cell->col));
  if (*cell->row_cell_empty_set_ref == 0)
    board->row_empty_set &= ~(INDEX_TO_SET(cell->row));
  
  *cell->col_cell_empty_set_ref &= ~(INDEX_TO_SET(cell->row));
  if (*cell->col_cell_empty_set_ref == 0)
    board->col_empty_set &= ~(INDEX_TO_SET(cell->col));
  
  *cell->tile_cell_empty_set_ref &= ~(INDEX_TO_SET(cell->index_in_tile));
  if (*cell->tile_cell_empty_set_ref == 0)
    board->tile_empty_set &= ~(INDEX_TO_SET(cell->tile));
}


static inline
unsigned int get_cell_possible_number_set(struct sudoku_cell *cell)
{
  unsigned int taken_number_set, possible_number_set;

  taken_number_set = *cell->row_number_taken_set_ref;
  taken_number_set |= *cell->col_number_taken_set_ref;
  taken_number_set |= *cell->tile_number_taken_set_ref;

  possible_number_set = NUMBER_TAKEN_TO_AVAILABLE_SET(taken_number_set);

  if (possible_number_set && cell->reserved_for_number_set)
     possible_number_set &= cell->reserved_for_number_set;

  assert((possible_number_set==0) || IS_VALID_NUMBER_SET(possible_number_set));

  return possible_number_set;
}


static inline
unsigned int get_cell_possible_number(struct sudoku_cell *cell)
{
  return number_set_to_number[get_cell_possible_number_set(cell)];
}


static inline
struct sudoku_cell* find_cell_with_lowest_availability_count(struct sudoku_board *board)
{
  int row, col;
  unsigned int cell_bit_count, lowest_available_count;
  struct sudoku_cell *cell, *lowest_cell;
  
  lowest_available_count = 10;
  lowest_cell = NULL;
  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      cell = &board->cells[row][col];
      if (cell->number == 0) {
        cell_bit_count = bit_count[get_cell_possible_number_set(cell)];
        if (cell_bit_count == 0) {
          // Board is dead
          set_board_dead(board, __func__);
          return NULL;
        } else if (cell_bit_count < lowest_available_count) {
          lowest_available_count = cell_bit_count;
          lowest_cell = cell;
        }
      }
    }
  }

  return lowest_cell;
}


static inline
void set_cell_number(struct sudoku_cell *cell, unsigned int number)
{
  assert(IS_VALID_NUMBER(number));

  unsigned int number_set;

  if (cell->number == 0)
    cell->board_ref->undetermined_count--;

  assert(cell->number == 0);
  assert(number);

  cell->number = number;

  number_set = NUMBER_TO_SET(number);
  cell->reserved_for_number_set = number_set;
  *cell->row_number_taken_set_ref |= number_set;
  *cell->col_number_taken_set_ref |= number_set;
  *cell->tile_number_taken_set_ref |= number_set;

  mark_cell_not_empty(cell);
  mark_cell_dirty(cell);
}


static inline
void set_cell_number_and_log(struct sudoku_cell *cell, unsigned int number)
{
  assert(IS_VALID_NUMBER(number));

  set_cell_number(cell, number);

  if (cell->board_ref->debug_level) {
    if (cell->board_ref->debug_level > 1)
      printf(DINDENT);  
    printf("[%i,%i]  =  %i\n", cell->row, cell->col, number);  
  }
}


static
void handle_bad_reserve_cell(struct sudoku_cell *cell, unsigned int number_set)
{
  unsigned int taken_set, available_set;

  assert(IS_VALID_NUMBER_SET(number_set));

  taken_set = *cell->row_number_taken_set_ref;
  taken_set |= *cell->col_number_taken_set_ref;
  taken_set |= *cell->tile_number_taken_set_ref;

  available_set = NUMBER_TAKEN_TO_AVAILABLE_SET(taken_set);

  if ((cell->board_ref->debug_level == 0) && (number_set == 0)) {
    // Do nothing, but set_board_bad (below) !!!
  } else {
    printf("ERROR: reserve_cell([%i,%i] req: ", cell->row, cell->col);
    print_number_set(number_set, "existing: ");
    print_number_set(cell->reserved_for_number_set, "available: ");
    print_number_set(available_set, NULL);
    printf(" number: %i\n", cell->number);
    assert(0);
  }
  set_board_dead(cell->board_ref, __func__);
}


static inline
int reserve_cell(struct sudoku_cell *cell, unsigned int number_set)
{
  unsigned int taken_set, available_set, new_reserved_for_number_set;
  int changed;

  assert(IS_VALID_NUMBER_SET(number_set));

  taken_set = *cell->row_number_taken_set_ref;
  taken_set |= *cell->col_number_taken_set_ref;
  taken_set |= *cell->tile_number_taken_set_ref;

  available_set = NUMBER_TAKEN_TO_AVAILABLE_SET(taken_set);

  // Make sure it is available to be reserved
  if (cell->number) {
    handle_bad_reserve_cell(cell, number_set);
    return 0;
  }

  // Make sure you only reserve numbers that are available
  if (((number_set & available_set) == 0) ||
      (cell->reserved_for_number_set &&
       ((cell->reserved_for_number_set | number_set) != cell->reserved_for_number_set))) {
    handle_bad_reserve_cell(cell, number_set);
    return 0;
  }

  number_set &= available_set;

  changed = 0;
  if (cell->reserved_for_number_set) {
    // Take the more restrictive set, make sure it's not zero
    new_reserved_for_number_set = (cell->reserved_for_number_set & number_set);

    if (new_reserved_for_number_set && (cell->reserved_for_number_set != new_reserved_for_number_set)) {
      cell->reserved_for_number_set = new_reserved_for_number_set;
      if (bit_count[new_reserved_for_number_set] == 1)
        mark_cell_dirty(cell);
      changed = 1;
    }
  } else {
    cell->reserved_for_number_set = number_set;
    if (bit_count[number_set] == 1)
      mark_cell_dirty(cell);
    changed = 1;
  }

  return changed;
}

static inline
int reserve_cell_and_log(struct sudoku_cell *cell, unsigned int number_set, const char *func_name)
{
  struct sudoku_board *board;
  int changed;

  board = cell->board_ref;
  if ((board->debug_level >= 3) && (func_name)) {     
    printf(DINDENT "%s: [%i,%i] = ", func_name, cell->row, cell->col);
    print_number_set(number_set, "  ( removing: ");
    print_number_set((get_cell_possible_number_set(cell) & ~number_set), ")\n");
   }

  // Check: Through logic we have reached the conclusion that no numbers are available for this cell - bad board! 
  if (number_set == 0) {
    set_board_dead(board, func_name);
    return 0;
  }

  changed = reserve_cell(cell, number_set);
  if (changed && (board->debug_level >= 1))
    print_reserved_set_for_cell(cell);  

  return changed;
}

static inline
int reserve_row_in_tile(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  unsigned int index, index_set, my_row, my_tile;
  unsigned int reserve_number_set, possible_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;
  int changed;

  assert(IS_VALID_NUMBER_SET(number_set));

  changed = 0;
  my_row = possible_cell->row;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;

  if (board->debug_level >= 4) {
    printf(DINDENT "%s: Removing numbers from cells around [%i,%i] in tile. Numbers to remove: ",
           __func__, possible_cell->row, possible_cell->col);
    print_number_set(number_set, "\n");
  }

  // Loop over all index in my_tile with empty cells
  index_set = board->tile_cell_empty_set[my_tile];
  while (index_set) {
    index = get_next_index_from_set(&index_set);
    cell = board->tile_ref[my_tile][index];
    assert((cell->tile == my_tile) && (cell->number == 0));
    
    possible_set = get_cell_possible_number_set(cell);
    if (possible_set & number_set) {
      // The number is a possibility for this cell
      if (cell->row != my_row) {
        // This is my tile and not my row so exclude the number in the reservation
        // We have already dealt with the cells in my row previously outside this function
        reserve_number_set = possible_set & (~number_set);
        changed += reserve_cell_and_log(cell, reserve_number_set, __func__);
      }
    }
  }

  return changed;
}


static inline
int reserve_col_in_tile(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  unsigned int index, index_set, my_col, my_tile;
  unsigned int reserve_number_set, possible_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;
  int changed;

  assert(IS_VALID_NUMBER_SET(number_set));

  changed = 0;
  my_col = possible_cell->col;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;

  if (board->debug_level >= 4) {
    printf(DINDENT "%s: Removing numbers from cells around [%i,%i] in tile. Numbers to remove: ",
           __func__, possible_cell->row, possible_cell->col);
    print_number_set(number_set, "\n");
  }

  // Loop over all index in my_tile with empty cells
  index_set = board->tile_cell_empty_set[my_tile];
  while (index_set) {
    index = get_next_index_from_set(&index_set);
    cell = board->tile_ref[my_tile][index];
    assert((cell->tile == my_tile) && (cell->number == 0));

    possible_set = get_cell_possible_number_set(cell);
    if (possible_set & number_set) {
      // The number is a possibility for this cell
      if (cell->col != my_col) {
        // This is my tile and not my col so exclude the number in the reservation
        // We have already dealt with the cells in my col previously outside this function
        reserve_number_set = possible_set & (~number_set);
        changed += reserve_cell_and_log(cell, reserve_number_set, __func__);
      }
    }
  }

  return changed;
}


static inline
int reserve_tile_in_row(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  unsigned int my_row, col, col_set, my_tile;
  unsigned int reserve_number_set, possible_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;
  int changed;

  assert(IS_VALID_NUMBER_SET(number_set));

  changed = 0;
  my_row = possible_cell->row;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;

  if (board->debug_level >= 4) {
    printf(DINDENT "%s: Removing numbers from cells around [%i,%i] in row. Numbers to remove: ",
           __func__, possible_cell->row, possible_cell->col);
    print_number_set(number_set, "\n");
  }

  // Loop over all cols in my_row with empty cells
  col_set = board->row_cell_empty_set[my_row];
  while (col_set) {
    col = get_next_index_from_set(&col_set);
    cell = &board->cells[my_row][col];
    assert(cell->number == 0);  
    
    possible_set = get_cell_possible_number_set(cell);
    if (possible_set & number_set) {
      // The number is a possibility for this cell
      if (cell->tile != my_tile) {
        // This is my row and not in my tile so exclude the number in the reservation
        // We have already dealt with the cells in my tile previously outside this function
        reserve_number_set = possible_set & (~number_set);
        changed += reserve_cell_and_log(cell, reserve_number_set, __func__);
      }
    }
  }

  return changed;
}


static inline
int reserve_tile_in_col(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  unsigned int row, row_set, my_col, my_tile;
  unsigned int reserve_number_set, possible_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;
  int changed;

  assert(IS_VALID_NUMBER_SET(number_set));

  changed = 0;
  my_col = possible_cell->col;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;

  if (board->debug_level >= 4) {
    printf(DINDENT "%s: Removing numbers from cells around [%i,%i] in col. Numbers to remove: ",
           __func__, possible_cell->row, possible_cell->col);
    print_number_set(number_set, "\n");
  }

  // Loop over all rows in my_col with empty cells
  row_set = board->col_cell_empty_set[my_col];
  while (row_set) {
    row = get_next_index_from_set(&row_set);
    cell = &board->cells[row][my_col];
    assert(cell->number == 0); 

    possible_set = get_cell_possible_number_set(cell);
    if (possible_set & number_set) {
      // The number is a possibility for this cell
      if (cell->tile != my_tile) {
        // This is my col and not my tile so exclude the number in the reservation
        // We have already dealt with the cells in my tile previously outside this function
        reserve_number_set = possible_set & (~number_set);
        changed += reserve_cell_and_log(cell, reserve_number_set, __func__);
      }
    }
  }

  return changed;
}


static inline
int reserve_cells_with_index_in_tile(struct sudoku_cell *possible_cell, unsigned int possible_index_set, unsigned int number_set)
{
  unsigned int my_tile, index, index_set, i;
  unsigned int reserve_number_set, possible_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;
  int changed;

  assert(IS_VALID_INDEX_SET(possible_index_set));
  assert(IS_VALID_NUMBER_SET(number_set));
  assert(bit_count[possible_index_set] > 1);
  assert(bit_count[number_set] > 1);
  assert(bit_count[possible_index_set] == bit_count[number_set]);

  changed = 0;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;

  if (board->debug_level >= 4) {
    printf(DINDENT "%s: Reserving numbers around [%i,%i] in tile. Numbers to reserve: ",
           __func__, possible_cell->row, possible_cell->col);
    print_number_set(number_set, "\n");
  }

  // Loop over all index in my_tile with empty cells
  index_set = board->tile_cell_empty_set[my_tile];
  while (index_set) {
    index = get_next_index_from_set(&index_set);
    cell = board->tile_ref[my_tile][index];
    assert(cell->number == 0);

    possible_set = get_cell_possible_number_set(cell);
    if (possible_set & number_set) {
      // The number is a possibility for this cell
      if (INDEX_TO_SET(index) & possible_index_set) {
        // This is cell so include the number in the reservation
        reserve_number_set = possible_set & number_set;
      } else {
        // This is not my cell so exclude the number in the reservation
        reserve_number_set = possible_set & (~number_set);
      }
      
      // Make the reservation if needed
      if (possible_set != reserve_number_set)
        changed += reserve_cell_and_log(cell, reserve_number_set, __func__);
    }
  }

  if (bit_count[possible_index_set] <= 3) {
    // If all possibilities (=2 or 3) on the same row, if so have the row reserve them
    for (i=0; i<3; i++) {
      if ((possible_index_set | index_row_mask[i]) == index_row_mask[i])
        changed += reserve_tile_in_row(possible_cell, number_set);
    }

    // If all possibilities (=2 or 3) on the same col, if so have the col reserve them
    for (i=0; i<3; i++) {
      if ((possible_index_set | index_col_mask[i]) == index_col_mask[i])
        changed += reserve_tile_in_col(possible_cell, number_set);
    }
  }

  return changed;
}


static inline
int reserve_cells_with_index_in_row(struct sudoku_cell *possible_cell, unsigned int possible_index_set, unsigned int number_set)
{
  unsigned int my_row, col, col_set, i;
  unsigned int reserve_number_set, possible_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;
  int changed;

  assert(IS_VALID_INDEX_SET(possible_index_set));
  assert(IS_VALID_NUMBER_SET(number_set));
  assert(bit_count[possible_index_set] > 1);
  assert(bit_count[number_set] > 1);
  assert(bit_count[possible_index_set] == bit_count[number_set]);

  changed = 0;
  my_row = possible_cell->row;
  board = possible_cell->board_ref;

  if (board->debug_level >= 4) {
    printf(DINDENT "%s: Reserving numbers around [%i,%i] in row. Numbers to reserve: ",
           __func__, possible_cell->row, possible_cell->col);
    print_number_set(number_set, "\n");
  }

  // Loop over all cols in my_row with empty cells
  col_set = board->row_cell_empty_set[my_row];
  while (col_set) {
    col = get_next_index_from_set(&col_set);
    cell = &board->cells[my_row][col];
    assert(cell->number == 0); 

    possible_set = get_cell_possible_number_set(cell);
    if (possible_set & number_set) {
      // The number is a possibility for this cell
      if (INDEX_TO_SET(col) & possible_index_set) {
        // This is cell so include the number in the reservation
        reserve_number_set = possible_set & number_set;
      } else {
        // This is not my cell so exclude the number in the reservation
        reserve_number_set = possible_set & (~number_set);
      }

      // Make the reservation if needed
      if (possible_set != reserve_number_set)
        changed += reserve_cell_and_log(cell, reserve_number_set, __func__);
    }
  }

  if (bit_count[possible_index_set] <= 3) {
    // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
    for (i=0; i<3; i++) {
      if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i])
        changed += reserve_row_in_tile(possible_cell, number_set);
    }
  }

  return changed;
}


static inline
int reserve_cells_with_index_in_col(struct sudoku_cell *possible_cell, unsigned int possible_index_set, unsigned int number_set)
{
  unsigned int row, row_set, my_col, my_tile, i;
  unsigned int reserve_number_set, possible_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;
  int changed;

  assert(IS_VALID_INDEX_SET(possible_index_set));
  assert(IS_VALID_NUMBER_SET(number_set));
  assert(bit_count[possible_index_set] > 1);
  assert(bit_count[number_set] > 1);
  assert(bit_count[possible_index_set] == bit_count[number_set]);

  changed = 0;
  my_col = possible_cell->col;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;

  if (board->debug_level >= 4) {
    printf(DINDENT "%s: Reserving numbers around [%i,%i] in col. Numbers to reserve: ",
           __func__, possible_cell->row, possible_cell->col);
    print_number_set(number_set, "\n");
  }

  // Loop over all rows in my_col with empty cells
  row_set = board->col_cell_empty_set[my_col];
  while (row_set) {
    row = get_next_index_from_set(&row_set);
    cell = &board->cells[row][my_col];
    assert(cell->number == 0);

    possible_set = get_cell_possible_number_set(cell);
    if (possible_set & number_set) {
      // The number is a possibility for this cell
      if (INDEX_TO_SET(row) & possible_index_set) {
        // This is cell so include the number in the reservation
        reserve_number_set = possible_set & number_set;
      } else {
        // This is not mmy cell so exclude the number in the reservation
        reserve_number_set = possible_set & (~number_set);
      }

      // Make the reservation if needed
      if (possible_set != reserve_number_set)
        changed += reserve_cell_and_log(cell, reserve_number_set, __func__);
    }
  }

  if (bit_count[possible_index_set] <= 3) {
    // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
    for (i=0; i<3; i++) {
      if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i]) 
        changed += reserve_col_in_tile(possible_cell, number_set);
    }
  }

  return changed;
}


static
int propagate_constraints(struct sudoku_board *board)
{
  unsigned int row, col, tile, index, row_set, col_set, tile_set, index_set;
  int changed;
  struct sudoku_cell *cell;
  unsigned int number;

  if (board->debug_level >= 2)
    printf("  Propagate constraints\n");

  if (board->row_dirty_set == 0)
    return 0;

  changed = 0;
  do {
    // Loop over all empty cells on dirty rows by going row by row and col by col
    row_set = board->row_empty_set & board->row_dirty_set;
    board->row_dirty_set = 0;
    while (row_set) {
      row = get_next_index_from_set(&row_set);
      col_set = board->row_cell_empty_set[row];
      while (col_set) {
        col = get_next_index_from_set(&col_set);
        cell = &board->cells[row][col];
        if (board->debug_level >= 4)
          printf(DINDENT "Empty cell in dirty row [%i,%i]\n", row, col);
        assert(cell->number == 0);
        number = get_cell_possible_number(cell);

        if (number) {
          set_cell_number_and_log(cell, number);
          changed++;
        }
      }
    }

    // Loop over all empty cells on dirty cols by going col by col and row by row
    col_set = board->col_empty_set & board->col_dirty_set;
    board->col_dirty_set = 0;
    while (col_set) {
      col = get_next_index_from_set(&col_set);
      row_set = board->col_cell_empty_set[col];
      while (row_set) {
        row = get_next_index_from_set(&row_set);
        cell = &board->cells[row][col];
        if (board->debug_level >= 4)
          printf(DINDENT "Empty cell in dirty col [%i,%i]\n", row, col);
        assert(cell->number == 0);
        number = get_cell_possible_number(cell);

        if (number) {
          set_cell_number_and_log(cell, number);
          changed++;
        }
      }
    }

    // Loop over all empty cells on dirty tiles by going tile by tile and index by index
    tile_set = board->tile_empty_set & board->tile_dirty_set;
    board->tile_dirty_set = 0;
    while (tile_set) {
      tile = get_next_index_from_set(&tile_set);
      index_set = board->tile_cell_empty_set[tile];
      while (index_set) {
        index = get_next_index_from_set(&index_set);
        cell = board->tile_ref[tile][index];
        if (board->debug_level >= 4)
          printf(DINDENT "Empty cell in dirty tile [%i,%i]\n", cell->row, cell->col);
        assert(cell->number == 0);
        number = get_cell_possible_number(cell);

        if (number) {
          set_cell_number_and_log(cell, number);
          changed++;
        }
      }
    }
  } while ((board->row_dirty_set | board->col_dirty_set | board->tile_dirty_set) && !is_board_done(board));

  return changed;
}


static
int solve_possible(struct sudoku_board *board)
{
  unsigned int row, col, row_set, col_set;
  int round, changed, changed_total;
  struct sudoku_cell *cell;
  unsigned int number;

  if (board->debug_level >= 2)
    printf("Solve possible\n");

  changed_total = 0;
  round = 0;
  do {
    if (board->debug_level >= 2)
      printf("  Round %i\n", round++);

    // Loop over all empty cells by going row by row and col by col
    changed = 0;    
    row_set = board->row_empty_set;
    while (row_set) {
      row = get_next_index_from_set(&row_set);
      col_set = board->row_cell_empty_set[row];
      while (col_set) {
        col = get_next_index_from_set(&col_set);
        cell = &board->cells[row][col];
        assert(cell->number == 0);
        number = get_cell_possible_number(cell);

        if (number) {
          set_cell_number_and_log(cell, number);
          changed++;
        }
      }
    }

    changed_total += changed;
  } while (changed && !is_board_done(board));

  mark_board_not_dirty(board);

  return changed_total;
}


// === Looking at a number and see what index we can stuff into it ===
//
// For each number and its corresponding Possible Index Set (prior_possible_index_set[9]):
//   Find indices that can be grouped together that share the same possible numbers.
static inline
int find_and_reserve_group_with_number(struct sudoku_cell *cell, 
                                       unsigned int prior_possible_index_set[9], int number, 
                                       unsigned int possible_index_set, 
                                       reserve_with_index_set_func_t reserve_with_index_set_func,
                                       const char *parent_func_name)
{
  int i, j, changed, possibilities, same_index_set_count;
  unsigned int reserve_number_set, joint_index_set;

  assert(IS_VALID_NUMBER(number));
  assert(IS_VALID_INDEX_SET(possible_index_set));
  assert(bit_count[possible_index_set] > 1);

  changed = 0;
  possibilities = bit_count[possible_index_set];

  // Initialise the reserve_number_set with the current number, then add the others in the following loop
  reserve_number_set = NUMBER_TO_SET(number);

  same_index_set_count = 0;
  for (i=0; i<(number-1); i++) {
    if (prior_possible_index_set[i] == possible_index_set) {
      same_index_set_count++;
      // The Number is i+1 here - yes, weird!
      reserve_number_set |= NUMBER_TO_SET(i+1);
    }
  }

  // Do we have other Numbers with the same possibilies?
  // Reserve these possibilities - include current number in this loop
  if ((same_index_set_count+1) == possibilities)
    changed += reserve_with_index_set_func(cell, possible_index_set, reserve_number_set);

  if (possibilities <= 3) {
    // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
    for (i=0; i<(number-1); i++) {
      // Is there any overlap with a previos set
      if (prior_possible_index_set[i] & possible_index_set) {
        // We have overlap - does this overlap takes us to 3
        joint_index_set = prior_possible_index_set[i] | possible_index_set;
        if (bit_count[joint_index_set] == 3) {
          // Look for a third that is within this set
          for (j=(i+1); j<(number-1); j++) {
            if (prior_possible_index_set[j] && ((joint_index_set | prior_possible_index_set[j]) == joint_index_set)) {
              // Now we have the three numbers (i+1, j+1, number) that go into joint_index_set
              reserve_number_set = NUMBER_TO_SET(i+1) | NUMBER_TO_SET(j+1) | NUMBER_TO_SET(number);
              if (cell->board_ref->debug_level >= 4) {
                printf(DINDENT "%s: Reserving cells around [%i,%i] with possibilities: %i index_set: ",
                        parent_func_name, cell->row, cell->col, possibilities);
                print_index_set(joint_index_set, "number_set: ");
                print_number_set(reserve_number_set, "\n");
              }
              changed += reserve_with_index_set_func(cell, joint_index_set, reserve_number_set);
              break;
            }
          }
        }
      }
    }
  }

  return changed;
}


static
int solve_eliminate_tiles_by_number(struct sudoku_board *board)
{
  struct sudoku_cell *cell, *possible_cell;
  unsigned int tile, index, tile_set, index_set, possibilities, i;
  unsigned int number, number_set, remaining_number_set, possible_index_set;
  unsigned int prior_possible_index_set[9];
  int changed;

  if (board->debug_level >= 2)
    printf("Solve eliminate tiles\n");

  changed = 0;
  tile_set = board->tile_empty_set;
  while (tile_set) {
    tile = get_next_index_from_set(&tile_set);
    if (board->debug_level >= 2)
      printf("  Tile %i\n", tile);

    zero_array_9(prior_possible_index_set);
    remaining_number_set = (~board->tile_number_taken_set[tile] & NUMBER_SET_MASK);
    while (remaining_number_set) {
      number = get_next_index_from_set(&remaining_number_set);
      number_set = NUMBER_TO_SET(number);
      if (board->debug_level >= 2)
        printf("    Number %i\n", number);

      // Check if number is already taken in this tile. If so, skip the number!
      assert(!(board->tile_number_taken_set[tile] & number_set));
      possibilities = 0;
      possible_cell = 0;
      possible_index_set = 0;

      // Go through all possible positions for Number
      index_set = board->tile_cell_empty_set[tile];
      while (index_set) {
        index = get_next_index_from_set(&index_set);
        cell = board->tile_ref[tile][index];
        assert(cell->number == 0);
        // The cell is empty - can we put Number in this cell?
        if (get_cell_possible_number_set(cell) & number_set) {
          possibilities++;
          possible_cell = cell;
          possible_index_set |= NUMBER_TO_SET(index);

          if (board->debug_level >= 4) {
            printf(DINDENT "Possible [%i,%i] avail_set: ", cell->row, cell->col);
            print_number_set(get_cell_possible_number_set(cell), "<> ");
            print_number_set(number_set, "");
            printf("cell_number: %i\n", cell->number);
          }
        }
      }

      if (board->debug_level >= 4) {
        printf(DINDENT "Tile: %i, Number: %i, possibilities: %i, possible_index_set: ", tile, number, possibilities);
        print_index_set(possible_index_set, "\n");
      }

      // Do we have any possibilities
      if (possibilities == 0) {
        // Board is dead
        set_board_dead(board, __func__);
        return 0;
      } else if (possibilities == 1) {
        // We have one and only one possible - set it!
        set_cell_number_and_log(possible_cell, number);
        changed++;
      } else {
        // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
        prior_possible_index_set[number-1] = possible_index_set;
        changed += find_and_reserve_group_with_number(possible_cell, prior_possible_index_set, 
                                                      number, possible_index_set, 
                                                      &reserve_cells_with_index_in_tile,
                                                      __func__);

        if (possibilities <= 3) {
          // If all possibilities (=2 or 3) on the same row, if so have the row reserve them
          for (i=0; i<3; i++) {
            if ((possible_index_set | index_row_mask[i]) == index_row_mask[i])
              changed += reserve_tile_in_row(possible_cell, NUMBER_TO_SET(number));
          }

          // If all possibilities (=2 or 3) on the same col, if so have the col reserve them
          for (i=0; i<3; i++) {
            if ((possible_index_set | index_col_mask[i]) == index_col_mask[i])
              changed += reserve_tile_in_col(possible_cell, NUMBER_TO_SET(number));
          }
        }
      }
    }

    if (is_board_dirty(board))
      changed += propagate_constraints(board);
  }

  return changed;
}

static
int solve_eliminate_rows_by_number(struct sudoku_board *board)
{
  struct sudoku_cell *cell, *possible_cell;
  unsigned int row, col, row_set, col_set, possibilities, i;  
  unsigned int number, number_set, remaining_number_set, possible_index_set;
  unsigned int prior_possible_index_set[9];
  int changed;

  if (board->debug_level >= 2)
    printf("Solve eliminate rows\n");

  changed = 0;
  row_set = board->row_empty_set;
  while (row_set) {
    row = get_next_index_from_set(&row_set);
    if (board->debug_level >= 2)
      printf("  Row %i\n", row);

    zero_array_9(prior_possible_index_set);
    remaining_number_set = (~board->row_number_taken_set[row] & NUMBER_SET_MASK);
    while (remaining_number_set) {
      number = get_next_index_from_set(&remaining_number_set);
      number_set = NUMBER_TO_SET(number);
      if (board->debug_level >= 2)
        printf("    Number %i\n", number);

      // Check if number is already taken in this row.
      assert(!(board->row_number_taken_set[row] & number_set));
      possibilities = 0;
      possible_cell = 0;
      possible_index_set = 0;

      // Go through all possible positions for Number
      col_set = board->row_cell_empty_set[row];
      while (col_set) {
        col = get_next_index_from_set(&col_set);
        cell = &board->cells[row][col];
        assert(cell->number == 0);
        // Is this cell free and can we put Number in this cell?
        if (get_cell_possible_number_set(cell) & number_set) {
          possibilities++;
          possible_cell = cell;
          possible_index_set |= NUMBER_TO_SET(col);

          if (board->debug_level >= 4) {
            printf(DINDENT "Possible [%i,%i] avail_set: ", cell->row, cell->col);
            print_number_set(get_cell_possible_number_set(cell), "<> ");
            print_number_set(number_set, "");
            printf("cell_number: %i\n", cell->number);
          }
        }
      }

      // Do we have any possibilities
      if (possibilities == 0) {
        // Board is dead
        set_board_dead(board, __func__);
        return 0;
      } else if (possibilities == 1) {
        // We have one and only one possible - set it!
        set_cell_number_and_log(possible_cell, number);
        changed++;
      } else {
        // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
        prior_possible_index_set[number-1] = possible_index_set;
        changed += find_and_reserve_group_with_number(possible_cell, prior_possible_index_set, 
                                                      number, possible_index_set, 
                                                      &reserve_cells_with_index_in_row,
                                                      __func__);

        if (possibilities <= 3) {
          // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
          for (i=0; i<3; i++) {
            if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i])
              changed += reserve_row_in_tile(possible_cell, NUMBER_TO_SET(number));
          }
        }
      }
    }

    if (is_board_dirty(board))
      changed += propagate_constraints(board);
  }

  return changed;
}


static
int solve_eliminate_cols_by_number(struct sudoku_board *board)
{
  struct sudoku_cell *cell, *possible_cell;
  unsigned int row, col, row_set, col_set, possibilities, i;
  unsigned int number, number_set, remaining_number_set, possible_index_set;
  unsigned int prior_possible_index_set[9];
  int changed;

  if (board->debug_level >= 2)
    printf("Solve eliminate cols\n");

  changed = 0;
  col_set = board->col_empty_set;
  while (col_set) {
    col = get_next_index_from_set(&col_set);
    if (board->debug_level >= 2)
      printf("  Col %i\n", col);

    zero_array_9(prior_possible_index_set);
    remaining_number_set = (~board->col_number_taken_set[col] & NUMBER_SET_MASK);
    while (remaining_number_set) {
      number = get_next_index_from_set(&remaining_number_set);
      number_set = NUMBER_TO_SET(number);
      if (board->debug_level >= 2)
        printf("    Number %i\n", number);

      // Check if number is already taken in this column. If so, skip the number!
      assert(!(board->col_number_taken_set[col] & number_set));
      possibilities = 0;
      possible_cell = 0;
      possible_index_set = 0;

      // Go through all possible positions for Number
      row_set = board->col_cell_empty_set[col];
      while (row_set) {
        row = get_next_index_from_set(&row_set);
        cell = &board->cells[row][col];
        assert(cell->number == 0);
        // This cell is free - can we put Number in this cell?
        if (get_cell_possible_number_set(cell) & number_set) {
          possibilities++;
          possible_cell = cell;
          possible_index_set |= NUMBER_TO_SET(row);

          if (board->debug_level >= 4) {
            printf(DINDENT "Possible [%i,%i] avail_set: ", cell->row, cell->col);
            print_number_set(get_cell_possible_number_set(cell), "<> ");
            print_number_set(number_set, "");
            printf("cell_number: %i\n", cell->number);
          }
        }
      }

      // Do we have any possibilities
      if (possibilities == 0) {
        // Board is dead
        set_board_dead(board, __func__);
        return 0;
      } else if (possibilities == 1) {
        // We have one and only one possible - set it!
        set_cell_number_and_log(possible_cell, number);
        changed++;
      } else {
        // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
        prior_possible_index_set[number-1] = possible_index_set;
        changed += find_and_reserve_group_with_number(possible_cell, prior_possible_index_set, 
                                                      number, possible_index_set, 
                                                      &reserve_cells_with_index_in_col,
                                                      __func__);

        if (bit_count[possible_index_set] <= 3) {
          // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
          for (i=0; i<3; i++) {
            if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i]) 
              changed += reserve_col_in_tile(possible_cell, NUMBER_TO_SET(number));
          }
        }
      }
    }

    if (is_board_dirty(board))
      changed += propagate_constraints(board);
  }

  return changed;
}


// === Looking at an index and see what numbers that are available to stuff into it ===
//
// For each index (row, col, or tile index) and its corresponding Possible Number Set (prior_possible_number_set[9]):
//   Find indices that can be grouped together that share the same possible numbers.
static inline
int find_and_reserve_group_with_index(struct sudoku_cell *cell, 
                                      unsigned int prior_possible_number_set[9], int this_index, 
                                      unsigned int possible_number_set, 
                                      reserve_with_index_set_func_t reserve_with_index_set_func,
                                      const char *parent_func_name)
{
  int i, j, changed, possibilities, same_number_set_count;
  unsigned int possible_index_set, joint_number_set;

  changed = 0;
  possibilities = bit_count[possible_number_set];
  assert(possibilities > 1);

  same_number_set_count = 0;
  possible_index_set = INDEX_TO_SET(this_index);
  for (i=0; i<this_index; i++) {
    if (prior_possible_number_set[i] == possible_number_set) {
      same_number_set_count++;
      possible_index_set |= INDEX_TO_SET(i);
    }
  }

  // Do we have other cells with the same possibilies
  // Reserve these possibilities - include current number in this loop
  if ((same_number_set_count+1) == possibilities) 
    changed += reserve_with_index_set_func(cell, possible_index_set, possible_number_set);

  if (possibilities <= 3) {
    // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
    for (i=0; i<this_index; i++) {
      // Is there any overlap with a previos set
      if (prior_possible_number_set[i] & possible_number_set) {
        // We have overlap - does this overlap takes us to 3
        joint_number_set = prior_possible_number_set[i] | possible_number_set;
        if (bit_count[joint_number_set] == 3) {
          // Look for a third that is within this set
          for (j=(i+1); j<this_index; j++) {
            if (prior_possible_number_set[j] && ((joint_number_set | prior_possible_number_set[j]) == joint_number_set)) {
              // Now we have the three indexes (i, j, row) that go into possible_index_set
              possible_index_set = INDEX_TO_SET(i) | INDEX_TO_SET(j) | INDEX_TO_SET(this_index);
              if (cell->board_ref->debug_level >= 4) {
                printf(DINDENT "%s: Reserving cells around [%i,%i] with possibilities: %i index_set: ",
                        parent_func_name, cell->row, cell->col, possibilities);
                print_index_set(possible_index_set, "number_set: ");
                print_number_set(joint_number_set, "\n");
              }
              changed += reserve_with_index_set_func(cell, possible_index_set, joint_number_set);
              break;
            }
          }
        }
      }
    }
  }

  return changed;
}


static
int solve_eliminate_tiles_by_index(struct sudoku_board *board)
{
  struct sudoku_cell *cell;
  unsigned int tile, tile_set, index, index_set, possibilities, possible_number_set;
  unsigned int prior_possible_number_set[9];
  int changed;

  if (board->debug_level >= 2)
    printf("Solve eliminate tiles 2\n");

  changed = 0;
  tile_set = board->tile_empty_set;
  while (tile_set) {
    tile = get_next_index_from_set(&tile_set);
    if (board->debug_level >= 2) {
      printf("  Tile %i\n", tile);
      if (board->debug_level >= 4)
        print_possible(board, DINDENT);
    }

    zero_array_9(prior_possible_number_set);
    index_set = board->tile_cell_empty_set[tile];
    while (index_set) {
      index = get_next_index_from_set(&index_set);
      if (board->debug_level >= 2)
        printf("    Index %i\n", index);
      cell = board->tile_ref[tile][index];
      assert(cell->number == 0);
      possible_number_set = get_cell_possible_number_set(cell);
      possibilities = bit_count[possible_number_set];

      // Do we have any possibilities
      if (possibilities == 0) {
        // Board is dead
        set_board_dead(board, __func__);
        return 0;
      } else if (possibilities == 1) {
        // We have one and only one possible - set it!
        set_cell_number_and_log(cell, number_set_to_number[possible_number_set]);
        changed++;
      } else {
        // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
        prior_possible_number_set[index] = possible_number_set;

        changed += find_and_reserve_group_with_index(cell, prior_possible_number_set, 
                                                     index, possible_number_set, 
                                                     &reserve_cells_with_index_in_tile,
                                                     __func__);
      }
    }

    if (is_board_dirty(board))
      changed += propagate_constraints(board);
  }

  return changed;
}


static
int solve_eliminate_rows_by_index(struct sudoku_board *board)
{
  struct sudoku_cell *cell;
  unsigned int row, col, row_set, col_set, possibilities, possible_number_set;
  unsigned int prior_possible_number_set[9];
  int changed;

  if (board->debug_level >= 2)
    printf("Solve eliminate rows 2\n");

  changed = 0;
  row_set = board->row_empty_set;
  while (row_set) {
    row = get_next_index_from_set(&row_set);
    if (board->debug_level >= 2) {
      printf("  Row %i\n", row);
      if (board->debug_level >= 4)
        print_possible(board, DINDENT);
    }

    zero_array_9(prior_possible_number_set);
    col_set = board->row_cell_empty_set[row];
    while (col_set) {
      col = get_next_index_from_set(&col_set);
      if (board->debug_level >= 2)
        printf("    Col %i\n", col);
      prior_possible_number_set[col] = 0;
      cell = &board->cells[row][col];
      assert(cell->number == 0); 
      possible_number_set = get_cell_possible_number_set(cell);
      possibilities = bit_count[possible_number_set];

      // Do we have any possibilities
      if (possibilities == 0) {
        // Board is dead
        set_board_dead(board, __func__);
        return 0;
      } else if (possibilities == 1) {
        // We have one and only one possible - set it!
        set_cell_number_and_log(cell, number_set_to_number[possible_number_set]);
        changed++;
      } else {
        // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
        prior_possible_number_set[col] = possible_number_set;

        changed += find_and_reserve_group_with_index(cell, prior_possible_number_set, 
                                                     col, possible_number_set, 
                                                     &reserve_cells_with_index_in_row,
                                                     __func__);
      }
    }

    if (is_board_dirty(board))
      changed += propagate_constraints(board);
  }

  return changed;
}


static
int solve_eliminate_cols_by_index(struct sudoku_board *board)
{
  struct sudoku_cell *cell;
  unsigned row, col, row_set, col_set, possibilities, possible_number_set;
  unsigned int prior_possible_number_set[9];
  int changed;

  if (board->debug_level >= 2)
    printf("Solve eliminate cols 2\n");

  changed = 0;
  col_set = board->col_empty_set;
  while (col_set) {
    col = get_next_index_from_set(&col_set);
    if (board->debug_level >= 2) {
      printf("  Col %i\n", col);
      if (board->debug_level >= 4)
        print_possible(board, DINDENT);
    }

    zero_array_9(prior_possible_number_set);
    row_set = board->col_cell_empty_set[col];
    while (row_set) {
      row = get_next_index_from_set(&row_set);
      if (board->debug_level >= 2)
        printf("    Row %i\n", row);
      prior_possible_number_set[row] = 0;
      cell = &board->cells[row][col];
      assert(cell->number == 0);
      possible_number_set = get_cell_possible_number_set(cell);
      possibilities = bit_count[possible_number_set];

      // Do we have any possibilities
      if (possibilities == 0) {
        // Board is dead
        set_board_dead(board, __func__);
        return 0;
      } else if (possibilities == 1) {
        // We have one and only one possible - set it!
        set_cell_number_and_log(cell, number_set_to_number[possible_number_set]);
        changed++;
      } else {
        // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
        prior_possible_number_set[row] = possible_number_set;

        changed += find_and_reserve_group_with_index(cell, prior_possible_number_set, 
                                                     row, possible_number_set, 
                                                     &reserve_cells_with_index_in_col,
                                                     __func__);
      }
    }

    if (is_board_dirty(board))
      changed += propagate_constraints(board);
  }

  return changed;
}


int solve_eliminate(struct sudoku_board *board)
{
  int this_changed, changed, total_changed, round, i;
  int (*const solve_func_arr[])(struct sudoku_board*) = {
    solve_eliminate_tiles_by_index,
    solve_eliminate_tiles_by_number,
    solve_eliminate_rows_by_number,
    solve_eliminate_cols_by_number,
    solve_eliminate_rows_by_index,
    solve_eliminate_cols_by_index
  }; 

  if (board->debug_level >= 2)
    printf("Solve eliminate\n");

  total_changed = 0;
  round = 0;

  do {
    if (board->debug_level >= 2)
      printf("  Round %i\n", round++);
    changed = 0;

    for(i=0; i<(sizeof(solve_func_arr)/sizeof(solve_func_arr[0])); i++) {
      this_changed = solve_func_arr[i](board);
      if (this_changed) {
        changed += this_changed;
        if (is_board_done(board))
          break;
      }
    }

    total_changed += changed;
  } while ((changed > 0) && (!is_board_done(board)));

  return total_changed;
}


static inline
void analyze_tile_interlock_rectangle_helper(struct sudoku_board *board, struct sudoku_cell *cell1,
                                             unsigned int set1, unsigned int set2,
                                             unsigned int set3, unsigned int set4, int *changed) 
{
  unsigned int common_set;

  common_set = (set3 & set4);
  
  if ((bit_count[set3] == 2) && (bit_count[set4] == 2) && (bit_count[common_set] == 1)) {
    if ((set1 & common_set) && (((set3|set4) & ~common_set) == set2)) {
     // Remove common_set from cell1
     (*changed) += reserve_cell_and_log(cell1, (set1 & ~common_set), "analyze_tile_interlock_rectangle");
    }
  }
}


static inline
int analyze_tile_interlock_rectangle(struct sudoku_cell *cell1, struct sudoku_cell *cell2, 
                                     struct sudoku_cell *cell3, struct sudoku_cell *cell4)
{
  int changed;
  unsigned int set1, set2, set3, set4, common_set12, common_set34;
  struct sudoku_board *board;

  changed = 0;
  board = cell1->board_ref;
  set1 = get_cell_possible_number_set(cell1);
  set2 = get_cell_possible_number_set(cell2);
  set3 = get_cell_possible_number_set(cell3);
  set4 = get_cell_possible_number_set(cell4);

  if (board->debug_level >= 4) {
    printf(DINDENT "set1: "); print_number_set(set1, "\n");
    printf(DINDENT "set2: "); print_number_set(set2, "\n");
    printf(DINDENT "set3: "); print_number_set(set3, "\n");
    printf(DINDENT "set4: "); print_number_set(set4, "\n");
    common_set12 = (set1 & set2);
    common_set34 = (set3 & set4);
    printf(DINDENT "common_set12: "); print_number_set(common_set12, "\n");
    printf(DINDENT "common_set34: "); print_number_set(common_set34, "\n");
  }

  analyze_tile_interlock_rectangle_helper(board, cell1, set1, set2, set3, set4, &changed);
  analyze_tile_interlock_rectangle_helper(board, cell2, set2, set1, set3, set4, &changed);
  analyze_tile_interlock_rectangle_helper(board, cell3, set3, set4, set1, set2, &changed);
  analyze_tile_interlock_rectangle_helper(board, cell4, set4, set3, set1, set2, &changed);

  return changed;
}


static
int solve_tile_interlock_rectangle(struct sudoku_board *board)
{
  unsigned int row1, col1, row2, col2, row1_set, row2_set, col1_set, col2_set;
  struct sudoku_cell *cell1, *cell2, *cell3, *cell4;
  const unsigned int above_mask[9] = { 0b111111000, 0b111111000, 0b111111000,
                                       0b111000000, 0b111000000, 0b111000000, 
                                       0b000000000, 0b000000000, 0b000000000 }; 
  int changed;

  if (board->debug_level >= 2)
    printf("Solve tile interlock rectangle\n");

  changed = 0;

  // Loop over row 0 to 5 with empty cells
  row1_set = board->row_empty_set & 0b000111111; 
  while (row1_set) {
    row1 = get_next_index_from_set(&row1_set);
    if (board->debug_level >= 2)
      printf("  Row %i\n", row1);

    // Loop over col 0 to 5 with empty cells
    col1_set = board->row_cell_empty_set[row1]  & 0b000111111;
    while (col1_set) {
      col1 = get_next_index_from_set(&col1_set);
      if (board->debug_level >= 2)
        printf("    Col %i\n", col1);

      cell1 = &board->cells[row1][col1];
      assert(cell1->number == 0);

      // Loop over rows in higher tiles with empty cells
      row2_set = board->row_empty_set & above_mask[row1]; 
      while (row2_set) {
        row2 = get_next_index_from_set(&row2_set);

        // Loop over cols in higher tiles with empty cells
        col2_set = board->row_cell_empty_set[row2]  & above_mask[col1];
        while (col2_set) {
          col2 = get_next_index_from_set(&col2_set);
          cell2 = &board->cells[row2][col2]; 
          assert(cell2->number == 0);

          if ((board->cells[row1][col2].number == 0) &&
              (board->cells[row2][col1].number == 0)) {
            // We have found a rectangle of empty cells
            if (board->debug_level >= 4)
              printf(DINDENT "Found inter-tile rectangle: [%i,%i]-[%i,%i]\n", row1, col1, row2, col2);
            cell3 = &board->cells[row2][col1];
            cell4 = &board->cells[row1][col2];
            changed += analyze_tile_interlock_rectangle(cell1, cell2, cell3, cell4);
          }
        }
      }
    }
  }

  return changed;
}


static
int solve_tile_interlock(struct sudoku_board *board)
{
  int changed, total_changed, round;

  if (board->debug_level >= 2)
    printf("Solve tile interlock\n");

  total_changed = 0;
  round = 0;

  do {
    if (board->debug_level >= 2)
      printf("  Round %i\n", round);
    round++;

    changed = solve_tile_interlock_rectangle(board);
    total_changed += changed;
    if (is_board_done(board))
      break;
    if (changed) {
      total_changed += propagate_constraints(board);
      if (is_board_done(board))
        break;
      total_changed += solve_eliminate(board);
      if (is_board_done(board))
        break;
    }
 } while (changed && !is_board_done(board));

  return total_changed;
}


static inline
void solve_hidden_cell(struct sudoku_cell *cell)
{
  unsigned int number, number_set;
  struct sudoku_board *board;
  struct sudoku_board *future_board;

  board = cell->board_ref;
  number_set = get_cell_possible_number_set(cell);
  while (number_set) {
    number = get_next_index_from_set(&number_set);
    if (board->debug_level)
      printf("Trying solution [%i,%i] = %i  (level: %i)\n", cell->row, cell->col, number, board->nest_level);

    future_board = dupilcate_board(board);
    future_board->nest_level++;
    if (board->debug_level < 3)
      future_board->debug_level = 0;
    set_cell_number(&future_board->cells[cell->row][cell->col], number);
    solve(future_board);

    if (future_board->undetermined_count == 0) {
      // Add to list of solutions
      if (board->debug_level >= 1)
        printf("Found hidden solution [%i,%i] = %i\n", cell->row, cell->col, number);
      assert(future_board->solutions_list == NULL);
      assert(future_board->solutions_count == 0);
      add_to_board_solutions_list(board, future_board);
      if (is_board_solved(board))
        return;
    } else if (future_board->solutions_list) {
      // Add to list of solutions
      if (board->debug_level >= 1)
        printf("Found hidden solution [%i,%i] = %i\n", cell->row, cell->col, number);
      add_list_to_board_solutions_list(board, future_board->solutions_list);
      future_board->solutions_list = NULL;
      future_board->solutions_count = 0;
      destroy_board(&future_board);
      if (is_board_solved(board))
        return;
    } else {
      destroy_board(&future_board);
    }
  }
}


static
void solve_hidden(struct sudoku_board *board)
{
  struct sudoku_cell *cell;
  struct sudoku_board *tmp;

  if (board->debug_level >= 2) {
    printf("Solve hidden\n");
    print_board(board);
    print_possible(board, NULL);
  }

  // Is the board good to go to another nest level?
  cell = find_cell_with_lowest_availability_count(board);
  if (cell) {
    solve_hidden_cell(cell);

    // Fix the special case with one-and-only-one solution found
    if ((board->nest_level == 0) && (board->solutions_count == 1)) {
      tmp = board->solutions_list;
      assert(tmp->next == NULL);
      tmp->next = NULL;
      board->solutions_list = NULL;
      board->solutions_count = 0;
      tmp->nest_level = board->nest_level;
      copy_board(tmp, board);
      destroy_board(&tmp);
    }
  }
}


int solve(struct sudoku_board *board)
{
  int solutions_count;

  solve_possible(board);

  if (!is_board_done(board))
    solve_eliminate(board);

  if (!is_board_done(board))
    solve_tile_interlock(board);   

  if (board->guessing_allowed) {
    if (!is_board_done(board)) 
      solve_hidden(board);
  }

  solutions_count = board->solutions_count;
  if (board->undetermined_count == 0)
    solutions_count++;

  return solutions_count;
}


static
void print_index_set(unsigned int index_set, const char *postfix)
{
  int index;

  printf("{");
  for (index=0; index<=8; index++) {
    if (index_set & INDEX_TO_SET(index))
      printf(" %i", index);
  }
  if (postfix)
    printf(" } %s", postfix);
  else
    printf(" }");
}


static
void print_number_set(unsigned int number_set, const char *postfix)
{
  int number;

  printf("{");
  for (number=1; number<=9; number++) {
    if (number_set & NUMBER_TO_SET(number))
      printf(" %i", number);
  }
  if (postfix)
    printf(" } %s", postfix);
  else
    printf(" }");
}


static
void print_reserved_set_for_cell(struct sudoku_cell *cell)
{
  if (cell->board_ref->debug_level > 1)
    printf("      ");  
  printf("[%i,%i]  =  ", cell->row, cell->col);
  print_number_set(cell->reserved_for_number_set, "\n");
}


static
void print_possible(struct sudoku_board *board, const char *prefix)
{
  int row, col, i;
  struct sudoku_cell *cell;
  unsigned int possible_set, taken_set, available_set, reserved_set;

  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      cell = &board->cells[row][col];

      if (cell->number == 0) {

        if (prefix)
          printf("%s", prefix);
        printf("[%i,%i] Possible: ", row, col);
        possible_set = get_cell_possible_number_set(cell) >> 1;
        for (i=0; i<9; i++) {
          if (possible_set & 1)
            printf("%i ", i+1);
          possible_set >>= 1;
        }

        taken_set = *cell->row_number_taken_set_ref;
        taken_set |= *cell->col_number_taken_set_ref;
        taken_set |= *cell->tile_number_taken_set_ref;

        available_set = NUMBER_TAKEN_TO_AVAILABLE_SET(taken_set) >> 1;

        printf("   (available:");
        for (i=0; i<9; i++) {
          if (available_set & 1)
            printf(" %i", i+1);
          available_set >>= 1;
        }

        reserved_set = cell->reserved_for_number_set >> 1;
        if (reserved_set) {
          printf("  reserved:");
          for (i=0; i<9; i++) {
            if (reserved_set & 1)
              printf(" %i", i+1);
            reserved_set >>= 1;
          }
        }
        printf(")\n");
      }
    }
  }
}


void print_solutions(struct sudoku_board *board)
{
  struct sudoku_board *current;

  if (board->solutions_count == 0) {
    print_board(board);
    if (board->undetermined_count)
      print_possible(board, NULL);
  } else {
    printf("Number of solutions: %i\n", board->solutions_count);
    current = board->solutions_list;
    while (current) {
      print_board(current);
      current = current->next;
      if (current)
        printf("\n\n");
    }
  }
}


int read_board(struct sudoku_board *board, const char *str)
{
  int row, col, result;
  char ch;
  unsigned int number;

  assert(str);

  row = 0;
  col = 0;
  result = 0;

  while ((ch = *str++)) {
    if ((ch == '.') || (ch == '?'))
      ch = '0';

    if  ((ch >= '0') && (ch <= '9')) {
      number = (ch - '0');

      if (number) {
        if (get_cell_possible_number_set(&board->cells[row][col]) & NUMBER_TO_SET(number)) {
          set_cell_number(&board->cells[row][col], number);
        } else {
          result = 1; // Invalid input

          if (board->debug_level)
            printf("[%i,%i] = %i - Invalid assignment, ingoring it\n", row, col, number);
        }
      }

      if (++col > 8) {
        col = 0;
        if (++row > 8)
          break;
      }
    }
  }

  return result;
}
