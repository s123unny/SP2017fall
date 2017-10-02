#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include "list_file.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <openssl/md5.h>

bool myfunction (const char* i, const char* j) { return strcmp(i, j) < 0; }

void free_pointers(char *pointers[], int length) {
	for (int i = 0; i < length; i++) {
		free(pointers[i]);
	}
}
void get_file_path(char dest[256], const char dir[],const char file[]) {
	strcpy(dest, dir);
	char *temp = dest + strlen(dest) + 1;
	dest[strlen(dest)] = '/';
	strcpy(temp, file);
}

void MD5_generator(const char file[256], char md5string[33], const char path[]) {
	char command[300] = "md5sum ", postfix[] = " | cut -d ' ' -f 1";
	char file_path[256];
	get_file_path(file_path, path, file);
	strcat(command, file_path);
	strcat(command, postfix);
	FILE *fp = popen(command, "r");
	fread(md5string, sizeof(char)*32, 1, fp);
	md5string[32] = '\0';
	//printf("	%s\n", md5string);
	pclose(fp);
}

int store_in_line(char *line[], int record_fp, char stop) {
	char c[2];
	int num = 1, line_index = 0;

	lseek(record_fp, -2, SEEK_END);
	read(record_fp, c, sizeof(char));
	while (c[0] != stop) {
		lseek(record_fp, -2, SEEK_CUR);
		read(record_fp, c, sizeof(char));
		num ++;
		if (c[0] == '\n') {
			line[line_index] = (char *)malloc(sizeof(char) * (num));
			read(record_fp, line[line_index], sizeof(char)*(num));
			line[line_index][num-1] = '\0';
			lseek(record_fp, -num, SEEK_CUR);
			line_index ++;
			num = 0;
		}
	}
	/*for (int i = 0; i < line_index; i++) {
		printf("%s\n", line[i]);
	}*/
	return line_index;
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
			//qsort(file_names.names, file_names.length, sizeof(*char), compare);
			std::sort(file_names.names, file_names.names + file_names.length, myfunction);
			//delete . and .. (from 2)
			for (int i = 2; i < file_names.length; i++) {
				printf("%s\n", file_names.names[i]);
			}
			free_file_names(file_names);
			printf("[modified]\n[copied]\n");
		} else {
			int new_file[1000], modified[1000], copied[1000], nf = 0, mo = 0, co = 0;
			char *copyfrom[1000];
			struct FileNames file_names = list_file(argv[2]);
			std::sort(file_names.names, file_names.names + file_names.length, myfunction);
			//remember delete . and ..
			
			char *line[file_names.length];
			int total_line = store_in_line(line, record_fp, ')');
			int line_index = total_line -1;

			for (int i = 2; i < file_names.length; i++) {
				//match
				if (strstr(line[line_index], file_names.names[i]) != NULL) {
					//exist -> check if modified
					//printf("found: %s\n", file_names.names[i]);
					char new_md5[33], before_md5[33];

					MD5_generator(file_names.names[i], new_md5, argv[2]);
					char *start = strchr(line[line_index], ' ');
					start ++;
					strcpy(before_md5, start);
					if (strcmp(new_md5, before_md5) != 0) {
						modified[mo] = i;
						//printf("%d modified %d %s\n", mo, i, file_names.names[modified[mo]]);
						mo ++;
					}
					line_index --;
				} else {
					//new or copy
					char new_md5[33];
					MD5_generator(file_names.names[i], new_md5, argv[2]);
					int found = 0;
					for (int j = 0; j < total_line; j++) {
						char *start = strstr(line[j], new_md5);
						if (start != NULL) {
							copied[co] = i;
							copyfrom[co] = (char *)malloc(sizeof(char) * 17);
							strncpy(copyfrom[co], line[j], start - line[j] - 1);
							//printf("%d copied %d %s\n", co, i, file_names.names[copied[co]]);
							co ++;
							found = 1;
							break;
						}
					}
					if (!found) {
						new_file[nf] = i;
						//printf("%d new_file %d %s\n", nf, i, file_names.names[new_file[nf]]);
						nf ++;
					}
				}
			}
			/*for (size_t i = 0; i < file_names.length; i++) {
				printf("%d %s\n",i, file_names.names[i]);
			}*/
			printf("[new_file]\n");
			for (int i = 0; i < nf; i++) {
				printf("%s\n", file_names.names[new_file[i]]);
			}
			printf("[modified]\n");
			for (int i = 0; i < mo; i++) {
				printf("%s\n", file_names.names[modified[i]]);
			}
			printf("[copied]\n");
			for (int i = 0; i < co; i++) {
				printf("%s => %s\n", copyfrom[i], file_names.names[copied[i]]);
			}
			free_pointers(copyfrom, co);
			free_pointers(line, total_line);
			free_file_names(file_names);
		}
		break;
	/*case 'c': //commit
		break;
	case 'l': //log
		break;*/
	}
	return 0;
}
