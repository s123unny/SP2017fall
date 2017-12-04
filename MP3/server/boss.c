#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/md5.h>

#include "boss.h"

#define max(a,b) (a > b? a:b)

typedef struct list{
    char path[PATH_MAX];
    int len;
    struct list *next;
}List;
List *start;

void MD5_generator(const char string[], char md5string[33], unsigned long len)
{
    unsigned char result[MD5_DIGEST_LENGTH];
    const unsigned char* str_buffer = string;
    
    MD5(str_buffer, len, result);
    for(int i=0; i <MD5_DIGEST_LENGTH; i++) {
        sprintf(&md5string[i*2], "%02x", (unsigned int)result[i]);
    }
}
unsigned long get_size_by_fd(int fd) {
    struct stat statbuf;
    if(fstat(fd, &statbuf) < 0) exit(-1);
    return statbuf.st_size;
}
int load_config_file(struct server_config *config, char *path)
{
    /* TODO finish your own config file parser */
    int fd = open(path, O_RDONLY);
    char data[1024], *str_ptr, tmp[100], tmp2[100], tmp3[100];
    static struct pipe_pair pipe[8];

    memset(data, 0, sizeof(data));
    read(fd, data, sizeof(data));
    sscanf(data, "%s%s", tmp, tmp2);
    config->mine_file = malloc(sizeof(char)*strlen(tmp2) + 1);
    strcpy(config->mine_file, tmp2);
    config->num_miners = 0;
    config->pipes = pipe;

    str_ptr = strchr(data, '\n');
    str_ptr += 1;

    while (*str_ptr != '\0') {
        if (sscanf(str_ptr, "%s%s%s", tmp, tmp2, tmp3) != 3) break;
        pipe[config->num_miners].input_pipe = malloc(sizeof(char)*strlen(tmp2) + 1);
        pipe[config->num_miners].output_pipe = malloc(sizeof(char)*strlen(tmp3) + 1);
        strcpy(pipe[config->num_miners].input_pipe, tmp2);
        strcpy(pipe[config->num_miners].output_pipe, tmp3);
        config->num_miners ++;
        str_ptr = strchr(str_ptr, '\n');
        str_ptr += 1;
    }
    fprintf(stderr, "%d\n", config->num_miners);
    return 0;
}
List *insert_list_node(List *head, char *path, int len)
{
    if (head == NULL) {
        List *current = (List *)malloc(sizeof(List));
        strcpy(current->path, path);
        current->len = len;
        current->next = head;
        return current;
    }
    head->next = insert_list_node(head->next, path, len);
    return head;
}
List *delete_list_node(List *delete)
{
    List *temp;
    temp = delete->next;
    free(delete);
    return temp;
}
void dump(struct result *current) {
    List *ptr, *before;
    int fd;
    if (start != NULL) {
        ptr = start;
        before = NULL;
        while (ptr != NULL) {
            fprintf(stderr, "%s\n", ptr->path);
            fd = open(ptr->path, O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK, S_IRUSR | S_IWUSR);
            if (fd > 0) {
                fprintf(stderr, "writing %d\n", ptr->len);
                int t = write(fd, current->string, ptr->len);
                fprintf(stderr, "%d\n", t);
                ptr = delete_list_node(ptr);
                if (before == NULL) {
                    start = ptr;
                } else {
                    before->next = ptr;
                }
            } else {
                ptr = ptr->next;
                if (before == NULL) {
                    before = start;
                } else {
                    before = before->next;
                }
            }
        }
    }
}
void dump2(struct result *current) {
    List *ptr, *before;
    int fd;
    if (start != NULL) {
        ptr = start;
        before = NULL;
        while (ptr != NULL) {
            fprintf(stderr, "%s\n", ptr->path);
            while ((fd = open(ptr->path, O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK, S_IRUSR | S_IWUSR)) == -1) {}
            fprintf(stderr, "writing %d\n", ptr->len);
            int t = write(fd, current->string, ptr->len);
            fprintf(stderr, "%d\n", t);
            ptr = delete_list_node(ptr);
            if (before == NULL) {
                start = ptr;
            } else {
                before->next = ptr;
            }
        }
    }
}
int assign_jobs(int num, struct fd_pair client_fds[], struct result *current)
{
    /* TODO design your own (1) communication protocol (2) job assignment algorithm */
    static unsigned char assign[16] = "\xff\x7f\x55\x40\x33\x2a\x24\x1f";
    assign[15] = 0x0f;
    fprintf(stderr, "assign_jobs\n");
    unsigned char from = '\x00';
    char type = 'n';
    current->num ++;
    for (int i = 0; i < num; i++) {
        write(client_fds[i].input_fd, &type, sizeof(char));
        write(client_fds[i].input_fd, &current->num, sizeof(int));
        write(client_fds[i].input_fd, &current->len, sizeof(int));
        write(client_fds[i].input_fd, current->string, current->len);
        write(client_fds[i].input_fd, &from, sizeof(char));
        from += assign[num-1];
        if (i == num-1) from = 0xff;
        write(client_fds[i].input_fd, &from, sizeof(char));
        from ++;
    }
    fprintf(stderr, "%c %d %d %s#\n", type, current->num, current->len, current->string);
    current->num --;
}
int handle_command(int num, struct fd_pair client_fds[], struct result *current)
{
    /* TODO parse user commands here */
    static int fd, len;
    static char *ptr;
    char cmd[8], type, path[PATH_MAX];
    scanf("%s", cmd);

    if (strcmp(cmd, "status") == 0) {
        /* TODO show status */
        for (int i = 0; i < num; i++) {
            type = 'c';
            write(client_fds[i].input_fd, &type, sizeof(char));
            type = 's';
            write(client_fds[i].input_fd, &type, sizeof(char));
        }
        if (!current->num) {
            printf("best 0-treasure in 0 bytes\n");
        } else {
            printf("best %d-treasure %s in %d bytes\n", current->num, current->md5, current->len-1);
        }
    } else if (strcmp(cmd, "dump") == 0) {
        /* TODO write best n-treasure to specified file */
        scanf("%s", path);
        start = insert_list_node(start, path, current->len-1);
        fprintf(stderr, "path: %s\n", start->path);
    } else {
        assert(strcmp(cmd, "quit") == 0);
        /* TODO tell clients to cease their jobs and exit normally */
        for (int i = 0; i < num; i++) {
            type = 'c';
            write(client_fds[i].input_fd, &type, sizeof(char));
            type = 'q';
            write(client_fds[i].input_fd, &type, sizeof(char));
            close(client_fds[i].input_fd);
            close(client_fds[i].output_fd);
        }
        dump2(current);
        fprintf(stderr, "I am going to rest\n");
        exit(0);
    }
}

