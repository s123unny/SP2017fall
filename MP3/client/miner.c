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
    unsigned char from, to, basic[100];
    char hash[33];
    int num, len;
};
int quit = 1;
int process_select(fd_set working_readset, int input_fd, struct workinginfo *working)
{   
    /*
    memcpy(&working_readset, &readset, sizeof(readset));
    select(maxfd, &working_readset, NULL, NULL, &timeout);
    process_select(working_readset, input_fd, &working);
    */
    int len, len2;
    char type;

    if (FD_ISSET(input_fd, &working_readset)) {
        // new work or command
        read(input_fd, &type, sizeof(char));
        if (type == 'c') { //print status
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
            read(input_fd, &working->len, sizeof(int));
            read(input_fd, working->basic, sizeof(char) * working->len);
            read(input_fd, &working->from, sizeof(char));
            read(input_fd, &working->to, sizeof(char));
            fprintf(stderr, "%d %s %x %x\n", working->num, working->basic, working->from, working->to);
            for (int i = 0; i < working->len; i++) {
                fprintf(stderr, "%x ", working->basic[i]);
            }
            fprintf(stderr, "\n");
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
    // unsigned char result[16], temp[2], pop_item[20];
    // int len, no, flag;
    // memset(temp, 0, sizeof(temp));
    // memset(pop_item, 0, sizeof(pop_item));

    int now, len = 2, no, flag, count = 0;
    unsigned char add[30], result[16];
    memset(add, 0, sizeof(add));
    add[0] = working->from;
    struct MD5Context context, new;
    MD5Init(&context);
    MD5Update(&context, working->basic, working->len-1);
    while (1) {
        new = context;
        MD5Update(&new, add, len-1);
        MD5Final(result, &new);
        for(int j = 0; j < MD5_DIGEST_LENGTH; j++) {
            sprintf(&md5string[j*2], "%02x", (unsigned int)result[j]);
        }
        no = 0;
        if (md5string[working->num] == '0') {
            no = 1;
        } else {
            for (int j = 0; j < working->num; j++) {
                if (md5string[j] != '0') {
                    no = 1;
                    break;
                }
            }
        }
        if (!no) {
            //write print
            write(output_fd, &len, sizeof(int));
            write(output_fd, add, len);
            fprintf(stderr, "%x %x %d\n", add[0], add[1], len);
            write(output_fd, md5string, 33);
            len = strlen(name) + 1;
            write(output_fd, &len, sizeof(int));
            write(output_fd, name, len);
            fprintf(stderr, "#I win a %d-treasure! %s\n", working->num, md5string);
            printf("I win a %d-treasure! %s\n", working->num, md5string);
            return 1;
        }
        count ++;
        if (count == 32) {
            // fprintf(stderr, "one round\n");
            memcpy(&working_readset, &readset, sizeof(readset));
            select(maxfd, &working_readset, NULL, NULL, &timeout);
            strcpy(working->hash, md5string);
            flag = process_select(working_readset, input_fd, working);
            if (flag == 2) {
                fprintf(stderr, "return\n");
                return 2;
            } else if (flag == 1) {
                return 0;
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
