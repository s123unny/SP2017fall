#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <bsd/md5.h>
#include <fcntl.h>
#include <string.h>

struct workinginfo
{
    unsigned char from, to;
    char hash[33];
    struct MD5Context context;
    int num, len;
};
int quit = 1;

struct MD5Context debug;

int process_select(fd_set working_readset, int input_fd, struct workinginfo *working)
{   
    int len, len2;
    char type;

    if (FD_ISSET(input_fd, &working_readset)) {
        // new work or command
        read(input_fd, &type, sizeof(char));
        if (type == 'c') { //command
            fprintf(stderr, "command\n");
            read(input_fd, &type, sizeof(char));
            if (type == 's') { //status
                fprintf(stderr, "status\n");
                printf("I'm working on %s\n", working->hash);
            } else { //quit
                fprintf(stderr, "quit\n");
                printf("BOSS is at rest.\n");
                quit = 1;
                return 2;
            }
        } else if (type == 'n') { //new work
            fprintf(stderr, "new work\n");
            if (quit) {
                printf("BOSS is mindful.\n");
                quit = 0;
            }
            read(input_fd, &working->num, sizeof(int));
            read(input_fd, &working->context, sizeof(struct MD5Context));
            read(input_fd, &working->from, sizeof(char));
            read(input_fd, &working->to, sizeof(char));
            fprintf(stderr, "%d %x %x\n", working->num, working->from, working->to);
            return 1;
        } else { //print
            fprintf(stderr, "print data %c#\n", type);
            read(input_fd, &len, sizeof(int));
            char data[len];
            memset(data, 0, sizeof(data));
            read(input_fd, data, sizeof(char) * len);
            printf("%s\n", data);
            process_select(working_readset, input_fd, working);
            return 1;
        }
    }
    return 0;
}
int process_working(fd_set readset, int input_fd, int output_fd, struct workinginfo *working, char *name)
{
    fprintf(stderr, "start process working\n");
    int maxfd = input_fd + 1;
    fd_set working_readset;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    char md5string[33];
    int now, len = 2, no, flag, count = 0;
    unsigned char add[30], result[16];
    memset(add, 0, sizeof(add));
    add[0] = working->from;
    struct MD5Context new;

    // MD5Final(result, &working->context);
    // for(int j = 0; j < MD5_DIGEST_LENGTH; j++) {
    //     sprintf(&working->hash[j*2], "%02x", (unsigned int)result[j]);
    // }
    // fprintf(stderr, "init hash:%s\n", working->hash);

    while (1) {
        new = working->context;
        MD5Update(&new, add, len-1);
        MD5Final(result, &new);
        for(int j = 0; j < MD5_DIGEST_LENGTH; j++) {
            sprintf(&working->hash[j*2], "%02x", (unsigned int)result[j]);
        }
        no = 0;
        if (working->hash[working->num] == '0') {
            no = 1;
        } else {
            for (int j = 0; j < working->num; j++) {
                if (working->hash[j] != '0') {
                    no = 1;
                    break;
                }
            }
        }
        if (!no) {
            //write print
            write(output_fd, &working->num, sizeof(int));
            write(output_fd, &len, sizeof(int));
            write(output_fd, add, len);
            fprintf(stderr, "%x %x %d\n", add[0], add[1], len);
            len = strlen(name) + 1;
            write(output_fd, &len, sizeof(int));
            write(output_fd, name, len);
            fprintf(stderr, "#I win a %d-treasure! %s\n", working->num, working->hash);
            printf("I win a %d-treasure! %s\n", working->num, working->hash);
            return 1;
        }
        count ++;
        if (count == 32) {
            // fprintf(stderr, "one round\n");
            memcpy(&working_readset, &readset, sizeof(readset));
            if (select(maxfd, &working_readset, NULL, NULL, &timeout) > 0) {
                flag = process_select(working_readset, input_fd, working);
                if (flag == 2) {
                    fprintf(stderr, "return\n");
                    return 2;
                } else if (flag == 1) {
                    return 0;
                }
            }
            count = 0;
        }
        for (now = len - 2; now > 0; now--) {
            if (add[now] != 0xff) {
                add[now] ++;
                for (int i = now + 1; i < len-1; i++) {
                    add[i] = 0;
                }
                break;
            }
        }
        if (!now) {
            if (add[0] != working->to) {
                add[0] ++;
            } else {
                add[0] = working->from;
                len ++;
                fprintf(stderr, "next len %d\n", len);
            }
            for (int i = 1; i < len-1; i++) {
                add[i] = 0;
            }
        }
    }
}
int main(int argc, char **argv)
{
    /* parse arguments */
    if (argc != 4) {
        fprintf(stderr, "Usage: %s CONFIG_FILE\n", argv[0]);
        exit(1);
    }
    setbuf(stdout, NULL);

    char *name = argv[1];
    char *input_pipe = argv[2];
    char *output_pipe = argv[3];

    /* create named pipes */
    int ret;
    ret = mkfifo(input_pipe, 0666);
    assert (!ret);

    ret = mkfifo(output_pipe, 0666);
    assert (!ret);

    /* open pipes */
    int input_fd = open(input_pipe, O_RDWR);
    assert (input_fd >= 0);

    int output_fd = open(output_pipe, O_RDWR);
    assert (output_fd >= 0);
    //setbuf(output_fd, NULL);

    /* TODO write your own (1) communication protocol (2) computation algorithm */
    /* To prevent from blocked read on input_fd, try select() or non-blocking I/O */
    /* initialize data for select() */
    int maxfd = input_fd + 1;
    fd_set readset;
    fd_set working_readset;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    FD_ZERO(&readset);
    FD_SET(input_fd, &readset);
    fprintf(stderr, "setting\n");

    struct workinginfo working;

    unsigned char hash[33], result[MD5_DIGEST_LENGTH];
    MD5Init(&debug);
    MD5Update(&debug, "B05902005", 9);
    MD5Final(result, &debug);
    for(int j = 0; j < MD5_DIGEST_LENGTH; j++) {
        sprintf(&hash[j*2], "%02x", (unsigned int)result[j]);
    }
    fprintf(stderr, "B05902005 hash: %s\n", hash);

    while (1) {
        fprintf(stderr, "waiting\n");
        memcpy(&working_readset, &readset, sizeof(readset));
        select(maxfd, &working_readset, NULL, NULL, NULL);
        if (process_select(working_readset, input_fd, &working) == 2) {
            continue;
        }
        while (!process_working(readset, input_fd, output_fd, &working, name)) {}
    }

    return 0;
}
