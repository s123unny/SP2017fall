#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <bsd/md5.h>

#include "boss.h"

#define max(a,b) (a > b? a:b)

struct result
{
    int num, len; //include '\0'
    char md5[33];
    unsigned char string[100];
    struct MD5Context context;
};
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
int assign_jobs(int num, struct fd_pair client_fds[], struct result *current)
{
    /* TODO design your own (1) communication protocol (2) job assignment algorithm */
    static unsigned char assign[16] = "\xff\x7f\x55\x40\x33\x2a\x24\x1f";
    assign[15] = 0x0f;
    fprintf(stderr, "assign_jobs\n");
    unsigned char from = '\x00';
    char type = 'n';
    current->num ++;
    // unsigned char result[MD5_DIGEST_LENGTH], hash[33];
    // MD5Final(result, &(current->context));
    // for(int i=0; i <MD5_DIGEST_LENGTH; i++) {
    //     sprintf(&hash[i*2], "%02x", (unsigned int)result[i]);
    // }
    for (int i = 0; i < num; i++) {
        write(client_fds[i].input_fd, &type, sizeof(char));
        write(client_fds[i].input_fd, &current->num, sizeof(int));
        write(client_fds[i].input_fd, &current->context, sizeof(struct MD5Context));
        write(client_fds[i].input_fd, &from, sizeof(char));
        from += assign[num-1];
        if (i == num-1) from = 0xff;
        write(client_fds[i].input_fd, &from, sizeof(char));
        from ++;
    }
    // fprintf(stderr, "2 md5: %s\n", hash);
    fprintf(stderr, "%c %d %d#\n", type, current->num, current->len);
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
        pid_t pid = fork();
        if (pid == 0) { //child
            int fd;
            while ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK, S_IRUSR | S_IWUSR)) == -1) {}
            fprintf(stderr, "child writing\n");
            int t = write(fd, current->string, current->len - 1);
            fprintf(stderr, "child write %d\n", t);
            close(fd);
            exit(0);
        }
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
        // assert (fd_ptr->output_fd >= 0);

        FD_SET(fd_ptr->output_fd, &readset);
        maxfd = max(fd_ptr->output_fd, maxfd);
    }
    maxfd ++;
    struct result current;
    struct MD5Context copy;

    unsigned char result[MD5_DIGEST_LENGTH];
    MD5Init(&current.context);

    fprintf(stderr, "open: %s\n", config.mine_file);
    int mine_fd = open(config.mine_file, O_RDONLY);
    unsigned long file_size = get_size_by_fd(mine_fd);
    fprintf(stderr, "%u\n", file_size);
    memset(current.string, 0, sizeof(current.string));
    read(mine_fd, current.string, file_size);
    current.len = file_size + 1;
    fprintf(stderr, "%s\n", current.string);
    MD5Update(&current.context, current.string, file_size);
    copy = current.context;
    MD5Final(result, &current.context);
    for(int i=0; i <MD5_DIGEST_LENGTH; i++) {
        sprintf(&current.md5[i*2], "%02x", (unsigned int)result[i]);
    }
    current.context = copy;
    fprintf(stderr, "init md5:%s\n", current.md5);

    for (current.num = 0; current.num < 33; current.num++) {
        if (current.md5[current.num] != '0') {
            break;
        }
    }

    /* assign jobs to clients */
    assign_jobs(config.num_miners, client_fds, &current);

    int len, len2, old, tempnum;
    char name[200];
    unsigned char temp[30], temp2[33];
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
                read(client_fds[i].output_fd, &tempnum, sizeof(int));
                read(client_fds[i].output_fd, &len, sizeof(int));
                memset(temp, 0, sizeof(temp));
                read(client_fds[i].output_fd, temp, len); //char to add
                fprintf(stderr, "%x %d\n", temp[1], len);
                read(client_fds[i].output_fd, &len2, sizeof(int));
                memset(name, 0, sizeof(name));
                read(client_fds[i].output_fd, name, len2); //name
                if (tempnum != current.num + 1) {
                    old = 1;
                }
                if (!old) {
                    fprintf(stderr, "wwww");
                    for (int j = 0; j < len; j++) {
                        fprintf(stderr, "%x ", temp[j]);
                    }
                    fprintf(stderr, "\n");
                    memcpy(current.string + current.len-1, temp, len);
                    current.len += len - 1;
                    current.num ++;

                    MD5Update(&current.context, temp, len-1);
                    copy = current.context;
                    MD5Final(result, &current.context);
                    for(int i=0; i <MD5_DIGEST_LENGTH; i++) {
                        sprintf(&current.md5[i*2], "%02x", (unsigned int)result[i]);
                    }
                    current.context = copy;
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
    }
    return 0;
}
