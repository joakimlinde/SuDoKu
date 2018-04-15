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


static unsigned int available_set_to_number[NUMBER_TO_SET(10)+1];
static unsigned int bit_count[NUMBER_TO_SET(10)+1];

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


static void print_number_set(unsigned int number_set, const char *postfix);

static void print_reserved_set_for_cell(struct sudoku_cell *cell, const char *comment);

static void print_possible(struct sudoku_board *board);

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


void init()
{
  int i;

  for (i=0; i<=NUMBER_TO_SET(10); i++)
    available_set_to_number[i] = 0;
  for (i=1; i<=9; i++)
    available_set_to_number[NUMBER_TO_SET(i)] = i;
  for (i=0; i<NUMBER_TO_SET(10); i++)
    bit_count[i] = bit_count_func(i);
}


static inline
int is_board_solved(struct sudoku_board *board)
{
  return ((board->undetermined_count == 0) || (board->solutions_count > 0));
}


static inline
int is_board_dead(struct sudoku_board *board)
{
  return board->dead;
}


static inline
void set_board_dead(struct sudoku_board *board)
{
  if (board->debug_level)
    printf("Board is declared dead!\n");

  board->dead = 1;
}


static inline
unsigned int get_cell_available_set(struct sudoku_cell *cell)
{
  unsigned int taken_set, available_set;

  taken_set = *cell->row_taken_set_ref;
  taken_set |= *cell->col_taken_set_ref;
  taken_set |= *cell->tile_taken_set_ref;

  available_set = TAKEN_TO_AVAIL_SET(taken_set);

  if (available_set && cell->reserved_for_number_set)
     available_set &= cell->reserved_for_number_set;

  return available_set;
}


