#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <map>
#include <vector>
#include <queue>
#include <setjmp.h>
#include <sys/mman.h>

#include "cJSON/cJSON.h"
#define BufferSize 6144
#define max(a,b) (a > b? a:b)

enum status{
	FREE 		= 0x00,
	MATCHING 	= 0x01,
	CHATTING	= 0x02
};

struct User{
	char name[33];
	unsigned int age;
	char gender[7];
	char introduction[1025];
};

struct Fd_Info{
	char buffer[BufferSize];
	unsigned char status;
	int partner;
	struct User user_info;
	char filter_function[4097];
	char file_so[36];
};

struct Fork_Info{
	int fd;
	Fd_Info fd_info;
};

struct Process_info{
	int wait_index, fork_id;
	bool operator<(Process_info other) const {
        return wait_index > other.wait_index;
    }
};
char struct_info[90] = "struct User{char name[33];unsigned int age;char gender[7];char introduction[1025];};\n";
char gcc_command[25] = "gcc -fPIC -shared -o ";
std::map<int, Fd_Info> fds;
std::vector<int> waiting; //fd
std::queue<int> newers;
char *flag[4]; //work finish result
int wait_index[4];
Fork_Info *forkinfo[4];
void create_json_match(char *string, Fd_Info *fd_info)
{
	cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "matched");
    cJSON_AddStringToObject(root, "name", fd_info->user_info.name);
    cJSON_AddNumberToObject(root, "age", fd_info->user_info.age);
    cJSON_AddStringToObject(root, "gender", fd_info->user_info.gender);
    cJSON_AddItemToObject(root, "introduction", cJSON_CreateString(fd_info->user_info.introduction));
    cJSON_AddItemToObject(root, "filter_function", cJSON_CreateString(fd_info->filter_function));

    strcpy(string, cJSON_PrintUnformatted(root));
    cJSON_Delete(root);
}
void send_info_to_each_other(int fd_1, int fd_2)
{
	printf("%d %d\n", fd_1, fd_2);
	char one[6144], two[6144];
	Fd_Info *one_info = &(fds.find(fd_1)->second);
	Fd_Info *two_info = &(fds.find(fd_2)->second);
	create_json_match(one, one_info);
	create_json_match(two, two_info);
	send(fd_1, two, strlen(two), 0);
	send(fd_1, "\n", 1, 0);
	send(fd_2, one, strlen(one), 0);
	send(fd_2, "\n", 1, 0);
	one_info->status = CHATTING;
	two_info->status = CHATTING; 
	one_info->partner = fd_2;
	two_info->partner = fd_1;
}
void *process_matching(void *ptr)
{
	jmp_buf found;
	int match, match_fd;
	while (1) {
		if (newers.empty()) continue;
		printf("process newers\n");
		
		std::map<int, int> process; //<wait_index, fork_id>
		std::map<int, int>::iterator it_find, it;
		Process_info current;
		Fork_Info item, insert;
		insert.fd = newers.front();
		insert.fd_info = fds[insert.fd];
		fprintf(stderr, "newers pop\n");
		newers.pop();
		if (fds.find(insert.fd) == fds.end() || fds[insert.fd].status == FREE) continue;
		if ((match_fd = setjmp(found)) != 0) {
			send_info_to_each_other(insert.fd, match_fd);
			continue;
		}
		int current_match = 0;
		if (waiting.size()) {
			while (fds.find(waiting[current_match]) == fds.end() || fds[waiting[current_match]].status == FREE) {
				fprintf(stderr, "waiting erase\n");
				waiting.erase(waiting.begin());
				if (current_match < waiting.size()) {
					break;
				}
			}
		}
		for (; current_match < 4 && current_match < waiting.size(); current_match++) {
			//asign first job
			printf("give work\n");
			*forkinfo[current_match] = insert;
			item.fd = waiting[current_match];
			item.fd_info = fds[item.fd];
			*(forkinfo[current_match]+1) = item;
			wait_index[current_match] = current_match;
			process[current_match] = current_match;
			*flag[current_match] = 1;
			while (fds.find(waiting[current_match]) == fds.end() || fds[waiting[current_match]].status == FREE) {
				fprintf(stderr, "waiting erase\n");
				waiting.erase(waiting.begin()+current_match);
				if (current_match < waiting.size()) {
					break;
				}
			}
		}
		while (!process.empty()) {
			for (int i = 0; i < 4; i++) {
				if (*(flag[i]+1)) {
					printf("someone finish work\n");
					if (*(flag[i]+2)) { //found
						printf("receive found\n");
						match = waiting[wait_index[i]];
						*(flag[i]+2) = 0;
						for (it = process.begin(); it->second != i; it++) {
							while (!*(flag[it->second]+1)) {} //wait until finish
							if (*(flag[it->second]+2)) {
								match = waiting[it->first];
								break;
							}
						}
						fprintf(stderr, "found fd:%d\n", match);
						fprintf(stderr, "waiting erase %d\n", it->first);
						waiting.erase(waiting.begin()+it->first);
						longjmp(found, match);
					} else {
						process.erase(wait_index[i]);
						if (current_match < waiting.size()) {
							while (fds.find(waiting[current_match]) == fds.end() || fds[waiting[current_match]].status == FREE) {
								fprintf(stderr, "waiting erase\n");
								waiting.erase(waiting.begin()+current_match);
								if (current_match < waiting.size()) {
									break;
								}
							}
						}
						if (current_match < waiting.size()) {
							printf("give work\n");
							item.fd = waiting[current_match];
							item.fd_info = fds[item.fd];
							*(forkinfo[i]+1) = item;
							wait_index[i] = current_match;
							process[current_match] = i;
							*flag[i] = 1;
						}
					}
					*(flag[i]+1) = 0;
				}
			}
		}
		fprintf(stderr, "waiting push_back\n");
		waiting.push_back(insert.fd);
	}
}
void json_parse_try_match(Fd_Info *element, int fd)
{
	cJSON *root = cJSON_Parse(element->buffer);
	cJSON *subitem = root->child;
	subitem = subitem->next; //pass type
	strcpy(element->user_info.name, subitem->valuestring);
	subitem = subitem->next;
	element->user_info.age = subitem->valuedouble;
	subitem = subitem->next;
	strcpy(element->user_info.gender, subitem->valuestring);
	subitem = subitem->next;
	strcpy(element->user_info.introduction, subitem->valuestring);

	char function[4097], function_name[15];
	subitem = subitem->next;
	strcpy(function, subitem->valuestring);
	strcpy(element->filter_function, function);
	sprintf(function_name, "client/%d.c", fd);
	int function_fd = open(function_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	write(function_fd, struct_info, strlen(struct_info));
	write(function_fd, function, strlen(function));
	close(function_fd);
	cJSON_Delete(root);

	char gcc_local[100];
	sprintf(gcc_local, "%sclient/%d.so %s", gcc_command, fd, function_name);
	fprintf(stderr, "gcc: %s\n", gcc_local);
	system(gcc_local);

	char file_so[36];
	sprintf(element->file_so, "client/%d.so", fd);

	memset(element->buffer, 0, BufferSize);
	element->status = MATCHING;
}
int main(int argc, char const *argv[])
{	
	//init socket
	int port = atoi(argv[1]);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = PF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	int retval = bind(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr));
	retval = listen(sockfd, 5);

	//pthread & fork
	pthread_t thread;
	for (int i = 0; i < 4; i++) {
		flag[i] = (char*)mmap(NULL, sizeof(char)*3, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
		forkinfo[i] = (Fork_Info*)mmap(NULL, sizeof(Fork_Info)*2, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
		pid_t pid = fork();
		if (pid == 0) {
			while (1) {
				if (! *(flag[i]) ) {
					continue;
				}
				printf("working\n");
				*(flag[i]) = 0;
				void *handle = dlopen((*forkinfo[i]).fd_info.file_so, RTLD_LAZY);
				dlerror();
				int (*filter)(struct User) = (int (*)(struct User)) dlsym(handle, "filter_function");
				const char *dlsym_error = dlerror();
    			if (dlsym_error) {
    				printf("error: %s\n", dlsym_error);
    			}
				struct User user = (*(forkinfo[i]+1)).fd_info.user_info;
				int result = filter(user);
				dlclose(handle);
				printf("result %d\n", result);
				if (result) {
					handle = dlopen((*(forkinfo[i]+1)).fd_info.file_so, RTLD_LAZY);
					int (*filter)(struct User) = (int (*)(struct User)) dlsym( handle, "filter_function");
					user = (*forkinfo[i]).fd_info.user_info;
					int result = filter(user);
					dlclose(handle);
					printf("result %d\n", result);
					if (result) {
						printf("found in fork\n");
						*(flag[i]+2) = 1;
					}
				}
				*(flag[i]+1) = 1;
			}
		}
	}

	//init select
	int max_fd = sockfd + 1;
	fd_set readset;
	fd_set working_readset;
	FD_ZERO(&readset);
	FD_SET(sockfd, &readset);

	//json init
	int len1, len2, len3;
	cJSON *try_match_ptr, *other_quit_ptr, *quit_ptr;
	try_match_ptr = cJSON_CreateObject();
    cJSON_AddStringToObject(try_match_ptr, "cmd", "try_match");
    char *try_match = cJSON_PrintUnformatted(try_match_ptr);
    len1 = strlen(try_match);
    other_quit_ptr = cJSON_CreateObject();
    cJSON_AddStringToObject(other_quit_ptr, "cmd", "other_side_quit");
    char *other_quit = cJSON_PrintUnformatted(other_quit_ptr);
    len2 = strlen(other_quit);
    quit_ptr = cJSON_CreateObject();
    cJSON_AddStringToObject(quit_ptr, "cmd", "quit");
    char *quit = cJSON_PrintUnformatted(quit_ptr);
    len3 = strlen(quit);
    cJSON_Delete(try_match_ptr);
    cJSON_Delete(other_quit_ptr);
    cJSON_Delete(quit_ptr);

	char buffer[BufferSize];
	Fd_Info *temp;
	while (1) {
		memcpy(&working_readset, &readset, sizeof(fd_set));
    	retval = select(max_fd, &working_readset, NULL, NULL, NULL);

    	for (int fd = 0; fd < max_fd; fd++) {
	        if (!FD_ISSET(fd, &working_readset)) {
	            continue;
	        }
	        if (fd == sockfd) { //new connection
	            struct sockaddr_in client_addr;
	            socklen_t addrlen = sizeof(client_addr);
	            int client_fd = accept(fd, (struct sockaddr*) &client_addr, &addrlen);
	            if (client_fd >= 0) {
	            	//add new fd for later communication
	            	fprintf(stderr, "new connection\n");
	                FD_SET(client_fd, &readset);
	                max_fd = max(max_fd, client_fd+1);

	                //push into map
	                Fd_Info element;
	                element.status = FREE;
	                memset(element.buffer, 0, BufferSize);
	                fds.emplace(client_fd, element);
	            }
	        } else {
	            ssize_t sz;
	            memset(buffer, 0, BufferSize);
	            sz = recv(fd, buffer, BufferSize, 0);
	            Fd_Info *element = &fds[fd];

	            if (sz == 0) { //client close connection
	            	//send quit to other side
	            	fprintf(stderr, "client quit\n");
					send(element->partner, other_quit, len2, 0);
					send(element->partner, "\n", 1, 0);
					temp = &fds[element->partner];
					temp->status = FREE;

					//close 
	                close(fd);
	                FD_CLR(fd, &readset);
					fds.erase(fd);
	            } else if (sz < 0) { // error?
	                /* 進行錯誤處理
	                   ...略...  */
	            } else { // sz > 0，表示有新資料讀入
	                strcat(element->buffer, buffer);
	                char *ptr = strchr(element->buffer, '\n');
	                fprintf(stderr, "receive message\n");
	                // fprintf(stderr, "%s\n", element->buffer);
	                if (ptr != NULL) { //complete request
	                	switch(element->status) {
	                	case FREE:
	                		//try_match
	                		fprintf(stderr, "try match\n");
	                		json_parse_try_match(element, fd);
	                		send(fd, try_match, len1, 0);
	                		send(fd, "\n", 1, 0);
	                		fprintf(stderr, "newers push\n");
	                		newers.push(fd);
	                		//create thread
	                		pthread_create(&thread, NULL, process_matching, (void*) NULL);
	                		element->status = MATCHING;
	                		break;
	                	case MATCHING:
	                		//quit
	                		fprintf(stderr, "MATCHING\n");
	                		send(fd, quit, len3, 0);
	                		send(fd, "\n", 1, 0);
	                		element->status = FREE;
	                		//other
	                		send(element->partner, other_quit, len2, 0);
	                		send(element->partner, "\n", 1, 0);
	                		temp = &fds[element->partner];
							temp->status = FREE;
	                		break;
	                	case CHATTING:
	                		//quit or send_message
	                		fprintf(stderr, "CHATTING\n");
	                		if (strcmp(element->buffer, quit) == 0) { //quit
	                			send(fd, quit, len3, 0);
	                			send(fd, "\n", 1, 0);
	                			element->status = FREE;

	                			send(element->partner, other_quit, len2, 0);
	                			send(element->partner, "\n", 1, 0);
		                		temp = &fds[element->partner];
								temp->status = FREE;
	                		} else { //message
	                			int tmp_len = strlen(buffer);
	                			send(fd, buffer, tmp_len, 0);
	                			send(element->partner, buffer, tmp_len, 0);
	                		}
	                		break;
	                	}
	                	memset(element->buffer, 0, BufferSize);
	                }
	            }
	        }
	    }
	}

	close(sockfd);
}