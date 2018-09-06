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

#define DINDENT "      "

#define MAX_WALK_HOP_COUNT 7

static unsigned int available_set_to_number[NUMBER_TO_SET(10)+1];
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

static const unsigned int row_to_tile_mask[] = {
  0x007, 0x007, 0x007, // 000 000 111
  0x038, 0x038, 0x038, // 000 111 000
  0x1C0, 0x1C0, 0x1C0  // 111 000 000
};

static const unsigned int col_to_tile_mask[] = {
  0x007, 0x007, 0x007, // 000 000 111
  0x038, 0x038, 0x038, // 000 111 000
  0x1C0, 0x1C0, 0x1C0  // 111 000 000
};

static void print_number_set(unsigned int number_set, const char *postfix);

static void print_reserved_set_for_cell(struct sudoku_cell *cell);

static void print_possible(struct sudoku_board *board, const char *prefix);

int solve(struct sudoku_board *board);


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
    available_set_to_number[i] = 0;

  for (i=1; i<=9; i++)
    available_set_to_number[NUMBER_TO_SET(i)] = i;

  for (i=0; i<NUMBER_TO_SET(10); i++) {
    bit_count[i] = bit_count_func(i);
    set_to_index[i].remaining_set = i;
    set_to_index[i].index = set_to_index_func(&(set_to_index[i].remaining_set));    
  }
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
void set_board_dead(struct sudoku_board *board)
{
  if (board->debug_level)
    printf("Board is declared dead!\n");

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

  return possible_number_set;
}


static inline
unsigned int get_cell_available_number(struct sudoku_cell *cell)
{
  return available_set_to_number[get_cell_possible_number_set(cell)];
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
          set_board_dead(board);
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
  unsigned int number_set;

  if (cell->number == 0)
    cell->board_ref->undetermined_count--;

  assert(cell->number == 0);
  assert(number);

  cell->number = number;
  cell->reserved_for_number_set = 0;

  number_set = NUMBER_TO_SET(number);
  *cell->row_number_taken_set_ref |= number_set;
  *cell->col_number_taken_set_ref |= number_set;
  *cell->tile_number_taken_set_ref |= number_set;

  mark_cell_not_empty(cell);
  mark_cell_dirty(cell);
}


static inline
void set_cell_number_and_log(struct sudoku_cell *cell, unsigned int number)
{
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
    print_number_set(available_set, "\n");
    assert(0);
  }
  set_board_dead(cell->board_ref);
}


typedef int (*reserve_func_t)(struct sudoku_cell *possible_cell, 
                              unsigned int number_set);
typedef int (*reserve_with_index_set_func_t)(struct sudoku_cell *possible_cell, 
                                             unsigned int possible_index_set, 
                                             unsigned int number_set);


static inline
int reserve_cell(struct sudoku_cell *cell, unsigned int number_set)
{
  unsigned int taken_set, available_set, new_reserved_for_number_set;
  int changed;

  taken_set = *cell->row_number_taken_set_ref;
  taken_set |= *cell->col_number_taken_set_ref;
  taken_set |= *cell->tile_number_taken_set_ref;

  available_set = NUMBER_TAKEN_TO_AVAILABLE_SET(taken_set);

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
      mark_cell_dirty(cell);
      changed = 1;
    }
  } else {
    cell->reserved_for_number_set = number_set;
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
    print_number_set(number_set, "\n");
  }

  changed = reserve_cell(cell, number_set);
  if (changed && (board->debug_level >= 1))
    print_reserved_set_for_cell(cell);  

  return changed;
}

static inline
int reserve_row_in_tile(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  int row, col, my_row, my_tile, changed;
  unsigned int reserve_number_set, available_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;

  changed = 0;
  my_row = possible_cell->row;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;
  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      cell = &board->cells[row][col];
      if ((cell->tile == my_tile) && (cell->number == 0)) {
        available_set = get_cell_possible_number_set(cell);
        if (available_set & number_set) {
          // The number is a possibility for this cell
          if (cell->row == my_row) {
            // This is my tile and my row so include the number in the reservation
            reserve_number_set = available_set;
          } else {
            // This is my tile but not my row so exclude the number in the reservation
            reserve_number_set = available_set & (~number_set);
          }

          // Make the reservation
          changed += reserve_cell_and_log(cell, reserve_number_set, "reserve_row_in_tile");
        }
      }
    }
  }

  return changed;
}


static inline
int reserve_col_in_tile(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  int row, col, my_col, my_tile, changed;
  unsigned int reserve_number_set, available_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;

  changed = 0;
  my_col = possible_cell->col;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;
  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      cell = &board->cells[row][col];
      if ((cell->tile == my_tile) && (cell->number == 0)) {
        available_set = get_cell_possible_number_set(cell);
        if (available_set & number_set) {
          // The number is a possibility for this cell
          if (cell->col == my_col) {
            // This is my tile and my row so include the number in the reservation
            reserve_number_set = available_set;
          } else {
            // This is my tile but not my row so exclude the number in the reservation
            reserve_number_set = available_set & (~number_set);
          }

          // Make the reservation
          changed += reserve_cell_and_log(cell, reserve_number_set, "reserve_col_in_tile");
        }
      }
    }
  }

  return changed;
}


static inline
int reserve_tile_in_row(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  int my_row, col, my_tile, changed;
  unsigned int reserve_number_set, available_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;

  changed = 0;
  my_row = possible_cell->row;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;
  for (col=0; col<9; col++) {
    cell = &board->cells[my_row][col];
    if (cell->number == 0) {
      available_set = get_cell_possible_number_set(cell);
      if (available_set & number_set) {
        // The number is a possibility for this cell
        if (cell->tile == my_tile) {
          // This is my tile and my row so include the number in the reservation
          reserve_number_set = available_set;
        } else {
          // This is my tile but not my row so exclude the number in the reservation
          reserve_number_set = available_set & (~number_set);
        }

        // Make the reservation
        changed += reserve_cell_and_log(cell, reserve_number_set, "reserve_tile_in_row");
      }
    }
  }

  return changed;
}


