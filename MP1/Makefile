CXXFLAGS = -std=c++11 -O2

main: main.cpp list_file.c list_file.h
	g++ $(CXXFLAGS) -lcrypto main.cpp list_file.c -o loser

.PHONY: clean
clean: 
	rm loser
