#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <bsd/md5.h>
#include <fcntl.h>
#include <string.h>
#define QMAX 100000

struct workinginfo
{
    unsigned char from, to, basic[100];
    char hash[33];
    int num, len;
};
unsigned char queue[QMAX][20];
int queue_len[QMAX];
int front, rear, amount;
void push_queue(unsigned char *element, int len)
{
    if (amount + 1 == QMAX) {
        fprintf(stderr, "Queue Overflow\n");
        return;
    } 
    if (rear == QMAX-1) {
        if (front) {
            rear = 0;
        }
    } else {
        if(front == -1) 
            front = 0;
        rear ++;
    }
    queue_len[rear] = len;
    strcpy(queue[rear], element);
    amount ++;
}
void pop_queue(unsigned char *pop_item, int *len)
{
    if (!amount) {
        fprintf(stderr, "Queue Underflow\n");
    }
    strcpy(pop_item, queue[front]);
    *len = queue_len[front];
    front ++;
    amount --;
    return;
}

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
                return 2;
            }
        } else if (type == 'n') { //new work
            fprintf(stderr, "new work\n");
            read(input_fd, &working->num, sizeof(int));
            read(input_fd, &working->len, sizeof(int));
            read(input_fd, working->basic, sizeof(char) * working->len);
            read(input_fd, &working->from, sizeof(char));
            read(input_fd, &working->to, sizeof(char));
            fprintf(stderr, "%d %s %x %x\n", working->num, working->basic, working->from, working->to);
            fprintf(stderr, "%d %x\n", strlen(working->basic), working->basic[len2-2]);
            return 1;
        } else { //print
            fprintf(stderr, "print data %c#\n", type);
            read(input_fd, &len, sizeof(int));
            char data[len];
            memset(data, 0, sizeof(data));
            read(input_fd, data, sizeof(char) * len);
            printf("%s\n", data);
        }
    }
    return 0;
}
void process_working(fd_set readset, int input_fd, int output_fd, struct workinginfo *working, char *name)
{
    int maxfd = input_fd + 1;
    fd_set working_readset;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 50;

    char md5string[33];
    unsigned char result[16], temp[2], pop_item[20];
    int len, no, flag;
    memset(temp, 0, sizeof(temp));
    memset(pop_item, 0, sizeof(pop_item));
    while (1) {
        front = -1; rear = -1; amount = 0;
        struct MD5Context context, copy;
        MD5Init(&context);
        MD5Update(&context, working->basic, strlen(working->basic));
        // MD5Update(&context, working->basic, working->len-1);
        for (unsigned char i = working->from; i < working->to; i++) {
            // fprintf(stderr, "%x\n", i);
            copy = context;
            temp[0] = i;
            MD5Update(&copy, temp, 1);
            MD5Final(result, &copy);
            for(int j = 0; j < MD5_DIGEST_LENGTH; j++) {
                sprintf(&md5string[j*2], "%02x", (unsigned int)result[j]);
            }
            no = 0;
            for (int j = 0; j < working->num; j++) {
                if (md5string[j] != '0') {
                    no = 1;
                    break;
                }
            }
            if (!no) {
                //write print
                len = 2;
                write(output_fd, &len, sizeof(int));
                write(output_fd, temp, len);
                write(output_fd, md5string, 33);
                len = strlen(name) + 1;
                write(output_fd, &len, sizeof(int));
                write(output_fd, name, len);
                fprintf(stderr, "#I win a %d-treasure! %s\n", working->num, md5string);
                printf("I win a %d-treasure! %s\n", working->num, md5string);
                return;
            }
            push_queue(temp, 1);
        }
        memcpy(&working_readset, &readset, sizeof(readset));
        select(maxfd, &working_readset, NULL, NULL, &timeout);
        strcpy(working->hash, md5string);
        flag = process_select(working_readset, input_fd, working);
        if (flag > 0) {
            fprintf(stderr, "return 1\n");
            return;
        }
        fprintf(stderr, "amount: %d\n", amount);
        while (amount > 0) {
            pop_queue(pop_item, &len);
            for (unsigned char i = 0; i < 255; i++) {
                copy = context;
                pop_item[len] = i;
                // fprintf(stderr, "%d %x\n", len, i);
                MD5Update(&copy, pop_item, len + 1);
                MD5Final(result, &copy);
                for(int j = 0; j < MD5_DIGEST_LENGTH; j++) {
                    sprintf(&md5string[j*2], "%02x", (unsigned int)result[j]);
                }
                no = 0;
                for (int j = 0; j < working->num; j++) {
                    if (md5string[j] != '0') {
                        no = 1;
                        break;
                    }
                }
                if (!no) {
                    //write print
                    len += 2;
                    write(output_fd, &len, sizeof(int));
                    write(output_fd, pop_item, len);
                    write(output_fd, md5string, 33);
                    len = strlen(name) + 1;
                    write(output_fd, &len, sizeof(int));
                    write(output_fd, name, len);
                    printf("I win a %d-treasure! %s\n", working->num, md5string);
                    fprintf(stderr, "I win a %d-treasure! %s\n", working->num, md5string);
                    return;
                }
                if ((i % '\x20') == '\x1f') {
                    memcpy(&working_readset, &readset, sizeof(readset));
                    select(maxfd, &working_readset, NULL, NULL, &timeout);
                    strcpy(working->hash, md5string);
                    flag = process_select(working_readset, input_fd, working);
                    if (flag > 0) {
                        return;
                    }
                }
                push_queue(pop_item, len + 1);
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
    int input_fd = open(input_pipe, O_RDONLY);
    assert (input_fd >= 0);

    int output_fd = open(output_pipe, O_WRONLY);
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
        process_select(working_readset, input_fd, &working);

        process_working(readset, input_fd, output_fd, &working, name);
    }

    return 0;
}