static inline
int reserve_tile_in_col(struct sudoku_cell *possible_cell, unsigned int number_set)
{
  int row, my_col, my_tile, changed;
  unsigned int reserve_number_set, available_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;

  changed = 0;
  my_col = possible_cell->col;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;
  for (row=0; row<9; row++) {
    cell = &board->cells[row][my_col];
    if (cell->number == 0) {
      available_set = get_cell_possible_number_set(cell);
      if (available_set & number_set) {
        // The number is a possibility for this cell
        if (cell->tile == my_tile) {
          // This is my tile and my row so include the number in the reservation
          reserve_number_set = available_set;
        } else {
          // This is my tile but not my row so exclude the number in the reservation
          reserve_number_set = available_set & (~number_set);
        }

        // Make the reservation
        changed += reserve_cell_and_log(cell, reserve_number_set, "reserve_tile_in_col");
      }
    }
  }

  return changed;
}


static inline
int reserve_cells_with_index_in_tile(struct sudoku_cell *possible_cell, unsigned int possible_index_set, unsigned int number_set)
{
  int my_tile, i, changed;
  unsigned int reserve_number_set, available_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;

  changed = 0;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;
  for (i=0; i<9; i++) {
    cell = board->tile_ref[my_tile][i];
    if (cell->number == 0) {
      available_set = get_cell_possible_number_set(cell);
      if (available_set & number_set) {
        if (INDEX_TO_SET(i) & possible_index_set) 
          reserve_number_set = available_set;
        else 
          reserve_number_set = available_set & (~number_set);
        
       changed += reserve_cell_and_log(cell, reserve_number_set, "reserve_cells_with_index_in_tile");
      }
    }
  }

  // If all possibilities (=2 or 3) on the same row, if so have the row reserve them
  for (i=0; i<3; i++) {
    if ((possible_index_set | index_row_mask[i]) == index_row_mask[i]) {
      if (reserve_tile_in_row(possible_cell, number_set)) {
        changed++;
      }
    }
  }

  // If all possibilities (=2 or 3) on the same col, if so have the col reserve them
  for (i=0; i<3; i++) {
    if ((possible_index_set | index_col_mask[i]) == index_col_mask[i]) {
      if (reserve_tile_in_col(possible_cell, number_set)) {
        changed++;
      }
    }
  }

  return changed;
}


static inline
int reserve_cells_with_index_in_row(struct sudoku_cell *possible_cell, unsigned int possible_index_set, unsigned int number_set)
{
  int my_row, col, changed, i;
  unsigned int reserve_number_set, available_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;

  changed = 0;
  my_row = possible_cell->row;
  board = possible_cell->board_ref;
  for (col=0; col<9; col++) {
    cell = &board->cells[my_row][col];
    if (cell->number == 0) {
      available_set = get_cell_possible_number_set(cell);
      if (available_set & number_set) {
        // The number is a possibility for this cell
        if (INDEX_TO_SET(col) & possible_index_set) {
          // This is cell so include the number in the reservation
          reserve_number_set = available_set;
        } else {
          // This is not mmy cell so exclude the number in the reservation
          reserve_number_set = available_set & (~number_set);
        }

        // Make the reservation
        changed += reserve_cell_and_log(cell, reserve_number_set, "reserve_cells_with_index_in_row");
      }
    }
  }

  // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
  for (i=0; i<3; i++) {
    if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i]) {
      if (reserve_row_in_tile(possible_cell, number_set))
        changed++;
    }
  }

  return changed;
}


static inline
int reserve_cells_with_index_in_col(struct sudoku_cell *possible_cell, unsigned int possible_index_set, unsigned int number_set)
{
  int row, my_col, my_tile, changed, i;
  unsigned int reserve_number_set, available_set;
  struct sudoku_board *board;
  struct sudoku_cell *cell;

  changed = 0;
  my_col = possible_cell->col;
  my_tile = possible_cell->tile;
  board = possible_cell->board_ref;
  for (row=0; row<9; row++) {
    cell = &board->cells[row][my_col];
    if (cell->number == 0) {
      available_set = get_cell_possible_number_set(cell);
      if (available_set & number_set) {
        // The number is a possibility for this cell
        if (INDEX_TO_SET(row) & possible_index_set) {
          // This is cell so include the number in the reservation
          reserve_number_set = available_set;
        } else {
          // This is not mmy cell so exclude the number in the reservation
          reserve_number_set = available_set & (~number_set);
        }

        // Make the reservation
        changed += reserve_cell_and_log(cell, reserve_number_set, "reserve_cells_with_index_in_col");
      }
    }
  }

  // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
  for (i=0; i<3; i++) {
    if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i]) {
      if (reserve_col_in_tile(possible_cell, number_set))
        changed++;
    }
  }

  return changed;
}


static
int propagate_constraints(struct sudoku_board *board)
{
  unsigned int row, col, tile, index, row_set, col_set, tile_set, index_set;
  int round, changed, changed_total;
  struct sudoku_cell *cell;
  unsigned int number;

  if (board->debug_level >= 2)
    printf("Propagate constraints\n");

  changed_total = 0;
  round = 0;
  do {
    if (board->debug_level >= 2)
      printf("  Round %i\n", round++);
    changed = 0;

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
        number = get_cell_available_number(cell);

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
        number = get_cell_available_number(cell);

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
        number = get_cell_available_number(cell);

        if (number) {
          set_cell_number_and_log(cell, number);
          changed++;
        }
      }
    }

    changed_total += changed;
  } while (changed && !is_board_done(board));

  return changed_total;
}


