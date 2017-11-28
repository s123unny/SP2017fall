#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
// #include <bsd/md5.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/md5.h>

#include "boss.h"

#define max(a,b) (a > b? a:b)

void MD5_generator(const char string[], char md5string[33])
{
    unsigned char result[MD5_DIGEST_LENGTH];
    const unsigned char* str_buffer = string;
    unsigned long str_size = strlen(string);
    
    MD5(str_buffer, str_size, result);
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
    fprintf(stderr, "%s\n", data);
    sscanf(data, "%s%s", tmp, tmp2);
    config->mine_file = malloc(sizeof(char)*strlen(tmp2) + 1);
    strcpy(config->mine_file, tmp2);
    fprintf(stderr, "%s\n", config->mine_file);
    config->num_miners = 0;
    config->pipes = pipe;

    str_ptr = strchr(data, '\n');
    str_ptr += 1;

    while (*str_ptr != '\0') {
        sscanf(str_ptr, "%s%s%s", tmp, tmp2, tmp3);
        pipe[config->num_miners].input_pipe = malloc(sizeof(char)*strlen(tmp2) + 1);
        pipe[config->num_miners].output_pipe = malloc(sizeof(char)*strlen(tmp3) + 1);
        strcpy(pipe[config->num_miners].input_pipe, tmp2);
        strcpy(pipe[config->num_miners].output_pipe, tmp3);
        // fprintf(stderr, "%s %s\n", pipe[config->num_miners].input_pipe, pipe[config->num_miners].output_pipe);
        config->num_miners ++;
        str_ptr = strchr(str_ptr, '\n');
        str_ptr += 1;
    }
    fprintf(stderr, "%d\n", config->num_miners);
    return 0;
}

int assign_jobs(int num, struct fd_pair client_fds[], struct result *current)
{
    /* TODO design your own (1) communication protocol (2) job assignment algorithm */
    fprintf(stderr, "assign_jobs\n");
    unsigned char range = '\xff', from = '\x00';
    char type = 'n';
    if (current->len != strlen(current->string) + 1) fprintf(stderr, "!!!!!\n");
    range /= num;
    current->num ++;
    for (int i = 0; i < num; i++) {
        write(client_fds[i].input_fd, &type, sizeof(char));
        write(client_fds[i].input_fd, &current->num, sizeof(int));
        write(client_fds[i].input_fd, &current->len, sizeof(int));
        write(client_fds[i].input_fd, current->string, current->len);
        write(client_fds[i].input_fd, &from, sizeof(char));
        from += range;
        write(client_fds[i].input_fd, &from, sizeof(char));
    }
    fprintf(stderr, "%c %d %d %s#\n", type, current->num, current->len, current->string);
    current->num --;
    fprintf(stderr, "finish\n");
}
int handle_command(int num, struct fd_pair client_fds[], struct result *current)
{
    /* TODO parse user commands here */
    char cmd[8], type, path[100];
    scanf("%s", cmd);

    if (strcmp(cmd, "status") == 0) {
        /* TODO show status */
        for (int i = 0; i < num; i++) {
            type = 'c';
            write(client_fds[i].input_fd, &type, sizeof(char));
            type = 's';
            write(client_fds[i].input_fd, &type, sizeof(char));
        }
        printf("best %d-treasure %s in %d bytes\n", current->num, current->md5, current->len-1);
    } else if (strcmp(cmd, "dump") == 0) {
        /* TODO write best n-treasure to specified file */
        scanf("%s", path);
        int fd = open(path, O_WRONLY);
        write(fd, current->string, current->len);
        close(fd);
    } else {
        assert(strcmp(cmd, "quit") == 0);
        /* TODO tell clients to cease their jobs and exit normally */
        for (int i = 0; i < num; i++) {
            type = 'c';
            write(client_fds[i].input_fd, &type, sizeof(char));
            type = 'q';
            write(client_fds[i].input_fd, &type, sizeof(char));
        }
        for (int ind = 0; ind < num; ind ++) {
            close(client_fds[ind].input_fd);
            close(client_fds[ind].output_fd);
        }
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
        assert (fd_ptr->output_fd >= 0);

        FD_SET(fd_ptr->output_fd, &readset);
        maxfd = max(fd_ptr->output_fd, maxfd);
    }

    struct result current;
    fprintf(stderr, "open: %s\n", config.mine_file);
    int mine_fd = open(config.mine_file, O_RDONLY);
    fprintf(stderr, "aaa %d\n", mine_fd);
    unsigned long file_size = get_size_by_fd(mine_fd);
    fprintf(stderr, "%u\n", file_size);
    memset(current.string, 0, sizeof(current.string));
    read(mine_fd, current.string, file_size);
    fprintf(stderr, "www%swww\n", current.string);
    if (strcmp(current.string, "B05902005") != 0) return 0;
    fprintf(stderr, "qqq\n");
    current.num = 0;
    current.len = strlen(current.string) + 1;
    MD5_generator(current.string, current.md5);
    // struct MD5Context context;
    // MD5Init(&context);
    // MD5Update(&context, current.string, current.len-1);

    /* assign jobs to clients */
    assign_jobs(config.num_miners, client_fds, &current);

    int len;
    char name[200];
    unsigned char temp[20], result[16];
    while (1) {
        fprintf(stderr, "waiting\n");
        memcpy(&working_readset, &readset, sizeof(readset)); // why we need memcpy() here?
        select(maxfd, &working_readset, NULL, NULL, NULL);

        if (FD_ISSET(STDIN_FILENO, &working_readset)) {
            /* TODO handle user input here */
            fprintf(stderr, "command\n");
            handle_command(config.num_miners, client_fds, &current);
        }

        /* TODO check if any client send me some message
           you may re-assign new jobs to clients */
        for (int i = 0; i < config.num_miners; i++) {
            fprintf(stderr, "wwwww\n");
            if (FD_ISSET(client_fds[i].output_fd, &working_readset)) {
                fprintf(stderr, "xxxx\n");
                read(client_fds[i].output_fd, &len, sizeof(int));
                // if (len == 0) fprintf(stderr, "QQQQQ\n");
                memset(temp, 0, sizeof(temp));
                read(client_fds[i].output_fd, temp, len);
                fprintf(stderr, "%x %d\n", temp[1], len);
                // strncat(current.string, temp, len-1);
                memcpy(current.string + current.len-1, temp, len);
                // fprintf(stderr, "%x\n", current.string[13]);
                current.len += len - 1;
                current.num ++;
                // MD5Update(&context, temp, len-1);
                // MD5Final(result, &context);
                // MD5(current.string, current.len-1, result);
                // for(int j = 0; j < MD5_DIGEST_LENGTH; j++) {
                //     sprintf(&current.md5[j*2], "%02x", (unsigned int)result[j]);
                // }
                // fprintf(stderr, "%d\n", current.len);
                memset(current.md5, 0, 33);
                read(client_fds[i].output_fd, current.md5, 33);
                read(client_fds[i].output_fd, &len, sizeof(int));
                memset(name, 0, sizeof(name));
                read(client_fds[i].output_fd, name, len);
                // MD5_generator(current.string, current.md5);

                char data[200], type = 'p';
                sprintf(data, "%s wins a %d-treasure! %s\n", name, current.num, current.md5);
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

    /* TODO close file descriptors */
    for (int ind = 0; ind < config.num_miners; ind ++) {
        close(client_fds[ind].input_fd);
        close(client_fds[ind].output_fd);
    }

    return 0;
}
