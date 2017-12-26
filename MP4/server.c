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
#include <assert.h>

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
	struct User user_info;
	char file_so[36];
};

struct Pids{
	pid_t pid[4];
};

char struct_info[90] = "struct User{char name[33];unsigned int age;char gender[7];char introduction[1025];};\n";
char gcc_command[25] = "gcc -fPIC -shared -o ";
std::map<int, Fd_Info> fds;
std::vector<int> waiting; //fd
std::queue<int> newers;
volatile char *flag[4]; //work finish result
int wait_index[4], current_match, insert_fd, quit_count[1030], is_kill;
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
	char one[BufferSize], two[BufferSize];
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
	for (int i = 0; i < 4; i++) {
		wait_index[i] = -1;
	}
	while (1) {
		if (newers.size() == 0) continue;
		assert(newers.size());
		printf("process newers\n");
		printf("queue: %d %u\n", newers.front(), newers.size());
		
		std::map<int, int> process; //<wait_index, fork_id>
		fprintf(stderr, "new process\n");
		std::map<int, int>::iterator it_find, it;
		Fork_Info item, insert;
		insert_fd = newers.front();
		insert.user_info = fds[insert_fd].user_info;
		strcpy(insert.file_so, fds[insert_fd].file_so);
		fprintf(stderr, "newers pop\n");
		newers.pop();
		if (quit_count[insert_fd]) {
			quit_count[insert_fd] --;
			continue;
		}
		if ((match_fd = setjmp(found)) != 0) {
			if (is_kill) {
				is_kill = 0;
				continue;
			}
			send_info_to_each_other(insert_fd, match_fd);
			insert_fd = 0;
			continue;
		}
		current_match = 0;
		for (; current_match < 4 && current_match < waiting.size(); current_match++) {
			//asign first job
			fprintf(stderr, "give work %d %d\n", current_match, waiting.size());
			*forkinfo[current_match] = insert;
			strcpy(item.file_so, fds[waiting[current_match]].file_so);
			item.user_info = fds[waiting[current_match]].user_info;
			*(forkinfo[current_match]+1) = item;
			wait_index[current_match] = current_match;
			process[current_match] = current_match;
			*flag[current_match] = 1;
		}
		while (!process.empty()) {
			for (int i = 0; i < 4; i++) {
				if (*(flag[i]+1)) {
					fprintf(stderr, "someone finish work\n");
					if (*(flag[i]+2)) { //found
						fprintf(stderr, "receive found %d\n", i);
						match = waiting[wait_index[i]];
						wait_index[i] = -1;
						for (it = process.begin(); it->second != i && it != process.end(); it++) {
							fprintf(stderr, "looping? %d %d\n", it->second, it->first);
							while (!*(flag[it->second]+1)) {} //wait until finish
							fprintf(stderr, "someone finish work.\n");
							*(flag[it->second]+1) = 0;
							wait_index[it->second] = -1;
							fprintf(stderr, "not looping\n");
							if (*(flag[it->second]+2)) {
								*(flag[it->second]+2) = 0;
								match = waiting[it->first];
								break;
							}
						}
						fprintf(stderr, "found fd:%d\n", match);
						if (!is_kill) {
							fprintf(stderr, "waiting erase %d\n", it->first);
							waiting.erase(waiting.begin()+it->first);
						}
						it ++;
						for (; it != process.end(); it ++) {
							fprintf(stderr, "wait for %d\n", it->second);
							while (!*(flag[it->second]+1)) {}
							fprintf(stderr, "someone finish work...\n");
							*(flag[it->second]+1) = 0;
							*(flag[it->second]+2) = 0;
							wait_index[it->second] = -1;
						}
						*(flag[i]+1) = 0;
						*(flag[i]+2) = 0;
						current_match = 0;
						longjmp(found, match);
					} else {
						process.erase(wait_index[i]);
						fprintf(stderr, "process erase\n");
						if (current_match < waiting.size()) {
							fprintf(stderr, "give work %d %d\n", current_match, waiting.size());
							strcpy(item.file_so, fds[waiting[current_match]].file_so);
							item.user_info = fds[waiting[current_match]].user_info;
							*(forkinfo[i]+1) = item;
							wait_index[i] = current_match;
							process[current_match] = i;
							*flag[i] = 1;
							current_match ++;
						} else {
							wait_index[i] = -1;
						}
					}
					*(flag[i]+1) = 0;
				}
			}
		}
		if (!is_kill) {
			waiting.push_back(insert_fd);
			fprintf(stderr, "waiting push_back\n");
			insert_fd = 0;
		}
		current_match = 0;
		is_kill = 0;
	}
}
void child_process(int i)
{
	while (1) {
		if (! *(flag[i]) ) {
			continue;
		}
		printf("working\n");
		*(flag[i]) = 0;
		void *handle = dlopen((*forkinfo[i]).file_so, RTLD_LAZY);
		dlerror();
		int (*filter)(struct User) = (int (*)(struct User)) dlsym(handle, "filter_function");
		const char *dlsym_error = dlerror();
		if (dlsym_error) {
			printf("error: %s\n", dlsym_error);
		}
		struct User user = (*(forkinfo[i]+1)).user_info;
		int result = filter(user);
		dlclose(handle);
		if (result) {
			handle = dlopen((*(forkinfo[i]+1)).file_so, RTLD_LAZY);
			int (*filter)(struct User) = (int (*)(struct User)) dlsym( handle, "filter_function");
			user = (*forkinfo[i]).user_info;
			int result = filter(user);
			dlclose(handle);
			if (result) {
				printf("found in fork\n");
				*(flag[i]+2) = 1;
			}
		}
		*(flag[i]+1) = 1;
		fprintf(stderr, "%d finish work: %d\n", i, result);
	}
}
void *keep_child_alive(void *ptr)
{
	Pids *pidptr = (Pids *)ptr;
	int wstatus;
	while (1) {
		for (int i = 0; i < 4; i++) {
			if (waitpid(pidptr->pid[i], &wstatus, WNOHANG) != 0) {
				fprintf(stderr, "child %d restart\n", i);
				*(flag[i]+2) = 0;
				*(flag[i]+1) = 1;
				*(flag[i]) = 0;
				pidptr->pid[i] = fork();
				if (pidptr->pid[i] == 0) {
					child_process(i);
				}
			}
		}
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
	if (retval != 0) {
		printf("socket fail\n");
		return 0;
	}
	retval = listen(sockfd, 5);

	//pthread & fork
	pthread_t thread1, thread2;
	Pids pids;
	pthread_create(&thread1, NULL, process_matching, (void*) NULL);
	for (int i = 0; i < 4; i++) {
		flag[i] = (volatile char*)mmap(NULL, sizeof(char)*3, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
		forkinfo[i] = (Fork_Info*)mmap(NULL, sizeof(Fork_Info)*2, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
		pids.pid[i] = fork();
		if (pids.pid[i] == 0) {
			child_process(i);
		}
	}
	pthread_create(&thread2, NULL, keep_child_alive, (void*) &pids);

	//init select
	int max_fd = sockfd + 1;
	fd_set readset;
	fd_set working_readset;
	FD_ZERO(&readset);
	FD_SET(sockfd, &readset);

	//json init
	int len1, len2, len3, len4;
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
    char receive_head[] = "{\"cmd\": \"receive_message\",";
    len4 = strlen(receive_head);

    mkdir("client", S_IRWXU);

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
	            	fprintf(stderr, "client quit\n");
	            	fds.erase(fd);
	            	//send quit to other side
	            	if (element->status == CHATTING) {
						send(element->partner, other_quit, len2, 0);
						send(element->partner, "\n", 1, 0);
						temp = &fds[element->partner];
						temp->status = FREE;
					} else if (element->status == MATCHING) {
						if (insert_fd == fd) {
							is_kill = 1;
							for (int j = 0; j < 4; j++) {
								if (wait_index[j] != -1) {
            						kill(pids.pid[j], SIGKILL);
								}
            				}
						} else {
							int in_waiting = 0;
							for (int i = 0; i < waiting.size(); i++) {
	                			if (waiting[i] == fd) {
	                				fprintf(stderr, "waiting erase %d\n", fd);
	                				waiting.erase(waiting.begin()+i);
	                				if (i < current_match) {
	        							current_match --;
	        						}
	                				for (int j = 0; j < 4; j++) {
	                					if (wait_index[j] == i) {
	                						fprintf(stderr, "kill %d\n", j);
	                						kill(pids.pid[j], SIGKILL);
	                					} else if (i < wait_index[j]) {
	                						wait_index[j] --;
	                					}
	                				}
	                				in_waiting = 1;
	                				break;
	                			}
	                		}
	                		if (!in_waiting) {
	                			quit_count[fd] ++;
	                		}
	                	}
					}
					//close 
	                close(fd);
	                FD_CLR(fd, &readset);
	            } else if (sz < 0) { // error?
	                /* 進行錯誤處理
	                   ...略...  */
	            } else { // sz > 0，表示有新資料讀入
	            	char *start = buffer;
	                char *ptr = strchr(buffer, '\n');
	                while (ptr != NULL) {
		                strncat(element->buffer, start, ptr-start+1);
		                fprintf(stderr, "receive message\n");
		                // fprintf(stderr, "%s\n", element->buffer);
						//complete request
	                	switch(element->status) {
	                	case FREE:
	                		//try_match
	                		fprintf(stderr, "try match\n");
	                		json_parse_try_match(element, fd);
	                		send(fd, try_match, len1, 0);
	                		fprintf(stderr, "%d\n", send(fd, "\n", 1, 0));
	                		fprintf(stderr, "newers push\n");
	                		newers.push(fd);
	                		element->status = MATCHING;
	                		break;
	                	case MATCHING:
	                		//quit
	                		element->status = FREE;
	                		if (insert_fd == fd) {
	                			is_kill = 1;
								for (int j = 0; j < 4; j++) {
									if (wait_index[j] != -1) {
	            						kill(pids.pid[j], SIGKILL);
									}
	            				}
							} else {
								int in_waiting = 0;
		                		for (int i = 0; i < waiting.size(); i++) {
		                			if (waiting[i] == fd) {
		                				fprintf(stderr, "waiting erase %d\n", fd);
		                				waiting.erase(waiting.begin()+i);
		                				if (i < current_match) {
			    							current_match --;
			    						}
			            				for (int j = 0; j < 4; j++) {
			            					if (wait_index[j] == i) {
			            						fprintf(stderr, "kill %d\n", j);
			            						kill(pids.pid[j], SIGKILL);
			            					} else if (i < wait_index[j]) {
		                						wait_index[j] --;
		                					}
			            				}
			            				in_waiting = 1;
		                				break;
		                			}
		                		}
		                		if (!in_waiting) {
		                			quit_count[fd] ++;
		                		}
		                	}
	                		fprintf(stderr, "MATCHING\n");
	                		send(fd, quit, len3, 0);
	                		send(fd, "\n", 1, 0);
	                		break;
	                	case CHATTING:
	                		//quit or send_message
	                		fprintf(stderr, "CHATTING\n");
	                		//fprintf(stderr, "%d %s %d %s\n", strlen(quit), quit, strlen(element->buffer), element->buffer);
	                		if (strncmp(element->buffer, quit, 14) == 0) { //quit
	                			fprintf(stderr, "quit chatting\n");
	                			send(fd, quit, len3, 0);
	                			send(fd, "\n", 1, 0);
	                			element->status = FREE;

	                			send(element->partner, other_quit, len2, 0);
	                			send(element->partner, "\n", 1, 0);
		                		temp = &fds[element->partner];
								temp->status = FREE;
	                		} else { //message
	                			fprintf(stderr, "send message %s", element->buffer);
	                			int tmp_len = strlen(element->buffer);
	                			fprintf(stderr, "send: %d\n", send(fd, element->buffer, tmp_len, 0));
	                			fprintf(stderr, "send: %d\n", send(element->partner, receive_head, len4, 0));
	                			fprintf(stderr, "send: %d\n", send(element->partner, element->buffer+22, tmp_len-22, 0));
	                		}
	                		break;
	                	}
		                memset(element->buffer, 0, BufferSize);
		                start = ptr+1;
		                ptr = strchr(start, '\n');
		            }
	            }
	        }
	    }
	}

	close(sockfd);
}