static inline
int for_each_empty_cell(struct sudoku_board *board, int (*func)(struct sudoku_cell *cell))
{
  unsigned int row, col, row_set, col_set;
  int changed;
  struct sudoku_cell *cell;

  // Loop over all empty cells by going row by row and then col by col
  changed = 0;
  row_set = board->row_empty_set;
  while (row_set) {
    row = get_next_index_from_set(&row_set);
    col_set = board->row_cell_empty_set[row];
    while (col_set) {
      col = get_next_index_from_set(&col_set);
      cell = &board->cells[row][col];
      if (cell->number == 0)
        changed += func(cell);
    }
  }

  return changed;
}


static inline
int for_each_empty_dependent_cell(struct sudoku_cell *cell,
                                  int (*func)(struct sudoku_cell *cell, struct sudoku_cell *from_cell))
{
  unsigned row, col, tile, index, row_set, col_set, index_set;
  struct sudoku_board *board;
  struct sudoku_cell *from_cell;
  int changed;

  changed = 0;
  board = cell->board_ref;
  from_cell = cell; // We are looking at other cells form the perspective of this cell (from_cell)

  // Loop over all empty cells on the same row as cell but not cell itself
  row = from_cell->row;
  col_set = board->row_cell_empty_set[row] & ~(INDEX_TO_SET(from_cell->col));
  while (col_set) {
    col = get_next_index_from_set(&col_set);
    cell = &board->cells[row][col];
    if (cell->number == 0)
      changed += func(cell, from_cell);
  }

  // Loop over all empty cells in the same col as cell but not cell itself
  col = from_cell->col;
  row_set = board->col_cell_empty_set[col] & ~(INDEX_TO_SET(from_cell->row));
  while (row_set) {
    row = get_next_index_from_set(&row_set);
    cell = &board->cells[row][col];
    if (cell->number == 0)
      changed += func(cell, from_cell);
  }

  // Loop over all empty cells in the same tile as cell but not cell itself
  tile = from_cell->tile;
  index_set = board->tile_cell_empty_set[tile] & ~(INDEX_TO_SET(from_cell->index_in_tile));
  while (index_set) {
    index = get_next_index_from_set(&index_set);
    cell = board->tile_ref[tile][index];
    // Make sure we haven't already covered this cell in row and col above
    if ((cell->row != from_cell->row) && (cell->col != from_cell->col) && (cell->number == 0))
      changed += func(cell, from_cell);
  }

  return changed;
}


static inline
int for_each_interdependent_empty_cell(struct sudoku_cell *cell, struct sudoku_cell *from_cell,
                                       int (*func)(struct sudoku_cell *cell, struct sudoku_cell *from_cell))
{
  unsigned row, col, tile, index, row_set, col_set, index_set;
  struct sudoku_board *board;
  struct sudoku_cell *next_cell;
  int changed;

  changed = 0;
  board = cell->board_ref;

  // Loop over all empty cells on the same row as cell but not the tile of the cell itself
  row = cell->row;
  if (!from_cell || (from_cell->row != row)) {
    col_set = board->row_cell_empty_set[row] & ~(col_to_tile_mask[cell->col]);
    while (col_set) {
      col = get_next_index_from_set(&col_set);
      next_cell = &board->cells[row][col];
      if (next_cell->number == 0)
        changed += func(next_cell, cell);
    }
  }

  // Loop over all empty cells in the same col as cell but not the tile of the cell itself
  col = cell->col;
  if (!from_cell || (from_cell->col != col)) {
    row_set = board->col_cell_empty_set[col] & ~(row_to_tile_mask[cell->row]);
    while (row_set) {
      row = get_next_index_from_set(&row_set);
      next_cell = &board->cells[row][col];
      if (next_cell->number == 0)
        changed += func(next_cell, cell);
    }
  }

  // Loop over all empty cells in the same tile as cell but not cell itself
  tile = cell->tile;
  if (!from_cell || (from_cell->tile != tile)) {
    index_set = board->tile_cell_empty_set[tile] & ~(INDEX_TO_SET(cell->index_in_tile));
    while (index_set) {
      index = get_next_index_from_set(&index_set);
      next_cell = board->tile_ref[tile][index];
      if (next_cell->number == 0)
        changed += func(next_cell, cell);
    }
  }

  return changed;
}


#if 0
// This is the old slow solve_possible
static
int solve_possible(struct sudoku_board *board)
{
  int row, col, round, changed, changed_total;
  struct sudoku_cell *cell;
  unsigned int number;

  if (board->debug_level >= 2)
    printf("Solve possible\n");

  changed_total = 0;
  round = 0;
  do {
    if (board->debug_level >= 2)
      printf("  Round %i\n", round++);

    changed = 0;
    for (row=0; row<9; row++) {
      for (col=0; col<9; col++) {
        cell = &board->cells[row][col];
        if (cell->number == 0) {
          number = get_cell_available_number(cell);

          if (number) {
            set_cell_number_and_log(cell, number);
            changed++;
          }
        }
      }
    }
    changed_total += changed;
  } while (changed && !is_board_done(board));

  mark_board_not_dirty(board);

  return changed_total;
}
#else


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
        number = get_cell_available_number(cell);

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
#endif


