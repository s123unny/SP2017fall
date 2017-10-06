#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include "list_file.h"
//#include <sys/types.h>
//#include <unistd.h>
//#include <sys/stat.h>
//#include <fcntl.h>
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

int store_in_line(char *line[], FILE *record_fp, char stop) {
    char c;
    int num = 1, line_index = 0;

    fscanf(record_fp, "%c", &c);
    while (c != stop) {
        fseek(record_fp, -2, SEEK_CUR);
        fscanf(record_fp, "%c", &c);
        num ++;
        if (c == '\n') {
            line[line_index] = (char *)malloc(sizeof(char) * (num+1));
            fgets(line[line_index], num+1, record_fp);
            line[line_index][num-1] = '\0';
            //printf("==%s==\n", line[line_index]);
            fseek(record_fp, -num, SEEK_CUR);
            line_index ++;
            num = 0;
        }
    }
    /*for (int i = 0; i < line_index; i++) {
            printf("%s\n", line[i]);
    }*/
    return line_index;
}

void status_commit(char const from_argv2[], int type, char record_path[]) {
	FILE *record_fp = fopen(record_path, "r+");
	struct FileNames file_names = list_file(from_argv2);
	std::sort(file_names.names, file_names.names + file_names.length, myfunction);
	if (record_fp == NULL) {
		//print all file except . .. and follow dictionary order
		if (type) {
			printf("[new_file]\n");
			//delete . and .. (from 2)
			for (int i = 2; i < file_names.length; i++) {
				printf("%s\n", file_names.names[i]);
			}
			printf("[modified]\n[copied]\n");
		} else {
			//create file
			record_fp = fopen(record_path, "w");
			//write newfle & md5
			fprintf(record_fp, "# commit 1\n[new_file]\n");
			for (int i = 2; i < file_names.length; i++) {
				fprintf(record_fp, "%s\n", file_names.names[i]);
			}
			fprintf(record_fp, "[modified]\n[copied]\n(MD5)\n");
			for (int i = 2; i < file_names.length; i++) {
				char new_md5[33];
				MD5_generator(file_names.names[i], new_md5, from_argv2);
				fprintf(record_fp, "%s %s\n", file_names.names[i], new_md5);
			}
		}
		free_file_names(file_names);
	} else {
		fseek(record_fp, -2, SEEK_END);
		int new_file[1000], modified[1000], copied[1000], nf = 0, mo = 0, co = 0;
		char *copyfrom[1000];
		//remember delete . and ..
		
		char all_md5[file_names.length][33]; //for commit
		char *line[file_names.length];
		int total_line = store_in_line(line, record_fp, ')');
		int line_index = total_line -1;
		std::unordered_map<std::string, std::string> line_in_map;
		for (int i = 0; i < total_line; i++) {
			std::string md5, filename = line[i];
			md5 = md5.assign(filename, filename.find(" ", 0)+1, filename.length());
			filename.erase(filename.find(' ', 0));
			//printf("%s==%s\n", md5.c_str(), filename.c_str());
			line_in_map.emplace(md5, filename);
		}

		for (int i = 2; i < file_names.length; i++) {
			//match
			if (strstr(line[line_index], file_names.names[i]) != NULL) {
				//exist -> check if modified
				//printf("found: %s\n", file_names.names[i]);
				char new_md5[33], before_md5[33];

				MD5_generator(file_names.names[i], new_md5, from_argv2);
				char *start = strchr(line[line_index], ' ');
				start ++;
				strcpy(before_md5, start);
				//printf("%s==\n", before_md5);
				if (strcmp(new_md5, before_md5) != 0) {
					modified[mo] = i;
					//printf("%d modified %d %s\n", mo, i, file_names.names[modified[mo]]);
					mo ++;
				}
				if (!type) {
					strcpy(all_md5[i], new_md5);
				}
				line_index --;
			} else {
				//new or copy
				if (strcmp(file_names.names[i], ".loser_record") != 0) {
					char new_md5[33];
					MD5_generator(file_names.names[i], new_md5, from_argv2);
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
					if (!type) {
						strcpy(all_md5[i], new_md5);
					}
				}
			}
		}
		if (type) {
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
		} else {
			//find which commit
			char commit_num[20] = "x";
			int i = 0, num = 0;
			while (commit_num[0] != '#') {
				fseek(record_fp, -2, SEEK_CUR);
				fscanf(record_fp, "%c", commit_num);
			}
			while (commit_num[i] != '\n') {
				i++;
				fscanf(record_fp, "%c", commit_num + i);
			}
			i --;
			while (commit_num[i] != ' ') {
				num *= 10;
				num += commit_num[i] - '0';
				i --;
			}
			num ++;
			fseek(record_fp, 0, SEEK_END);
			fprintf(record_fp, "\n# commit %d\n[new_file]\n", num);
			for (int i = 0; i < nf; i++) {
				fprintf(record_fp, "%s\n", file_names.names[new_file[i]]);
			}
			fprintf(record_fp, "[modified]\n");
			for (int i = 0; i < mo; i++) {
				fprintf(record_fp, "%s\n", file_names.names[modified[i]]);
			}
			fprintf(record_fp, "[copied]\n");
			for (int i = 0; i < co; i++) {
				fprintf(record_fp, "%s => %s\n", copyfrom[i], file_names.names[copied[i]]);
			}
			fprintf(record_fp, "(MD5)\n");
			for (int i = 2; i < file_names.length; i++) {
				if (strcmp(file_names.names[i], ".loser_record") != 0) {
					fprintf(record_fp, "%s %s\n", file_names.names[i], all_md5[i]);
				}
			}
		}
		free_pointers(copyfrom, co);
		free_pointers(line, total_line);
		free_file_names(file_names);
	}
}

