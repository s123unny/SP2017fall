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
#include <unordered_map>
#include <string>

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
	char record_path[256], name[] = ".loser_record";
	int record_fp;
	switch(argv[1][0]) {
	case 's': //status
		get_file_path(record_path, argv[2], name);
		record_fp = open(record_path, O_RDONLY);
		lseek(record_fp, -2, SEEK_END);
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
			std::unordered_map<std::string, std::string> line_in_map;
			for (int i = 0; i < total_line; i++) {
				std::string md5, filename = line[i];
				md5 = md5.assign(filename, filename.find(" ", 0)+1, filename.length());
				filename.erase(filename.find(' ', 0));
				printf("%s==%s\n", md5.c_str(), filename.c_str());
				line_in_map.emplace(md5, filename);
			}

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
					std::unordered_map<std::string,std::string>::iterator found = line_in_map.find(new_md5);
					if (found == line_in_map.end()) {
						new_file[nf] = i;
						nf ++;
					} else {
						copied[co] = i;
						copyfrom[co] = (char *)malloc(sizeof(char) * 256);
						strcpy(copyfrom[co], found->second.c_str());
						co ++;
					}
				}
			}
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
		break;*/
	case 'l': //log
		get_file_path(record_path, argv[3], name);
		int len = strlen(argv[2]), time = 0;
		for (int i = 0; i < len; i++) {
			time *= 10;
			time += argv[2][i] - '0';
		}
		record_fp = open(record_path, O_RDONLY);
		lseek(record_fp, -2, SEEK_END);
		if (record_fp != -1) {
			while (time--) {
				//if time greater than commit number?
				char *line[2010];
				int total_line = store_in_line(line, record_fp, '#');
				char commit_line[20] = "#";
				int i = 0;
				while (commit_line[i] != '\n') {
					i++;
					read(record_fp, commit_line + i, sizeof(char));
				}
				commit_line[i] = '\0';
				printf("%s\n", commit_line);
				for (int i = total_line-1; i >= 0; i--) {
					printf("%s\n", line[i]);
				}
				free_pointers(line, total_line);
				off_t temp = lseek(record_fp, -strlen(commit_line)-5, SEEK_CUR);
				if (temp < 0) {
					break;
				}
				if (time)printf("\n");
			}
		}
		break;
	}
	return 0;
}