static
int solve_eliminate_tiles_1(struct sudoku_board *board)
{
  int tile, index, i, j, changed, possibilities, same_index_set_count;
  struct sudoku_cell *cell, *possible_cell;
  unsigned int number, number_set, reserve_number_set, this_reserve_number_set, possible_index_set, index_set;
  unsigned int prior_possible_index_set[9];

  if (board->debug_level >= 2)
    printf("Solve eliminate tiles\n");

  changed = 0;
  for (tile=0; tile<9; tile++) {
    if (board->debug_level >= 2)
      printf("  Tile %i\n", tile);
    for (number=1; number<=9; number++) {
      if (board->debug_level >= 2)
        printf("    Number %i\n", number);
      number_set = NUMBER_TO_SET(number);
      prior_possible_index_set[number-1] = 0;

      // Check if number is already taken in this tile. If so, skip the number!
      if (!(board->tile_number_taken_set[tile] & number_set)) {
        possibilities = 0;
        possible_cell = 0;
        possible_index_set = 0;

        // Go through all possible positions for Number
        for (index=0; index<9; index++) {
          cell = board->tile_ref[tile][index];
          // Is this cell free and can we put Number in this cell?
          if ((cell->number == 0) && (get_cell_possible_number_set(cell) & number_set)) {
            possibilities++;
            possible_cell = cell;
            possible_index_set |= NUMBER_TO_SET(index);

            if (board->debug_level >= 4)
              printf(DINDENT "Possible [%i,%i] avail_set: 0x%X <> 0x%X cell_number: %i\n", 
                     cell->row, cell->col, get_cell_possible_number_set(cell), number_set, cell->number);
          }
        }

        if (board->debug_level >= 4) {
          printf(DINDENT "Tile: %i, Number: %i, possibilities: %i, possible_index_set is ", tile, number, possibilities);
          for(int i=0; i<9; i++)
            if (possible_index_set & NUMBER_TO_SET(i))
              printf("%i ", i);
          printf("\n");
        }

        // Do we have any possibilities
        if (possibilities == 0) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(possible_cell, number);
          changed++;
        } else {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_index_set[number-1] = possible_index_set;
          same_index_set_count = 0;

          // Initialise the reserve_number_set with the current number, then add the others in the following loop
          reserve_number_set = NUMBER_TO_SET(number);

          for (i=0; i<(number-1); i++) {
            if (prior_possible_index_set[i] == possible_index_set) {
              same_index_set_count++;
              // The Number is i+1 here - yes, weird!
              reserve_number_set |= NUMBER_TO_SET(i+1);
            }
          }

          // Do we have other Numbers with the same possibilies
          if ((same_index_set_count+1) == possibilities) {
            // Reserve these possibilities - include current number in this loop
            for (index=0; index<9; index++) {
              if (NUMBER_TO_SET(index) & possible_index_set) {
                // Finally - Reserve!!!
                changed += reserve_cell_and_log(board->tile_ref[tile][index], reserve_number_set, "solve_eliminate_tiles (complete)");        
              }
            }
          }

          if (possibilities <= 3) {
            // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
            for (i=0; i<(number-1); i++) {
              // Is there any overlap with a previos set
              if (prior_possible_index_set[i] & possible_index_set) {
                // We have overlap - does this overlap takes us to 3
                index_set = prior_possible_index_set[i] | possible_index_set;
                if (bit_count[index_set] == 3) {
                  // Look for a third that is within this set
                  for (j=(i+1); j<(number-1); j++) {
                    if (prior_possible_index_set[j] && ((index_set | prior_possible_index_set[j]) == index_set)) {
                      // Now we have the three numbers (i+1, j+1, number) that go into index_set
                      reserve_number_set = NUMBER_TO_SET(i+1) | NUMBER_TO_SET(j+1) | NUMBER_TO_SET(number);

                      // Reserve these possibilities - include current number in this loop
                      for (index=0; index<9; index++) {
                        if (NUMBER_TO_SET(index) & index_set) {
                          // Finally - Reserve!!!
                          this_reserve_number_set = reserve_number_set & get_cell_possible_number_set(board->tile_ref[tile][index]);
                          changed += reserve_cell_and_log(board->tile_ref[tile][index], this_reserve_number_set, "solve_eliminate_tiles (partial)");        
                        }
                      }
                    }
                  }
                }
              }
            }

            // If all possibilities (=2 or 3) on the same row, if so have the row reserve them
            for (i=0; i<3; i++) {
              if ((possible_index_set | index_row_mask[i]) == index_row_mask[i]) {
                if (reserve_tile_in_row(possible_cell, number_set)) {
                  changed++;
                }
              }
            }

            // If all possibilities (=2 or 3) on the same col, if so have the col reserve them
            for (i=0; i<3; i++) {
              if ((possible_index_set | index_col_mask[i]) == index_col_mask[i]) {
                if (reserve_tile_in_col(possible_cell, number_set)) {
                  changed++;
                }
              }
            }
          }
        }
      }
    }
  }

  return changed;
}


