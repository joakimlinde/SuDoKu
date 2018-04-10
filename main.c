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
#include <unistd.h>
#include <errno.h>
#include "sudoku.h"


#define BUFFER_SIZE 10000


struct options {
  int verbose_level;
  int quiet_mode;
  int pretty_print; 
  int print_latex;
  int print_help;
  int run_builtin_test;
  char *input_file_name;
  char *output_file_name;
};


static
int run_from_file(const char *file_name, struct options *options)
{
  FILE *f;
  long fsize;
  struct sudoku_board *board;
  char *input_str;
  int solutions_count;

  f = fopen(file_name, "rb");
  if (!f) {
    fprintf(stderr, "Cound not open file: %s\n", file_name);
    return -1;
  }

  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  input_str = (char*)malloc(fsize + 1);
  fread(input_str, fsize, 1, f);
  fclose(f);

  board = create_board();
  board->debug_level = options->verbose_level;
  read_board(board, input_str);

  if (options->verbose_level) {
    printf("-------- Input --------\n");
    print_board(board);
    if (options->print_latex) 
      print_board_latex(board);
    printf("---Solve---\n");
  }
  
  solutions_count = solve(board);
  
  if (options->verbose_level) {
    if (solutions_count)
      printf("Found %i solution(s)\n", solutions_count);
    else
      printf("No solution found\n");
    printf("-------- Output -------\n");
    print_solutions(board);
    printf("\n");    
  }

  if (options->pretty_print) 
    print_board(board);
  else if (options->print_latex) 
    print_board_latex(board);
  else
    print_board_simple(board);

  destroy_board(&board);
  free(input_str);

  return 0;
}


static
int run_stdio(struct options *options)
{
  struct sudoku_board *board;
  char *buffer, *line;
  size_t line_size;
  int buffer_bytes, chars_read, solutions_count;

  buffer = (char*)malloc(BUFFER_SIZE);
  buffer_bytes = 0;

  line = buffer;
  line_size = BUFFER_SIZE;

  while ((chars_read = getline(&line, &line_size, stdin)) != -1) {
    if ((chars_read) && (line[0] != '#') && (line[0] != ';') && (line[0] != '!')) {
      line += chars_read;
      line_size -= chars_read;
      buffer_bytes += chars_read;
    }
  }
  buffer[buffer_bytes] = 0;

  board = create_board();
  read_board(board, buffer);

  board->debug_level = options->verbose_level;
  if (options->verbose_level) {
    printf("-------- Input --------\n");
    print_board(board);
    if (options->print_latex) 
      print_board_latex(board);
  }

  solutions_count = solve(board);

  if (options->verbose_level) {
    printf("-------- Output -------\n");
    print_solutions(board);
    printf("\n");     
  }

  if (options->pretty_print) 
    print_board(board);
  else if (options->print_latex) 
    print_board_latex(board);
  else
    print_board_simple(board);
  
  destroy_board(&board);
  free(buffer);

  return solutions_count;
}


static
int run_batch_from_file(struct options *options)
{
  FILE *fin, *fout;
  char *line = NULL;
  size_t line_size = BUFFER_SIZE;
  struct sudoku_board *board;
  int chars_read, solutions_count, total_solved, total_unsolved;

  fin = fopen(options->input_file_name, "r");
  if (!fin) {
    fprintf(stderr, "Cound not open input file: %s\n", options->input_file_name);
    return -1;
  }

  fout = NULL;
  if (options->output_file_name) {
    fout = fopen(options->output_file_name, "w");
    if (!fout) {
      fprintf(stderr, "Cound not open output file: %s\n", options->output_file_name);
      fclose(fin);
      return -1;
    }
  }

  total_solved = 0;
  total_unsolved = 0;
  line = (char*)malloc(line_size);
  while ((chars_read = getline(&line, &line_size, fin)) != -1) {
    if (chars_read >= ((8*8)+1) && (line[0] != '#') && (line[0] != ';') && (line[0] != '!')) {
      board = create_board();
      read_board(board, line);
      
      board->debug_level = options->verbose_level;
      if (options->verbose_level) {
        printf("-------- Input --------\n");
        print_board(board);
        printf("--- Solve---\n");
      }

      solutions_count = solve(board);
      
      if (solutions_count){     
        if (options->verbose_level)
          printf("Found %i solution(s)\n", solutions_count);
        total_solved++;
      } else {
        if (options->verbose_level)
          printf("No solution found\n");
        total_unsolved++;
      }

      if (options->verbose_level) {
        printf("-------- Output -------\n");
        print_solutions(board);
        printf("\n=====================\n\n");
      }
  
      if (fout)
        print_board_line(fout, board);

      destroy_board(&board);
    }
  }

  if (!options->quiet_mode) {
    if (total_solved + total_unsolved)
      printf("Number of solved: %i  Number of unsolved: %i\n", total_solved, total_unsolved);
    else
      printf("No pussles found in file: %s\n", options->input_file_name);    
  }

  fclose(fin);
  if (fout)
    fclose(fout);
  free(line);

  return 0;
}


