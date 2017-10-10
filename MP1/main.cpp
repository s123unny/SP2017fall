#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include "list_file.h"
#include <openssl/md5.h>
#include <unordered_map>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

bool myfunction (const char* i, const char* j) { return strcmp(i, j) < 0; }

void get_file_path(char dest[256], const char dir[],const char file[]) {
	strcpy(dest, dir);
	char *temp = dest + strlen(dest) + 1;
	dest[strlen(dest)] = '/';
	strcpy(temp, file);
}
unsigned long get_size_by_fd(int fd) {
    struct stat statbuf;
    if(fstat(fd, &statbuf) < 0) exit(-1);
    return statbuf.st_size;
}

void MD5_generator(const char file[256], char md5string[33], const char path[]) {
	/*char command[300] = "md5sum ";
	char file_path[256];
	get_file_path(file_path, path, file);
	strcat(command, file_path);
	FILE *fp = popen(command, "r");
	fread(md5string, sizeof(char), 32, fp);
	md5string[32] = '\0';
	//printf("	%s\n", md5string);
	pclose(fp);*/
	unsigned char result[MD5_DIGEST_LENGTH];
	char file_path[256];
	get_file_path(file_path, path, file);
	unsigned long file_size;
    void* file_buffer;
	int file_descript = open(file_path, O_RDONLY);

	file_size = get_size_by_fd(file_descript);

    file_buffer = mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0);
    MD5((unsigned char*) file_buffer, file_size, result);
    munmap(file_buffer, file_size); 
    for(int i=0; i <MD5_DIGEST_LENGTH; i++) {
    	sprintf(&md5string[i*2], "%02x", (unsigned int)result[i]);
    }
}

void status_commit(char const from_argv2[], int type, char record_path[]) {
	FILE *record_fp = fopen(record_path, "r+");
	struct FileNames file_names = list_file(from_argv2);
	std::sort(file_names.names, file_names.names + file_names.length, myfunction);
	if (record_fp == NULL) {
		if (type) {
			record_fp = stdout;
		} else {
			if (file_names.length == 2) {
				free_file_names(file_names);
				return;
			}
			//create file
			record_fp = fopen(record_path, "w");
			fprintf(record_fp, "# commit 1\n");
		}
		//print all except . & ..
		fprintf(record_fp, "[new_file]\n");
		for (int i = 2; i < file_names.length; i++) {
			fprintf(record_fp, "%s\n", file_names.names[i]);
		}
		fprintf(record_fp, "[modified]\n[copied]\n");
		if (!type) {
			//write md5
			fprintf(record_fp, "(MD5)\n");
			for (int i = 2; i < file_names.length; i++) {
				char new_md5[33];
				MD5_generator(file_names.names[i], new_md5, from_argv2);
				fprintf(record_fp, "%s %s\n", file_names.names[i], new_md5);
			}
		}
		free_file_names(file_names);
	} else {
		fseek(record_fp, -1, SEEK_END);
		int new_file[file_names.length], modified[file_names.length], copied[file_names.length], nf = 0, mo = 0, co = 0;
		char copyfrom[file_names.length][256];
		
		char all_md5[file_names.length][33]; //for commit
		std::unordered_map<std::string, std::string> line_in_map;
		char line_filename[file_names.length+1][256], line_md5[file_names.length][33];
		std::string md5, filename;

		long pos =  ftell(record_fp);
	    pos = std::min(pos, (long)550000);
	    char buffer[pos+1];
	    fseek(record_fp, -pos, SEEK_CUR);
	    fread(buffer, sizeof(char), pos, record_fp);
	    buffer[pos] = '\0';

	    char *start = strrchr(buffer, ')'), *commit_start;
	    start += 2;

	    int offset, line_index = 0;
		while (sscanf(start, "%s%s%n", line_filename[line_index], line_md5[line_index], &offset) == 2) {
			start += offset;
			filename = line_filename[line_index];
			md5 = line_md5[line_index];
			//printf("%s==%s\n", temp.c_filename, temp.c_md5);
			line_in_map.emplace(md5, filename);
			line_index ++;
		}
		line_index = 0;
		for (int i = 2; i < file_names.length; i++) {
			if (strcmp(file_names.names[i], ".loser_record") != 0) {
				char new_md5[33];
				MD5_generator(file_names.names[i], new_md5, from_argv2);
				//match
				if (strcmp(line_filename[line_index], file_names.names[i]) == 0) {
					//exist -> check if modified
					if (strcmp(new_md5, line_md5[line_index]) != 0) {
						modified[mo] = i;
						mo ++;
					}
					line_index ++;
				} else {
					//new or copy
					std::unordered_map<std::string,std::string>::iterator found = line_in_map.find(new_md5);
					if (found == line_in_map.end()) {
						new_file[nf] = i;
						nf ++;
					} else {
						copied[co] = i;
						strcpy(copyfrom[co], found->second.c_str());
						co ++;
					}
				}
				if (!type) {
					strcpy(all_md5[i], new_md5);
				}
			}
		}
		if (!type) {
			//find which commit
			if (nf == 0 && mo == 0 && co == 0) {
				free_file_names(file_names);
				return;
			}
			start = strrchr(buffer, '#');
			start = strchr(start+2, ' '); //commit_start is the pointer to #
			start ++;
			int num = 0;
			while (*start != '\n') {
				num *= 10;
				num += *start - '0';
				start ++;
			}
			num ++;
			fseek(record_fp, 0, SEEK_END);
			fprintf(record_fp, "\n# commit %d\n", num);
		} else {
			fclose(record_fp);
			record_fp = stdout;
		}
		char buf[6000];//print into buffer first to performance better
		setvbuf(record_fp, buf, _IOFBF, sizeof(buf));
		
		fprintf(record_fp, "[new_file]\n");
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
		if (!type) {
			fprintf(record_fp, "(MD5)\n");
			for (int i = 2; i < file_names.length; i++) {
				if (strcmp(file_names.names[i], ".loser_record") != 0) {
					fprintf(record_fp, "%s %s\n", file_names.names[i], all_md5[i]);
				}
			}
			fclose(record_fp);
		}
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
		get_file_path(config_path, argv[argc-1], config);
		FILE *fp = fopen(config_path, "r");
		//if (fp == NULL) printf("QQQ\n");
		type = feature_of_config(fp, argv[1]);
		fclose(fp);
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
			char buffer[550000];
			fseek(record_fp, 0, SEEK_END);
			while (time--) {
				long pos = ftell(record_fp), length = 1024;
			    pos = std::min(pos, length);
			    fseek(record_fp, -pos, SEEK_CUR);
			    fread(buffer, sizeof(char), pos, record_fp);
			    buffer[pos] = '\0';

			    char *start = strrchr(buffer, '#');
			    while (start == NULL) {
			    	pos =  ftell(record_fp);
			    	length += 1024;
			    	pos = std::min(pos, length);
			    	fseek(record_fp, -pos, SEEK_CUR);
			    	fread(buffer, sizeof(char), pos, record_fp);
			    	buffer[pos] = '\0';
			    	start = strrchr(buffer, '#');
			    }
			    printf("%s", start);
			    
			    off_t temp = fseek(record_fp, -strlen(start)-1, SEEK_CUR);
			    if (temp < 0) {
					break;//if time greater than commit number? ->done
				}
				if (time) printf("\n");
			}
		}
		fclose(record_fp);
		break;
	}
	return 0;
}