static
int solve_eliminate_rows_1(struct sudoku_board *board)
{
  int row, col, i, j, changed, possibilities, same_index_set_count;
  struct sudoku_cell *cell, *possible_cell;
  unsigned int number, number_set, reserve_number_set, this_reserve_number_set, possible_index_set, index_set;
  unsigned int prior_possible_index_set[9];

  if (board->debug_level >= 2)
    printf("Solve eliminate rows\n");

  changed = 0;
  for (row=0; row<9; row++) {
    if (board->debug_level >= 2)
      printf("  Row %i\n", row);

    for (number=1; number<=9; number++) {
      if (board->debug_level >= 2)
        printf("    Number %i\n", number);
      number_set = NUMBER_TO_SET(number);
      prior_possible_index_set[number-1] = 0;

      // Check if number is already taken in this row. If so, skip the number!
      if (!(board->row_number_taken_set[row] & number_set)) {
        possibilities = 0;
        possible_cell = 0;
        possible_index_set = 0;

        // Go through all possible positions for Number
        for (col=0; col<9; col++) {
          cell = &board->cells[row][col];
          // Is this cell free and can we put Number in this cell?
          if ((cell->number == 0) && (get_cell_possible_number_set(cell) & number_set)) {
            possibilities++;
            possible_cell = cell;
            possible_index_set |= NUMBER_TO_SET(col);

            if (board->debug_level >= 4)
              printf(DINDENT "Possible [%i,%i] avail_set: 0x%X <> 0x%X cell_number: %i\n", 
                     cell->row, cell->col, get_cell_possible_number_set(cell), number_set, cell->number);
          }
        }

        // Do we have any possibilities
        if (possibilities == 0) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(possible_cell, number);
          changed++;
        } else {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_index_set[number-1] = possible_index_set;
          same_index_set_count = 0;

          // Initialise the reserve_number_set with the current number, then add the others in the following loop
          reserve_number_set = NUMBER_TO_SET(number);

          for (i=0; i<(number-1); i++) {
            if (prior_possible_index_set[i] == possible_index_set) {
              same_index_set_count++;
              // The Number is i+1 here - yes, weird!
              reserve_number_set |= NUMBER_TO_SET(i+1);
            }
          }

          // Do we have other Numbers with the same possibilies
          if ((same_index_set_count+1) == possibilities) {
            // Reserve these possibilities - include current number in this loop
            for (col=0; col<9; col++) {
              if (NUMBER_TO_SET(col) & possible_index_set) {
                // Finally - Reserve!!!
                changed += reserve_cell_and_log(&board->cells[row][col], reserve_number_set, "solve_eliminate_rows");
              }
            }
          }

          if (possibilities <= 3) {
            // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
            for (i=0; i<(number-1); i++) {
              // Is there any overlap with a previos set
              if (prior_possible_index_set[i] & possible_index_set) {
                // We have overlap - does this overlap takes us to 3
                index_set = prior_possible_index_set[i] | possible_index_set;
                if (bit_count[index_set] == 3) {
                  // Look for a third that is within this set
                  for (j=(i+1); j<(number-1); j++) {
                    if (prior_possible_index_set[j] && ((index_set | prior_possible_index_set[j]) == index_set)) {
                      // Now we have the three numbers (i+1, j+1, number) that go into index_set
                      reserve_number_set = NUMBER_TO_SET(i+1) | NUMBER_TO_SET(j+1) | NUMBER_TO_SET(number);

                      // Reserve these possibilities - include current number in this loop
                      for (col=0; col<9; col++) {
                        if (NUMBER_TO_SET(col) & index_set) {
                          // Finally - Reserve!!!
                          this_reserve_number_set = reserve_number_set & get_cell_possible_number_set(&board->cells[row][col]);
                          changed += reserve_cell_and_log(&board->cells[row][col], this_reserve_number_set, "solve_eliminate_rows (partial)");
                        }
                      }
                    }
                  }
                }
              }
            }

            // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
            for (i=0; i<3; i++) {
              if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i])
                if (reserve_row_in_tile(possible_cell, number_set))
                  changed++;
            }
          }
        }
      }
    }
  }

  return changed;
}


static
int solve_eliminate_cols_1(struct sudoku_board *board)
{
  int row, col, i, j, changed, possibilities, same_index_set_count;
  struct sudoku_cell *cell, *possible_cell;
  unsigned int number, number_set, reserve_number_set, this_reserve_number_set, possible_index_set, index_set;
  unsigned int prior_possible_index_set[9];

  if (board->debug_level >= 2)
    printf("Solve eliminate cols\n");

  changed = 0;
  for (col=0; col<9; col++) {
    if (board->debug_level >= 2)
      printf("  Col %i\n", col);

    for (number=1; number<=9; number++) {
      if (board->debug_level >= 2)
        printf("    Number %i\n", number);
      number_set = NUMBER_TO_SET(number);
      prior_possible_index_set[number-1] = 0;

      // Check if number is already taken in this column. If so, skip the number!
      if (!(board->col_number_taken_set[col] & number_set)) {
        possibilities = 0;
        possible_cell = 0;
        possible_index_set = 0;

        // Go through all possible positions for Number
        for (row=0; row<9; row++) {
          cell = &board->cells[row][col];
          // Is this cell free and can we put Number in this cell?
          if ((cell->number == 0) && (get_cell_possible_number_set(cell) & number_set)) {
            possibilities++;
            possible_cell = cell;
            possible_index_set |= NUMBER_TO_SET(row);

            if (board->debug_level >= 4)
              printf(DINDENT "Possible [%i,%i] avail_set: 0x%X <> 0x%X cell_number: %i\n", 
                     cell->row, cell->col, get_cell_possible_number_set(cell), number_set, cell->number);
          }
        }

        // Do we have any possibilities
        if (possibilities == 0) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(possible_cell, number);
          changed++;
        } else {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_index_set[number-1] = possible_index_set;
          same_index_set_count = 0;

          // Initialise the reserve_number_set with the current number, then add the others in the following loop
          reserve_number_set = NUMBER_TO_SET(number);

          for (i=0; i<(number-1); i++) {
            if (prior_possible_index_set[i] == possible_index_set) {
              same_index_set_count++;
              // The Number is i+1 here - yes, weird!
              reserve_number_set |= NUMBER_TO_SET(i+1);
            }
          }

          // Do we have other Numbers with the same possibilies
          if ((same_index_set_count+1) == possibilities) {
            // Reserve these possibilities - include current number in this loop
            for (row=0; row<9; row++) {
              if (NUMBER_TO_SET(row) & possible_index_set) {
                // Finally - Reserve!!!
                changed += reserve_cell_and_log(&board->cells[row][col], reserve_number_set, "solve_eliminate_cols (complete)");
              }
            }
          }

          if (possibilities <= 3) {
            // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
            for (i=0; i<(number-1); i++) {
              // Is there any overlap with a previos set
              if (prior_possible_index_set[i] & possible_index_set) {
                // We have overlap - does this overlap takes us to 3
                index_set = prior_possible_index_set[i] | possible_index_set;
                if (bit_count[index_set] == 3) {
                  // Look for a third that is within this set
                  for (j=(i+1); j<(number-1); j++) {
                    if (prior_possible_index_set[j] && ((index_set | prior_possible_index_set[j]) == index_set)) {
                      // Now we have the three numbers (i+1, j+1, number) that go into index_set
                      reserve_number_set = NUMBER_TO_SET(i+1) | NUMBER_TO_SET(j+1) | NUMBER_TO_SET(number);

                      // Reserve these possibilities - include current number in this loop
                      for (row=0; row<9; row++) {
                        if (NUMBER_TO_SET(row) & index_set) {
                          // Finally - Reserve!!!
                          this_reserve_number_set = reserve_number_set & get_cell_possible_number_set(&board->cells[row][col]);
                          changed += reserve_cell_and_log(&board->cells[row][col], this_reserve_number_set, "solve_eliminate_rows (partial)");
                        }
                      }
                    }
                  }
                }
              }
            }

            // If all possibilities (=2 or 3) in the same tile, if so have the tile reserve them
            for (i=0; i<3; i++) {
              if ((possible_index_set | index_tile_mask[i]) == index_tile_mask[i])
                if (reserve_col_in_tile(possible_cell, number_set))
                  changed++;
            }
          }
        }
      }
    }
  }

  return changed;
}


