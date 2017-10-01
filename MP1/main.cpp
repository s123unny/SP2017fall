#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include "list_file.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

bool myfunction (const char* i, const char* j) { return strcmp(i, j) < 0; }

void get_file_path(char dest[256], const char dir[], char file[]) {
	strcpy(dest, dir);
	char *temp = dest + strlen(dest) + 1;
	dest[strlen(dest)] = '/';
	strcpy(temp, file);
}

int main(int argc, char const *argv[])
{
	//haven't done rename function yet
	switch(argv[1][0]) {
	case 's': //status
		char record_path[256], name[] = ".loser_record";
		get_file_path(record_path, argv[2], name);
		int record_fp = open(record_path, O_RDONLY);
		if (record_fp < 0) {
			printf("[new_file]\n");
			//print all file except . .. and follow dictionary order
			struct FileNames file_names = list_file(argv[2]);
			std::sort(file_names.names, file_names.names + file_names.length, myfunction);
			//delete . and .. (from 2)
			for (size_t i = 2; i < file_names.length; i++) {
				printf("%s\n", file_names.names[i]);
			}
			free_file_names(file_names);
			printf("[modified]\n[copied]\n");
		} else {
			
			printf("[new_file]\n");
			printf("[modified]\n");
			printf("[copied]\n");
		}
		break;
	/*case 'c': //commit
		break;
	case 'l': //log
		break;*/
	}
	return 0;
}
