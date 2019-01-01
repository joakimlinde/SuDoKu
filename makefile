CC = cc
CCFLAGS = -Ofast -Wall -Wno-unused-function -DNDEBUG
EXE = sudoku
OBJS = main.o board.o solve.o test.o

$(EXE) : $(OBJS)
	$(CC) $(CCFLAGS) $^ -o $@

main.o : main.c sudoku.h
	$(CC) $(CCFLAGS) -c $<

board.o : board.c sudoku.h
	$(CC) $(CCFLAGS) -c $<

solve.o : solve.c sudoku.h
	$(CC) $(CCFLAGS) -c $<

test.o : test.c sudoku.h
	$(CC) $(CCFLAGS) -c $<

# Phony

.PHONY: all
all : $(EXE) 

.PHONY: clean
clean : 
	rm -f $(OBJS) $(EXE)

.PHONY: run
run : $(EXE)
	./$(EXE)

.PHONY: test
test : $(EXE)
	./$(EXE) -t