static inline
int find_and_reserve_group_with_index(struct sudoku_cell *cell, 
                                      unsigned int prior_possible_number_set[9], int this_index, 
                                      unsigned int possible_number_set, 
                                      reserve_with_index_set_func_t reserve_with_index_set_func,
                                      char *parent_func_name)
{
  int i, j, changed, possibilities, same_number_set_count;
  unsigned int possible_index_set, joint_number_set;

  changed = 0;
  possibilities = bit_count[possible_number_set];

  same_number_set_count = 0;
  possible_index_set = INDEX_TO_SET(this_index);
  for (i=0; i<this_index; i++) {
    if (prior_possible_number_set[i] == possible_number_set) {
      same_number_set_count++;
      possible_index_set |= INDEX_TO_SET(i);
    }
  }

  // Do we have other cells with the same possibilies
  if ((same_number_set_count+1) == possibilities) {
    if (reserve_with_index_set_func(cell, possible_index_set, possible_number_set))
      changed++;
  }

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
              if (cell->board_ref->debug_level >= 4)
                printf(DINDENT "%s: Reserving cells in row %i with possibilities: %i index_set: %i  number_set: %i\n",
                        parent_func_name, cell->row, possibilities, possible_index_set, joint_number_set);
              if (reserve_with_index_set_func(cell, possible_index_set, joint_number_set))
                changed++;
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
int solve_eliminate_tiles_2(struct sudoku_board *board)
{
  int tile, index, changed, possibilities;
  struct sudoku_cell *cell;
  unsigned int possible_number_set;
  unsigned int prior_possible_number_set[9];

  if (board->debug_level >= 2)
    printf("Solve eliminate tiles 2\n");

  changed = 0;
  for (tile=0; tile<9; tile++) {
    if (board->debug_level >= 2) {
      printf("  Tile %i\n", tile);
      if (board->debug_level >= 4)
        print_possible(board, DINDENT);
    }

    for (index=0; index<9; index++) {
      if (board->debug_level >= 2)
        printf("    Index %i\n", index);
      prior_possible_number_set[index] = 0;
      cell = board->tile_ref[tile][index];
      if (cell->number == 0) {
        possible_number_set = get_cell_possible_number_set(cell);
        possibilities = bit_count[possible_number_set];

        // Do we have any possibilities
        if (possibilities == 0) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(cell, available_set_to_number[possible_number_set]);
          changed++;
        } else {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_number_set[index] = possible_number_set;

          changed += find_and_reserve_group_with_index(cell, prior_possible_number_set, 
                                                       index, possible_number_set, 
                                                       &reserve_cells_with_index_in_tile,
                                                       "solve_eliminate_tiles_2");
        }
      }
    }
  }

  return changed;
}


static
int solve_eliminate_rows_2(struct sudoku_board *board)
{
  int row, col, changed, possibilities;
  struct sudoku_cell *cell;
  unsigned int possible_number_set;
  unsigned int prior_possible_number_set[9];

  if (board->debug_level >= 2)
    printf("Solve eliminate rows 2\n");

  changed = 0;
  for (row=0; row<9; row++) {
    if (board->debug_level >= 2) {
      printf("  Row %i\n", row);
      if (board->debug_level >= 4)
        print_possible(board, DINDENT);
    }

    for (col=0; col<9; col++) {
      if (board->debug_level >= 2)
        printf("    Col %i\n", col);
      prior_possible_number_set[col] = 0;
      cell = &board->cells[row][col];
      if (cell->number == 0) {
        possible_number_set = get_cell_possible_number_set(cell);
        possibilities = bit_count[possible_number_set];

        // Do we have any possibilities
        if (possibilities == 0) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(cell, available_set_to_number[possible_number_set]);
          changed++;
        } else {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_number_set[col] = possible_number_set;

          changed += find_and_reserve_group_with_index(cell, prior_possible_number_set, 
                                                       col, possible_number_set, 
                                                       &reserve_cells_with_index_in_row,
                                                       "solve_eliminate_rows_2");
        }
      }
    }
  }

  return changed;
}