int main(int argc, char **argv)
{
    /* sanity check on arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s CONFIG_FILE\n", argv[0]);
        exit(1);
    }

    start = NULL;

    /* load config file */
    struct server_config config;
    load_config_file(&config, argv[1]);

    /* open the named pipes */
    struct fd_pair client_fds[config.num_miners];

    /* initialize data for select() */
    int maxfd;
    fd_set readset;
    fd_set working_readset;

    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);

    // TODO add input pipes to readset, setup maxfd
    for (int ind = 0; ind < config.num_miners; ind ++) {
        fprintf(stderr, "%d\n", ind);
        struct fd_pair *fd_ptr = &client_fds[ind];
        struct pipe_pair *pipe_ptr = &config.pipes[ind];

        fprintf(stderr, "open: %s\n", pipe_ptr->input_pipe);
        fd_ptr->input_fd = open(pipe_ptr->input_pipe, O_WRONLY);
        // assert (fd_ptr->input_fd >= 0);

        fprintf(stderr, "open: %s\n", pipe_ptr->output_pipe);
        fd_ptr->output_fd = open(pipe_ptr->output_pipe, O_RDONLY);
        // assert (fd_ptr->output_fd >= 0);

        FD_SET(fd_ptr->output_fd, &readset);
        maxfd = max(fd_ptr->output_fd, maxfd);
    }
    maxfd ++;
    struct result current;
    fprintf(stderr, "open: %s\n", config.mine_file);
    int mine_fd = open(config.mine_file, O_RDONLY);
    fprintf(stderr, "aaa %d\n", mine_fd);
    unsigned long file_size = get_size_by_fd(mine_fd);
    fprintf(stderr, "%u\n", file_size);
    memset(current.string, 0, sizeof(current.string));
    read(mine_fd, current.string, file_size);
    fprintf(stderr, "www%swww\n", current.string);
    // fprintf(stderr, "qqq\n");
    current.len = file_size + 1;
    MD5_generator(current.string, current.md5, file_size);

    for (current.num = 0; current.num < 33; current.num++) {
        if (current.md5[current.num] != '0') {
            break;
        }
    }

    /* assign jobs to clients */
    assign_jobs(config.num_miners, client_fds, &current);

    int len, len2, old;
    char name[200];
    unsigned char temp[30], result[16], temp2[33];
    while (1) {
        fprintf(stderr, "waiting\n");
        memcpy(&working_readset, &readset, sizeof(readset)); // why we need memcpy() here?
        select(maxfd, &working_readset, NULL, NULL, NULL);

        if (FD_ISSET(STDIN_FILENO, &working_readset)) {
            /* TODO handle user input here*/
            handle_command(config.num_miners, client_fds, &current);
        }

        /* TODO check if any client send me some message
           you may re-assign new jobs to clients */
        for (int i = 0; i < config.num_miners; i++) {
            // fprintf(stderr, "wwwww\n");
            if (FD_ISSET(client_fds[i].output_fd, &working_readset)) {
                old = 0;
                fprintf(stderr, "xxxx\n");
                read(client_fds[i].output_fd, &len, sizeof(int));
                memset(temp, 0, sizeof(temp));
                read(client_fds[i].output_fd, temp, len); //char to add
                fprintf(stderr, "%x %d\n", temp[1], len);
                memset(temp2, 0, 33);
                read(client_fds[i].output_fd, temp2, 33); //hash
                read(client_fds[i].output_fd, &len2, sizeof(int));
                memset(name, 0, sizeof(name));
                read(client_fds[i].output_fd, name, len2); //name
                for (int j = 0; j < current.num + 1; j++) {
                    if (temp2[j] != '0') {
                        old = 1;
                        break;
                    }
                }
                if (!old) {
                    memcpy(current.string + current.len-1, temp, len);
                    current.len += len - 1;
                    current.num ++;
                    memcpy(current.md5, temp2, 33);
                    char data[250], type = 'p';
                    sprintf(data, "%s wins a %d-treasure! %s", name, current.num, current.md5);
                    len = strlen(data) + 1;
                    for (int j = 0; j < config.num_miners; j++) {
                        if (j != i) {
                            write(client_fds[j].input_fd, &type, sizeof(char));
                            write(client_fds[j].input_fd, &len, sizeof(int));
                            write(client_fds[j].input_fd, data, len);
                        }
                    }

                    assign_jobs(config.num_miners, client_fds, &current);
                    printf("A %d-treasure discovered! %s\n", current.num, current.md5);
                }
            }
        }
        dump(&current);
    }
    return 0;
}