static inline
unsigned int get_cell_available_number(struct sudoku_cell *cell)
{
  return available_set_to_number[get_cell_available_set(cell)];
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
        cell_bit_count = bit_count[get_cell_available_set(cell)];
        if (cell_bit_count == 0) {
          board->dead = 1;
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
  *cell->row_taken_set_ref |= number_set;
  *cell->col_taken_set_ref |= number_set;
  *cell->tile_taken_set_ref |= number_set;
}


static inline
void set_cell_number_and_log(struct sudoku_cell *cell, unsigned int number)
{
  set_cell_number(cell, number);

  if (cell->board_ref->debug_level)
    printf("    [%i,%i]  =  %i\n", cell->row, cell->col, number);  
}


static
void handle_bad_reserve_cell(struct sudoku_cell *cell, unsigned int number_set)
{
  unsigned int taken_set, available_set;

  taken_set = *cell->row_taken_set_ref;
  taken_set |= *cell->col_taken_set_ref;
  taken_set |= *cell->tile_taken_set_ref;

  available_set = TAKEN_TO_AVAIL_SET(taken_set);

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


static inline
int reserve_cell(struct sudoku_cell *cell, unsigned int number_set)
{
  unsigned int taken_set, available_set, new_reserved_for_number_set;
  int changed;

  taken_set = *cell->row_taken_set_ref;
  taken_set |= *cell->col_taken_set_ref;
  taken_set |= *cell->tile_taken_set_ref;

  available_set = TAKEN_TO_AVAIL_SET(taken_set);

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
      changed = 1;
    }
  } else {
    cell->reserved_for_number_set = number_set;
    changed = 1;
  }

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
        available_set = get_cell_available_set(cell);
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
          if (board->debug_level >= 2) {
            printf("reserve_row_in_tile: [%i,%i] = ", cell->row, cell->col);
            print_number_set(reserve_number_set, "\n");
          }
          if (reserve_cell(cell, reserve_number_set)) {
            changed++;
            if (board->debug_level >= 1)
              print_reserved_set_for_cell(cell, "row in tile");
          }
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
        available_set = get_cell_available_set(cell);
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
          if (board->debug_level >= 2) {
            printf("reserve_col_in_tile: [%i,%i] = ", cell->row, cell->col);
            print_number_set(reserve_number_set, "\n");
          }
          if (reserve_cell(cell, reserve_number_set)) {
            changed++;
            if (board->debug_level >= 1)
              print_reserved_set_for_cell(cell, "col in tile");
          }
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
      available_set = get_cell_available_set(cell);
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
        if (board->debug_level >= 2) {
          printf("reserve_tile_in_row: [%i,%i] = ", cell->row, cell->col);
          print_number_set(reserve_number_set, "\n");
        }
        if (reserve_cell(cell, reserve_number_set)) {
          changed++;
          if (board->debug_level >= 1)
            print_reserved_set_for_cell(cell, "tile in row");
        }
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
      available_set = get_cell_available_set(cell);
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
        if (board->debug_level >= 2) {
          printf("reserve_tile_in_col: [%i,%i] = ", cell->row, cell->col);
          print_number_set(reserve_number_set, "\n");
        }
        if (reserve_cell(cell, reserve_number_set)) {
          changed++;
          if (board->debug_level >= 1)
            print_reserved_set_for_cell(cell, "tile in row");
        }
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
      available_set = get_cell_available_set(cell);
      if (available_set & number_set) {
        if (INDEX_TO_SET(i) & possible_index_set) 
          reserve_number_set = available_set;
        else 
          reserve_number_set = available_set & (~number_set);
        
        if (board->debug_level >= 2) {
          printf("reserve_cells_with_index_in_tile: [%i,%i] = ", cell->row, cell->col);
          print_number_set(reserve_number_set, "\n");
        }
        if (reserve_cell(cell, reserve_number_set)) {
          changed++;
          if (board->debug_level >= 1)
            print_reserved_set_for_cell(cell, "index in tile");
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
      available_set = get_cell_available_set(cell);
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
        if (board->debug_level >= 2) {
          printf("reserve_cells_with_index_in_row: [%i,%i] = ", cell->row, cell->col);
          print_number_set(reserve_number_set, "\n");
        }
        if (reserve_cell(cell, reserve_number_set)) {
          changed++;
          if (board->debug_level >= 1)
            print_reserved_set_for_cell(cell, "index in row");
        }
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
      available_set = get_cell_available_set(cell);
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
        if (board->debug_level >= 2) {
          printf("reserve_cells_with_index_in_col: [%i,%i] = ", cell->row, cell->col);
          print_number_set(reserve_number_set, "\n");
        }
        if (reserve_cell(cell, reserve_number_set)) {
          changed++;
          if (board->debug_level >= 1)
            print_reserved_set_for_cell(cell, "index in col");
        }
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
int solve_possible(struct sudoku_board *board)
{
  int row, col, round, changed, changed_total;
  struct sudoku_cell *cell;
  unsigned int number;

  if (board->debug_level)
    printf("Solve possible\n");

  changed_total = 0;
  round = 0;
  do {
    if (board->debug_level >= 2)
      printf("  Round %i:\n", round++);

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
  } while (changed);

  return changed_total;
}


static
int solve_eliminate_tiles(struct sudoku_board *board)
{
  int tile, index, i, j, changed, possibilities, same_index_set_count;
  struct sudoku_cell *cell, *possible_cell;
  unsigned int number, number_set, reserve_number_set, this_reserve_number_set, possible_index_set, index_set;
  unsigned int prior_possible_index_set[9];

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
      if (!(board->tile_taken_set[tile] & number_set)) {
        possibilities = 0;
        possible_cell = 0;
        possible_index_set = 0;

        // Go through all possible positions for Number
        for (index=0; index<9; index++) {
          cell = board->tile_ref[tile][index];
          // Is this cell free and can we put Number in this cell?
          if ((cell->number == 0) && (get_cell_available_set(cell) & number_set)) {
            possibilities++;
            possible_cell = cell;
            possible_index_set |= NUMBER_TO_SET(index);

            if (board->debug_level >= 3)
              printf("Possible [%i,%i] avail_set: 0x%X <> 0x%X cell_number: %i\n", 
                     cell->row, cell->col, get_cell_available_set(cell), number_set, cell->number);
          }
        }

        if (board->debug_level >= 3) {
          printf("Tile: %i, Number: %i, possibilities: %i, possible_index_set is ", tile, number, possibilities);
          for(int i=0; i<9; i++)
            if (possible_index_set & NUMBER_TO_SET(i))
              printf("%i ", i);
          printf("\n");
        }

        // Do we have any possibilities
        if (!possibilities) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(possible_cell, number);
          changed++;
        } else if (possibilities > 1) {
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
                if (board->debug_level >= 2) {
                  printf("solve_eliminate_tiles: (complete) [%i,%i] = ", 
                          board->tile_ref[tile][index]->row, board->tile_ref[tile][index]->col);
                  print_number_set(reserve_number_set, "\n");
                }
                if (reserve_cell(board->tile_ref[tile][index], reserve_number_set)) {
                  changed++;
                  if (board->debug_level >= 1)
                    print_reserved_set_for_cell(board->tile_ref[tile][index], "complete match by tile");
                }
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
                          this_reserve_number_set = reserve_number_set & get_cell_available_set(board->tile_ref[tile][index]);
                          if (board->debug_level >= 2) {
                            printf("solve_eliminate_tiles: (partial) [%i,%i] = ", 
                                    board->tile_ref[tile][index]->row, board->tile_ref[tile][index]->col);
                            print_number_set(this_reserve_number_set, "\n");
                          }
                          if (reserve_cell(board->tile_ref[tile][index], this_reserve_number_set)) {
                            changed++;
                            if (board->debug_level >= 1)
                              print_reserved_set_for_cell(board->tile_ref[tile][index], "partial match by tile");
                          }
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
int solve_eliminate_rows(struct sudoku_board *board)
{
  int row, col, i, j, changed, possibilities, same_index_set_count;
  struct sudoku_cell *cell, *possible_cell;
  unsigned int number, number_set, reserve_number_set, this_reserve_number_set, possible_index_set, index_set;
  unsigned int prior_possible_index_set[9];

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
      if (!(board->row_taken_set[row] & number_set)) {
        possibilities = 0;
        possible_cell = 0;
        possible_index_set = 0;

        // Go through all possible positions for Number
        for (col=0; col<9; col++) {
          cell = &board->cells[row][col];
          // Is this cell free and can we put Number in this cell?
          if ((cell->number == 0) && (get_cell_available_set(cell) & number_set)) {
            possibilities++;
            possible_cell = cell;
            possible_index_set |= NUMBER_TO_SET(col);

            if (board->debug_level >= 3)
              printf("Possible [%i,%i] avail_set: 0x%X <> 0x%X cell_number: %i\n", 
                     cell->row, cell->col, get_cell_available_set(cell), number_set, cell->number);
          }
        }

        // Do we have any possibilities
        if (!possibilities) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(possible_cell, number);
          changed++;
        } else if (possibilities > 1) {
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
                if (board->debug_level >= 2) {
                  printf("solve_eliminate_rows: (complete) [%i,%i] = ", row, col);
                  print_number_set(reserve_number_set, "\n");
                }
                if (reserve_cell(&board->cells[row][col], reserve_number_set)) {
                  changed++;
                  if (board->debug_level >= 1)
                    print_reserved_set_for_cell(&board->cells[row][col], "complete match by row");
                }
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
                          if (board->debug_level >= 2) {
                            printf("solve_eliminate_rows: (partial) [%i,%i] = ", row, col);
                            print_number_set(this_reserve_number_set, "\n");
                          }
                          this_reserve_number_set = reserve_number_set & get_cell_available_set(&board->cells[row][col]);
                          if (reserve_cell(&board->cells[row][col], this_reserve_number_set)) {
                            changed++;
                            if (board->debug_level >= 1)
                              print_reserved_set_for_cell(&board->cells[row][col], "partial match by row");
                          }
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
int solve_eliminate_cols(struct sudoku_board *board)
{
  int row, col, i, j, changed, possibilities, same_index_set_count;
  struct sudoku_cell *cell, *possible_cell;
  unsigned int number, number_set, reserve_number_set, this_reserve_number_set, possible_index_set, index_set;
  unsigned int prior_possible_index_set[9];

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
      if (!(board->col_taken_set[col] & number_set)) {
        possibilities = 0;
        possible_cell = 0;
        possible_index_set = 0;

        // Go through all possible positions for Number
        for (row=0; row<9; row++) {
          cell = &board->cells[row][col];
          // Is this cell free and can we put Number in this cell?
          if ((cell->number == 0) && (get_cell_available_set(cell) & number_set)) {
            possibilities++;
            possible_cell = cell;
            possible_index_set |= NUMBER_TO_SET(row);

            if (board->debug_level >= 3)
              printf("Possible [%i,%i] avail_set: 0x%X <> 0x%X cell_number: %i\n", 
                     cell->row, cell->col, get_cell_available_set(cell), number_set, cell->number);
          }
        }

        // Do we have any possibilities
        if (!possibilities) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(possible_cell, number);
          changed++;
        } else if (possibilities > 1) {
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
                if (board->debug_level >= 2) {
                  printf("solve_eliminate_cols: (complete) [%i,%i] = ", row, col);
                  print_number_set(reserve_number_set, "\n");
                }
                if (reserve_cell(&board->cells[row][col], reserve_number_set)) {
                  changed++;
                  if (board->debug_level >= 1)
                    print_reserved_set_for_cell(&board->cells[row][col], "complete match by col");
                }
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
                          this_reserve_number_set = reserve_number_set & get_cell_available_set(&board->cells[row][col]);
                          if (board->debug_level >= 2) {
                            printf("solve_eliminate_rows: (partial) [%i,%i] = ", row, col);
                            print_number_set(this_reserve_number_set, "\n");
                          }
                          if (reserve_cell(&board->cells[row][col], this_reserve_number_set)) {
                            changed++;
                            if (board->debug_level >= 1)
                              print_reserved_set_for_cell(&board->cells[row][col], "partial match col");
                          }
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


static
int solve_eliminate_tiles_2(struct sudoku_board *board)
{
  int tile, index, i, j, changed, possibilities, same_number_set_count;
  struct sudoku_cell *cell;
  unsigned int possible_number_set, possible_index_set, joint_number_set;
  unsigned int prior_possible_number_set[9];

  changed = 0;
  for (tile=0; tile<9; tile++) {
    if (board->debug_level >= 2) {
      printf("  Tile %i\n", tile);
      if (board->debug_level >= 3)
        print_possible(board);
    }

    for (index=0; index<9; index++) {
      if (board->debug_level >= 2)
        printf("    Index %i\n", index);
      prior_possible_number_set[index] = 0;
      cell = board->tile_ref[tile][index];
      if (cell->number == 0) {
        possible_number_set = get_cell_available_set(cell);
        possibilities = bit_count[possible_number_set];

        // Do we have any possibilities
        if (!possibilities) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(cell, available_set_to_number[possible_number_set]);
          changed++;
        } else if (possibilities > 1) {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_number_set[index] = possible_number_set;
          same_number_set_count = 0;
          possible_index_set = INDEX_TO_SET(index);
          for (i=0; i<index; i++) {
            if (prior_possible_number_set[i] == possible_number_set) {
              same_number_set_count++;
              possible_index_set |= INDEX_TO_SET(i);
            }
          }

          // Do we have other cells with the same possibilies
          if ((same_number_set_count+1) == possibilities) {
            if (reserve_cells_with_index_in_tile(cell, possible_index_set, possible_number_set))
              changed++;
          }

          if (possibilities <= 3) {
            // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
            for (i=0; i<index; i++) {
              // Is there any overlap with a previos set
              if (prior_possible_number_set[i] & possible_number_set) {
                // We have overlap - does this overlap takes us to 3
                joint_number_set = prior_possible_number_set[i] | possible_number_set;
                if (bit_count[joint_number_set] == 3) {
                  // Look for a third that is within this set
                  for (j=(i+1); j<index; j++) {
                    if (prior_possible_number_set[j] && ((joint_number_set | prior_possible_number_set[j]) == joint_number_set)) {
                      // Now we have the three indexes (i, j, index) that go into possible_index_set
                      possible_index_set = INDEX_TO_SET(i) | INDEX_TO_SET(j) | INDEX_TO_SET(index);
                      if (board->debug_level >= 3)
                        printf("solve_eliminate_tiles_2: Reserving cells in row %i with possibilities: %i index_set: %i  number_set: %i\n",
                               cell->row, possibilities, possible_index_set, joint_number_set);
                      if (reserve_cells_with_index_in_tile(cell, possible_index_set, joint_number_set))
                        changed++;
                      break;
                    }
                  }
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
int solve_eliminate_rows_2(struct sudoku_board *board)
{
  int row, col, i, j, changed, possibilities, same_number_set_count;
  struct sudoku_cell *cell;
  unsigned int possible_number_set, possible_index_set, joint_number_set;
  unsigned int prior_possible_number_set[9];

  changed = 0;
  for (row=0; row<9; row++) {
    if (board->debug_level >= 2) {
      printf("  Row %i\n", row);
      if (board->debug_level >= 3)
        print_possible(board);
    }

    for (col=0; col<9; col++) {
      if (board->debug_level >= 2)
        printf("    Col %i\n", col);
      prior_possible_number_set[col] = 0;
      cell = &board->cells[row][col];
      if (cell->number == 0) {
        possible_number_set = get_cell_available_set(cell);
        possibilities = bit_count[possible_number_set];

        // Do we have any possibilities
        if (!possibilities) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(cell, available_set_to_number[possible_number_set]);
          changed++;
        } else if (possibilities > 1) {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_number_set[col] = possible_number_set;
          same_number_set_count = 0;
          possible_index_set = INDEX_TO_SET(col);
          for (i=0; i<col; i++) {
            if (prior_possible_number_set[i] == possible_number_set) {
              same_number_set_count++;
              possible_index_set |= INDEX_TO_SET(i);
            }
          }

          // Do we have other cells with the same possibilies
          if ((same_number_set_count+1) == possibilities) {
            if (reserve_cells_with_index_in_row(cell, possible_index_set, possible_number_set))
              changed++;

          }
          
          if (possibilities <= 3) {
            // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
            for (i=0; i<col; i++) {
              // Is there any overlap with a previos set
              if (prior_possible_number_set[i] & possible_number_set) {
                // We have overlap - does this overlap takes us to 3
                joint_number_set = prior_possible_number_set[i] | possible_number_set;
                if (bit_count[joint_number_set] == 3) {
                  // Look for a third that is within this set
                  for (j=(i+1); j<col; j++) {
                    if (prior_possible_number_set[j] && ((joint_number_set | prior_possible_number_set[j]) == joint_number_set)) {
                      // Now we have the three indexes (i, j, row) that go into possible_index_set
                      possible_index_set = INDEX_TO_SET(i) | INDEX_TO_SET(j) | INDEX_TO_SET(col);
                      if (board->debug_level >= 3)
                        printf("solve_eliminate_rows_2: Reserving cells in row %i with possibilities: %i index_set: %i  number_set: %i\n",
                               cell->row, possibilities, possible_index_set, joint_number_set);
                      if (reserve_cells_with_index_in_row(cell, possible_index_set, joint_number_set))
                        changed++;
                      break;
                    }
                  }
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
int solve_eliminate_cols_2(struct sudoku_board *board)
{
  int row, col, i, j, changed, possibilities, same_number_set_count;
  struct sudoku_cell *cell;
  unsigned int possible_number_set, possible_index_set, joint_number_set;
  unsigned int prior_possible_number_set[9];

  changed = 0;
  for (col=0; col<9; col++) {
    if (board->debug_level >= 2) {
      printf("  Col %i\n", col);
      if (board->debug_level >= 3)
        print_possible(board);
    }

    for (row=0; row<9; row++) {
      if (board->debug_level >= 2)
        printf("    Row %i\n", row);
      prior_possible_number_set[row] = 0;
      cell = &board->cells[row][col];
      if (cell->number == 0) {
        possible_number_set = get_cell_available_set(cell);
        possibilities = bit_count[possible_number_set];

        // Do we have any possibilities
        if (!possibilities) {
          // Board is dead
          set_board_dead(board);
          return 0;
        } else if (possibilities == 1) {
          // We have one and only one possible - set it!
          set_cell_number_and_log(cell, available_set_to_number[possible_number_set]);
          changed++;
        } else if (possibilities > 1) {
          // We have multiple possibilities - any other number with same possibilites so we should reserve the combo
          prior_possible_number_set[row] = possible_number_set;
          same_number_set_count = 0;
          possible_index_set = INDEX_TO_SET(row);
          for (i=0; i<row; i++) {
            if (prior_possible_number_set[i] == possible_number_set) {
              same_number_set_count++;
              possible_index_set |= INDEX_TO_SET(i);
            }
          }

          // Do we have other cells with the same possibilies
          if ((same_number_set_count+1) == possibilities) {
            if (reserve_cells_with_index_in_col(cell, possible_index_set, possible_number_set))
              changed++;
          }

          if (possibilities <= 3) {
            // Now look at partial matches so we can catch {12}, {23}, {13} or {12}, {23}, {123}
            for (i=0; i<row; i++) {
              // Is there any overlap with a previos set
              if (prior_possible_number_set[i] & possible_number_set) {
                // We have overlap - does this overlap takes us to 3
                joint_number_set = prior_possible_number_set[i] | possible_number_set;
                if (bit_count[joint_number_set] == 3) {
                  // Look for a third that is within this set
                  for (j=(i+1); j<row; j++) {
                    if (prior_possible_number_set[j] && ((joint_number_set | prior_possible_number_set[j]) == joint_number_set)) {
                      // Now we have the three indexes (i, j, row) that go into possible_index_set
                      possible_index_set = INDEX_TO_SET(i) | INDEX_TO_SET(j) | INDEX_TO_SET(row);
                      if (board->debug_level >= 3)
                        printf("solve_eliminate_cols_2: Reserving cells in row %i with possibilities: %i index_set: %i  number_set: %i\n",
                               cell->row, possibilities, possible_index_set, joint_number_set);
                      if (reserve_cells_with_index_in_col(cell, possible_index_set, joint_number_set))
                        changed++;
                      break;
                    }
                  }
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


int solve_eliminate(struct sudoku_board *board)
{
  int changed, total_changed, round;

  if (board->debug_level)
    printf("Solve eliminate\n");
  total_changed = 0;
  round = 0;

  do {
    if (board->debug_level >= 2)
      printf("  Round %i:\n", round);
    round++;

    changed = solve_eliminate_tiles(board);
    if (!is_board_dead(board))
      changed += solve_eliminate_rows(board);
    if (!is_board_dead(board))
      changed += solve_eliminate_cols(board);
    if (!is_board_dead(board))
      changed += solve_eliminate_rows_2(board);
    if (!is_board_dead(board))
      changed += solve_eliminate_cols_2(board);
    if (!is_board_dead(board))
      changed += solve_eliminate_tiles_2(board);

    total_changed += changed;
  } while ((changed > 0) && (!is_board_dead(board)));

  return total_changed;
}


static inline
void solve_hidden_cell(struct sudoku_cell *cell)
{
  unsigned int number, available_set;
  struct sudoku_board *future_board;

  available_set = get_cell_available_set(cell);
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


void solve_hidden(struct sudoku_board *board)
{
  struct sudoku_cell *cell;
  struct sudoku_board *tmp;

  if (board->debug_level) {
    printf("Solve hidden\n");
    print_board(board);
    print_possible(board);
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
  int changed, solutions_count;

  changed = solve_possible(board);

  if (!is_board_solved(board))
    changed += solve_eliminate(board);

  if (!is_board_dead(board) && !is_board_solved(board))
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
    printf(" }  %s", postfix);
  else
    printf(" }");
}

static
void print_reserved_set_for_cell(struct sudoku_cell *cell, const char *comment)
{
  printf("    [%i,%i]  =  ", cell->row, cell->col);
  print_number_set(cell->reserved_for_number_set, comment);
  printf("\n");
}


static
void print_possible(struct sudoku_board *board)
{
  int row, col, i;
  struct sudoku_cell *cell;
  unsigned int possible_set, taken_set, available_set, reserved_set;

  for (row=0; row<9; row++) {
    for (col=0; col<9; col++) {
      cell = &board->cells[row][col];

      if (cell->number == 0) {

        printf("[%i,%i] Possible: ", row, col);
        possible_set = get_cell_available_set(cell) >> 1;
        for (i=0; i<9; i++) {
          if (possible_set & 1)
            printf("%i ", i+1);
          possible_set >>= 1;
        }

        taken_set = *cell->row_taken_set_ref;
        taken_set |= *cell->col_taken_set_ref;
        taken_set |= *cell->tile_taken_set_ref;

        available_set = TAKEN_TO_AVAIL_SET(taken_set) >> 1;

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
      print_possible(board);
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
        if (get_cell_available_set(&board->cells[row][col]) & NUMBER_TO_SET(number)) {
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