static
int solve_eliminate_cols_2(struct sudoku_board *board)
{
  int row, col, changed, possibilities;
  struct sudoku_cell *cell;
  unsigned int possible_number_set;
  unsigned int prior_possible_number_set[9];

  if (board->debug_level >= 2)
    printf("Solve eliminate cols 2\n");

  changed = 0;
  for (col=0; col<9; col++) {
    if (board->debug_level >= 2) {
      printf("  Col %i\n", col);
      if (board->debug_level >= 4)
        print_possible(board, DINDENT);
    }

    for (row=0; row<9; row++) {
      if (board->debug_level >= 2)
        printf("    Row %i\n", row);
      prior_possible_number_set[row] = 0;
      cell = &board->cells[row][col];
      if (cell->number == 0) {
        possible_number_set = get_cell_possible_number_set(cell);
        possibilities = bit_count[possible_number_set];

        // Do we have any possibilities
        if (possibilities == 0) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(cell, available_set_to_number[possible_number_set]);
          changed++;
        } else {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_number_set[row] = possible_number_set;

          changed += find_and_reserve_group_with_index(cell, prior_possible_number_set, 
                                                       row, possible_number_set, 
                                                       &reserve_cells_with_index_in_col,
                                                       "solve_eliminate_cols_2");
        }
      }
    }
  }

  return changed;
}


