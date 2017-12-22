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
	void* handle;
};

struct Fork_Info{
	pid_t pid;
	int fd, wait_index;
};
char struct_info[90] = "struct User{char name[33];unsigned int age;char gender[7];char introduction[1025];};\n";
char gcc_command[25] = "gcc -fPIC -shared -o ";
std::map<int, Fd_Info> fds;
std::vector<int> waiting; //fd
std::queue<int> newers;
void create_json_match(char *string, Fd_Info *fd_info)
{
	cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "matched");
    cJSON_AddStringToObject(root, "name", fd_info->user_info.name);
    cJSON_AddNumberToObject(root, "age", fd_info->user_info.age);
    cJSON_AddStringToObject(root, "gender", fd_info->user_info.gender);
    cJSON_AddItemToObject(root, "introduction", cJSON_CreateString(fd_info->user_info.introduction));
    cJSON_AddItemToObject(root, "filter_function", cJSON_CreateString(fd_info->filter_function));

    string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
}
void send_info_to_each_other(int fd_1, int fd_2)
{
	printf("%d %d\n", fd_1, fd_2);
	char *one, *two;
	Fd_Info one_info = fds[fd_1];
	Fd_Info two_info = fds[fd_2];
	create_json_match(one, &one_info);
	create_json_match(two, &two_info);
	send(fd_1, two, strlen(two), 0);
	send(fd_1, "\n", 1, 0);
	send(fd_2, one, strlen(one), 0);
	send(fd_2, "\n", 1, 0);
	one_info.status = CHATTING;
	two_info.status = CHATTING; 
}
Fork_Info process_fork(int i, int fd, Fd_Info *insert)
{
	Fork_Info item;
	pid_t pid = fork();
	if (pid == 0) { //child
		pid_t child1 = fork();
		if (child1 == 0) {
			int (*filter)(struct User) = (int (*)(struct User)) dlsym(insert->handle, "filter_function");
			struct User user = fds[waiting[i]].user_info;
			int result = filter(user);
			printf("result: %d\n", result);
			exit(result);
		}
		pid_t child2 = fork();
		if (child2 == 0) {
			int (*filter)(struct User) = (int (*)(struct User)) dlsym(fds[waiting[i]].handle, "filter_function");
			struct User user = insert->user_info;
			int result = filter(user);
			printf("result: %d\n", result);
			exit(result);
		}
		int result1, result2;
		waitpid(child1, &result1, 0);
		waitpid(child2, &result2, 0);
		printf("parent recv result: %d %d\n", result1, result2);
		if (result1 == 1 && result2 == 1) {
			exit(1);
		}
		exit(0);
	} else { //parent
		item.wait_index = i;
		item.pid = pid;
		item.fd = waiting[i];
		printf("pid: %d\n", item.pid);
		return item;
	}
}
void *process_matching(void *ptr)
{
	while (1) {
		int found = 0;
		if (newers.empty()) continue;
		printf("process newers\n");
		int fd = newers.front();
		Fd_Info insert = fds[fd];
		newers.pop();
		
		std::queue<Fork_Info> process;
		Fork_Info item;
		int current_match = 0;
		for (; current_match < 4 && current_match < waiting.size(); current_match++) {
			//fork
			printf("fork\n");
			item = process_fork(current_match, fd, &insert);
			process.push(item);
		}
		int wstatus;
		pid_t pid, pid_f;
		Fork_Info match;
		while (!process.empty()) {
			pid = wait(&wstatus);
			printf("wait: %d %d\n", pid, wstatus);
			if (wstatus == 1) { //found
				printf("found\n");
				found = 1;
				while (pid != process.front().pid) {
					pid_f = process.front().pid;
					waitpid(pid_f, &wstatus, 0);
					if (wstatus == 1) {
						break;
					}
					process.pop();
				}
				match = process.front();
				send_info_to_each_other(match.fd, fd);
				waiting.erase(waiting.begin()+match.wait_index);
			} else {
				if (current_match < waiting.size()) {
					item = process_fork(current_match, fd, &insert);
					process.push(item);
					current_match ++;
				}
			}
		}
		if (!found) {
			waiting.push_back(fd);
		}
	}
	pthread_exit(0);
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

	char function[4097], function_name[5];
	subitem = subitem->next;
	strcpy(function, subitem->valuestring);
	strcpy(element->filter_function, function);
	sprintf(function_name, "client/%d.c", fd);
	int function_fd = open(function_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	write(function_fd, struct_info, strlen(struct_info));
	write(function_fd, function, strlen(function));
	close(function_fd);
	cJSON_Delete(root);

	char gcc_local[100] = {};
	sprintf(gcc_local, "%sclient/%d.so %s", gcc_command, fd, function_name);
	system(gcc_local);

	char file_so[36];
	sprintf(file_so, "client/%d.so", fd);
	element->handle = dlopen(file_so, RTLD_LAZY);

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

	//init select
	int max_fd = sockfd + 1;
	fd_set readset;
	fd_set working_readset;
	FD_ZERO(&readset);
	FD_SET(sockfd, &readset);

	//pthread
	pthread_t thread;

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
	Fd_Info temp;
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
	            	printf("new connection\n");
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
	            	printf("client quit\n");
					send(element->partner, other_quit, len2, 0);
					Fd_Info temp = fds[element->partner];
					temp.status = FREE;

					//close 
	                close(fd);
	                FD_CLR(fd, &readset);
	                dlclose(element->handle);
					fds.erase(fd);
	            } else if (sz < 0) { // error?
	                /* 進行錯誤處理
	                   ...略...  */
	            } else { // sz > 0，表示有新資料讀入
	                strcat(element->buffer, buffer);
	                char *ptr = strchr(element->buffer, '\n');
	                printf("receive message\n");
	                if (ptr != NULL) { //complete request
	                	switch(element->status) {
	                	case FREE:
	                		//try_match
	                		printf("try match\n");
	                		json_parse_try_match(element, fd);
	                		send(fd, try_match, len1, 0);
	                		send(fd, "\n", 1, 0);
	                		newers.push(fd);
	                		//create thread
	                		pthread_create(&thread, NULL, process_matching, (void*) NULL);
	                		element->status = MATCHING;
	                		break;
	                	case MATCHING:
	                		//quit
	                		send(fd, quit, len3, 0);
	                		send(fd, "\n", 1, 0);
	                		element->status = FREE;
	                		//other
	                		send(element->partner, other_quit, len2, 0);
	                		send(element->partner, "\n", 1, 0);
	                		temp = fds[element->partner];
							temp.status = FREE;
	                		break;
	                	case CHATTING:
	                		//quit or send_message
	                		if (strcmp(element->buffer, quit) == 0) { //quit
	                			send(fd, quit, len3, 0);
	                			send(fd, "\n", 1, 0);
	                			element->status = FREE;

	                			send(element->partner, other_quit, len2, 0);
	                			send(element->partner, "\n", 1, 0);
		                		temp = fds[element->partner];
								temp.status = FREE;
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