.PHONY: all clean

CXXFLAGS=-O2 -lpthread -ldl

all: inf-bonbon-server

inf-bonbon-server: server.c cJSON/cJSON.c
	g++ -o inf-bonbon-server $(CXXFLAGS) server.c cJSON/cJSON.c

clean:
	rm -f inf-bonbon-server
	rm -f client/*