static 
void print_legal() 
{
  printf("Copyright (c) 2018  Linde Labs, LLC\n"
         "This program comes with ABSOLUTELY NO WARRANTY.\n"
         "This is free software, and you are welcome to redistribute it\n"
         "under certain conditions; see LICENSE file for details.\n" );
}

static
int parse_argument(int argc, char **argv, struct options *options)
{
  char *dummy;
  int c, status;
  long value;

  status = 0;
  options->verbose_level = 0;
  options->quiet_mode = 0;
  options->pretty_print = 0;
  options->print_latex = 0;
  options->print_help = 0;
  options->run_builtin_test  = 0;
  options->input_file_name = NULL;
  options->output_file_name = NULL;

  opterr = 0;
  while ((c = getopt(argc, argv, "vqd:xho:f:pt")) != -1) {
    switch (c) {
      case 'v':
        options->verbose_level = 1;
        break;

      case 'q':
        options->quiet_mode = 1;
        break;

      case 'd':
        options->verbose_level = 1;
        if (optarg) {
          value  = strtol(optarg, &dummy, 10);
          if ((errno != ERANGE) && (value > 0))
            options->verbose_level = value;
        }
        break;

      case 'x':
        options->print_latex = 1;
        break;

      case 'o':
        options->output_file_name = optarg;
        break;

      case 'f':
        options->input_file_name = optarg;
        break;

      case 'p':
        options->pretty_print = 1;
        break;

      case 't':
        options->run_builtin_test = 1;
        break;

      case 'h':
        options->print_help = 1;
        break;
        
      case '?':
        fprintf(stderr, "Unknown option -%c. Use -h for help.\n", optopt);
        return 1;

      case ':':
        if ((optopt == 'f') || (optopt == 'o')) 
          fprintf(stderr, "Option -%c without filename. Use -h for help.\n", optopt);
        else if (optopt == 'd') 
          fprintf(stderr, "Option -%c without level. Use -h for help.\n", optopt);
        return 1;

      default:
        return 1;
    }
  }

  if (options->quiet_mode && options->verbose_level) {
    fprintf(stderr, "Option -v and -q can't be used together. Use -h for help.\n");
    return 1;
  }

  if (options->output_file_name && !options->input_file_name) {
    fprintf(stderr, "Option -o filename can't be given without -f filename. Use -h for help.\n");
    return 1;
  }

  if (options->input_file_name && (argc > optind)) {
    fprintf(stderr, "Option -f can't be used with arguments. Use -h for help.\n");
    return 1;
  }

  if (options->print_help) {
    printf("Usage: sudoku [options] [<file> ...]\n");
    printf("  Solve Sudoku in file(s) <file> or stdin if no file(s) given.\n"); 
    printf("  Batch solve Sudokus using -f <filename> and -o <filename> with one Sudoku per line.\n");
    printf("Options:\n");
    printf("  -h    Help\n");
    printf("  -v    Verbose\n");
    printf("  -q    Quiet mode\n");
    printf("  -a    Find all solutions not just the first\n");
    printf("  -f <filename>  Input file with one Sudoku per line\n");
    printf("  -o <filename>  Output file with one Sudoku per line\n");
    printf("  -p    Pretty print Sudoku instead of just numbers\n");
    printf("  -x    Print latex code for Sudoku\n");
    printf("  -d <level>  Turn on debug level\n");
    printf("  -t    Run built-in tests\n");
    print_legal();
  }

  return 0;
}

int main(int argc, char **argv)
{
  struct options options;
  char *file_name;
  int index, status;

  // Get the arguments
  status = parse_argument(argc, argv, &options);
  if (status)
    return status;
  if (options.print_help)
    return 0;
  if (options.verbose_level || options.run_builtin_test)
    print_legal();

  init();
  status = 0;

  // If we got an -t then go with that
  if (options.run_builtin_test)
    return run_built_in_tests(&options);

  // If we got an -i then go with that first
  if (options.input_file_name)
    status = run_batch_from_file(&options);

  // Loop over all the file name arguments
  file_name = NULL;
  for (index = optind; (index < argc) && (status == 0); index++) {
    file_name = argv[index];
    status = run_from_file(file_name, &options);
  }

  // Still no input, go with stdio
  if ((file_name == NULL) && (options.input_file_name == NULL))
    status = run_stdio(&options);    
  
  return status;
};