char feature_of_config(FILE *fp, char const from_argv2[]) {
	int found = 0;
	char new_name[260], type;
	while(!found) {
		fscanf(fp, "%s", new_name);
		if (strcmp(new_name, from_argv2) == 0) {
			found = 1;
			fscanf(fp, " = %c", &type);
		}
		fscanf(fp, " = %s", new_name);
	}
	return type;
}

int main(int argc, char const *argv[])
{
	//haven't done rename function yet->done
	char record_path[256], name[] = ".loser_record";
	char type;
	if (strcmp(argv[1], "status") == 0 || strcmp(argv[1], "log") == 0 || strcmp(argv[1], "commit") == 0) {
		type = argv[1][0];
	} else {
		char config_path[256], config[] = ".loser_config";
		get_file_path(config_path, argv[3], config);
		FILE *fp = fopen(config_path, "r");
		//if (fp == NULL) printf("QQQ\n");
		type = feature_of_config(fp, argv[1]);
	}
	switch(type) {
	case 's': //status
		get_file_path(record_path, argv[2], name);
		status_commit(argv[2], 1, record_path);
		break;
	case 'c': //commit
		get_file_path(record_path, argv[2], name);
		status_commit(argv[2], 0, record_path);
		break;
	case 'l': //log
		get_file_path(record_path, argv[3], name);
		int len = strlen(argv[2]), time = 0;
		for (int i = 0; i < len; i++) {
			time *= 10;
			time += argv[2][i] - '0';
		}
		FILE *record_fp = fopen(record_path, "r");
		if (record_fp != NULL) {
			fseek(record_fp, -2, SEEK_END);
			while (time--) {
				//if time greater than commit number?
				char *line[2010];
				int total_line = store_in_line(line, record_fp, '#');
				char commit_line[20];
				int num;
				fscanf(record_fp, " %s %d", commit_line, &num);
				printf("# %s %d\n", commit_line, num);
				for (int i = total_line-1; i >= 0; i--) {
					printf("%s\n", line[i]);
				}
				free_pointers(line, total_line);
				off_t temp = fseek(record_fp, -strlen(commit_line)-7, SEEK_CUR);
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
