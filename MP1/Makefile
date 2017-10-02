CXXFLAGS = -std=c++11 -O2 -Wall -fsanitize=undefined

main: main.cpp list_file.c list_file.h
	g++ $(CXXFLAGS) main.cpp list_file.c -o loser

.PHONY: clean
clean: 
	rm loser