int solve_eliminate(struct sudoku_board *board)
{
  int this_changed, changed, total_changed, round, i;
  int (*const solve_func_arr[])(struct sudoku_board*) = {
    solve_eliminate_tiles_1,
    solve_eliminate_rows_1,
    solve_eliminate_cols_1,
    solve_eliminate_rows_2,
    solve_eliminate_cols_2,
    solve_eliminate_tiles_2
  }; 

  if (board->debug_level >= 2)
    printf("Solve eliminate\n");

  total_changed = 0;
  round = 0;

  do {
    if (board->debug_level >= 2)
      printf("  Round %i\n", round++);
    changed = 0;

    for(i=0; i<(sizeof(solve_func_arr)/sizeof(solve_func_arr[0])) ; i++) {
      this_changed = solve_func_arr[i](board);
      if (is_board_done(board))
        break;
      if (this_changed) {
        changed += this_changed + propagate_constraints(board);
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
  int row1, col1, row2, col2, start_row, start_col, changed;
  unsigned int tile1;
  struct sudoku_cell *cell1, *cell2, *cell3, *cell4;
  const int search_start_row_for_tile[9] = { 3, 3, 0, 6, 6, 0, 0, 0, 0};
  const int search_start_col_for_tile[9] = { 3, 6, 0, 3, 6, 0, 0, 0, 0};

  if (board->debug_level >= 2)
    printf("Solve tile interlock rectangle\n");

  changed = 0;
  for (row1=0; row1<6; row1++) {
    if (board->debug_level >= 2)
      printf("  Row %i\n", row1);

    for (col1=0; col1<6; col1++) {
      if (board->debug_level >= 2)
        printf("    Col %i\n", col1);

      cell1 = &board->cells[row1][col1];
      if (cell1->number == 0) {
        tile1 = cell1->tile;

        start_row = search_start_row_for_tile[tile1];
        start_col = search_start_col_for_tile[tile1];
        assert(start_row && start_col);

        for (row2=start_row; row2<9; row2++) {
          for (col2=start_col; col2<9; col2++) {
            cell2 = &board->cells[row2][col2]; 
            if ((cell2->number == 0) && 
                (board->cells[row1][col2].number == 0) &&
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
  }

  return changed;
}


static
int solve_tile_interlock(struct sudoku_board *board)
{
  int this_changed, changed, total_changed, round;

  if (board->debug_level >= 2)
    printf("Solve tile interlock\n");

  total_changed = 0;
  round = 0;

  do {
    if (board->debug_level >= 2)
      printf("  Round %i\n", round);
    round++;
    changed = 0;

    this_changed = solve_tile_interlock_rectangle(board);
    if (is_board_done(board))
      break;
    if (this_changed) {
      changed += this_changed + propagate_constraints(board);
      if (is_board_done(board))
        break;
      changed += solve_eliminate(board);
      if (is_board_done(board))
        break;
    }

    total_changed += changed;
  } while ((changed > 0) && (!is_board_done(board)));

  return total_changed;
}


static 
void print_cell_loop(struct sudoku_cell *cell) {
  printf(DINDENT "Loop: ");
  do {
    printf("[%i,%i]=", cell->row, cell->col);
    print_number_set(get_cell_possible_number_set(cell), " ");
    cell = cell->walk_next_cell;
  } while (cell->walk_hop_count != 0);
  printf("\n");
}


static
int analyze_interdependent_loop(struct sudoku_cell *cell) {
  struct sudoku_cell *tmp_cell;
  struct stack {
    unsigned int number;
    unsigned int number_as_set;
    unsigned int possible_number_set;
    unsigned int remaining_number_set;
  } stack[MAX_WALK_HOP_COUNT+1];
  struct stack *sp; // stack pointer
  struct stack *bsp; // base stack pointer
  struct stack *tsp; // top stack pointer
  enum {
    state_goto_next_level,
    state_goto_previous_level,
    state_at_level,
    state_done,
  } state;
  unsigned int idx;
  int changed;
  bool dead;
  
  changed = 0;
  if (cell->board_ref->debug_level >= 4) 
    print_cell_loop(cell);

  // Build help stack data
  idx = 0;
  bsp = &stack[0];
  sp = bsp;
  tmp_cell = cell;
  do {
    assert(idx == tmp_cell->walk_hop_count);
    assert(idx <= MAX_WALK_HOP_COUNT);
    (sp++)->possible_number_set = get_cell_possible_number_set(tmp_cell);
    idx++;
    tmp_cell = tmp_cell->walk_next_cell;
  } while (tmp_cell != cell);
  tsp = sp-1; 

  // Go though all possible number for [0] and see if we can get them all to work
  bsp->remaining_number_set = bsp->possible_number_set;
  while (bsp->remaining_number_set) {
    bsp->number = get_next_index_from_set(&bsp->remaining_number_set);
    bsp->number_as_set = NUMBER_TO_SET(bsp->number);

    dead = true;
    sp = bsp;
    state = state_goto_next_level;
    while (state != state_done) {
      switch (state) {
        case state_goto_next_level:
          if (sp == tsp) {
            // We have reached the top of the stack and are done - are we dead?
            if (tsp->number != bsp->number) {
              dead = false;
              state = state_done;
            } else {
              state = state_at_level;
            }
          } else {
            // Move to the next level and see if we have any options here
            sp++;
            sp->remaining_number_set = (sp->possible_number_set & ~(sp-1)->number_as_set);
            if (sp->remaining_number_set == 0)
              state = state_goto_previous_level;
            else
              state = state_at_level;
          }
          break;

        case state_at_level:
          if (sp->remaining_number_set) {
            sp->number = get_next_index_from_set(&sp->remaining_number_set);
            sp->number_as_set = NUMBER_TO_SET(sp->number);
            state = state_goto_next_level;
          } else {
            state = state_goto_previous_level;
          }
          break;

        case state_goto_previous_level:
          if (--sp == bsp) {
            // We are back at base level and did not finish 
            dead = true;
            state = state_done;
          } else {
            state = state_at_level;
          }
          break;

        default:
          break;
      }
    }

    if (dead) {
      // No path found for number - number is not possible - remove it!
      changed += reserve_cell_and_log(cell, (bsp->possible_number_set & ~bsp->number_as_set), 
                                      "analyze_interdependent_loop"); 
      bsp->possible_number_set = get_cell_possible_number_set(cell);
    }
  }

  return changed;
}


static
int recursive_walk_to_find_loops(struct sudoku_cell *cell, struct sudoku_cell *from_cell) {
  struct sudoku_board *board;
  int changed;

  board = from_cell->board_ref;
  if (cell->walk_next_cell) {
    if (cell->walk_hop_count == 0) {
      // We have reached the start so we have a loop - close the loop & analyze
      from_cell->walk_next_cell = cell; 
      changed = analyze_interdependent_loop(cell);
      from_cell->walk_next_cell = NULL; 
      return changed;
    } else {
      // This cell is already in our walk path and on our call stack - ignore it
     return 0;
    }
  }

  if (from_cell->walk_hop_count == MAX_WALK_HOP_COUNT) {
    // We have walked too far - time to return
    return 0;
  }

  // This is a cell we haven't visited so let's explore it - recurse!
  from_cell->walk_next_cell = cell; 
  cell->walk_hop_count = from_cell->walk_hop_count + 1;
  changed = for_each_interdependent_empty_cell(cell, from_cell, recursive_walk_to_find_loops);
  cell->walk_hop_count = 0;
  assert(cell->walk_next_cell == 0);
  from_cell->walk_next_cell = NULL; 

  return changed;
}


static
int validate_constraints_for_cell(struct sudoku_cell *cell) 
{
  struct sudoku_board *board;
  int changed;

  changed = 0;
  board = cell->board_ref;
  if (board->debug_level >= 2) {
    printf("  Cell [%i,%i] with ", cell->row, cell->col);
    print_number_set(get_cell_possible_number_set(cell), "\n");
  }

  assert(cell->walk_next_cell == NULL);
  assert(cell->walk_hop_count == 0);

  // Find any loops that involves this starting cell
  changed = for_each_interdependent_empty_cell(cell, NULL, recursive_walk_to_find_loops);

  if (changed)
    changed += propagate_constraints(board);

  return changed;
}


static
int solve_validate_constraints(struct sudoku_board *board)
{
  int changed, total_changed;

  if (board->debug_level >= 2)
    printf("Solve validate constraints\n");

  total_changed = 0;
  do {
    changed = for_each_empty_cell(board, validate_constraints_for_cell);
    total_changed += changed;

    if (changed && !is_board_done(board)) {
      changed = solve_eliminate(board);
      total_changed += changed;
    }
    
  } while (changed);

  return total_changed;
}


static inline
void solve_hidden_cell(struct sudoku_cell *cell)
{
  unsigned int number, available_set;
  struct sudoku_board *future_board;

  available_set = get_cell_possible_number_set(cell);
  for (number=1; number<=9; number++) {
    if (available_set & NUMBER_TO_SET(number)) {
      if (cell->board_ref->debug_level)
        printf("Trying solution [%i,%i] = %i\n", cell->row, cell->col, number);

      future_board = dupilcate_board(cell->board_ref);
      future_board->nest_level++;
      future_board->debug_level = 0;
      set_cell_number(&future_board->cells[cell->row][cell->col], number);
      solve(future_board);

      if (future_board->undetermined_count == 0) {
        // Add to list of solutions
        if (cell->board_ref->debug_level >= 1)
          printf("Found hidden solution [%i,%i] = %i\n", cell->row, cell->col, number);
        assert(future_board->solutions_list == NULL);
        assert(future_board->solutions_count == 0);
        add_to_board_solutions_list(cell->board_ref, future_board);
        if (is_board_solved(cell->board_ref))
          return;
      } else if (future_board->solutions_list) {
        // Add to list of solutions
        if (cell->board_ref->debug_level >= 1)
          printf("Found hidden solution [%i,%i] = %i\n", cell->row, cell->col, number);
        add_list_to_board_solutions_list(cell->board_ref, future_board->solutions_list);
        future_board->solutions_list = NULL;
        future_board->solutions_count = 0;
        destroy_board(&future_board);
        if (is_board_solved(cell->board_ref))
          return;
      } else {
        destroy_board(&future_board);
      }
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

  if (!is_board_done(board))
    solve_validate_constraints(board);

  if (!is_board_done(board))
     solve_hidden(board);

  solutions_count = board->solutions_count;
  if (board->undetermined_count == 0)
    solutions_count++;

  return solutions_count;
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
