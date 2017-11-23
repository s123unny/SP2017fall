#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "boss.h"

int load_config_file(struct server_config *config, char *path)
{
    /* TODO finish your own config file parser */
    config->mine_file  = /* path to mine file */;
    config->pipes      = /* array of pipe pairs */;
    config->num_miners = /* number of miners */;

    return 0;
}

int assign_jobs()
{
    /* TODO design your own (1) communication protocol (2) job assignment algorithm */
}

int handle_command()
{
    /* TODO parse user commands here */
    char *cmd = /* command string */;

    if (strcmp(cmd, "status"))
    {
        /* TODO show status */
    }
    else if (strcmp(cmd, "dump"))
    {
        /* TODO write best n-treasure to specified file */
    }
    else
    {
        assert(strcmp(cmd, "quit"));
        /* TODO tell clients to cease their jobs and exit normally */
    }
}

int main(int argc, char **argv)
{
    /* sanity check on arguments */
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s CONFIG_FILE\n", argv[0]);
        exit(1);
    }

    /* load config file */
    struct server_config config;
    load_config_file(&config, argv[1]);

    /* open the named pipes */
    struct fd_pair client_fds[config.num_miners];

    for (int ind = 0; ind < config.num_miners; ind += 1)
    {
        struct fd_pair *fd_ptr = &client_fds[ind];
        struct pipe_pair *pipe_ptr = &config.pipes[ind];

        fd_ptr->input_fd = open(pipe_ptr->input_pipe, O_WRONLY);
        assert (fd_ptr->input_fd >= 0);

        fd_ptr->output_fd = open(pipe_ptr->input_pipe, O_RDONLY);
        assert (fd_ptr->output_fd >= 0);
    }

    /* initialize data for select() */
    int maxfd;
    fd_set readset;
    fd_set working_readset;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);
    // TODO add input pipes to readset, setup maxfd

    /* assign jobs to clients */
    assign_jobs();

    while (1)
    {
        memcpy(&working_readset, &readset, sizeof(readset)); // why we need memcpy() here?
        select(maxfd, &working_readset, NULL, NULL, &timeout);

        if (FD_ISSET(STDIN_FILENO, &working_readset))
        {
            /* TODO handle user input here */
            handle_command();
        }

        /* TODO check if any client send me some message
           you may re-assign new jobs to clients
        if (FD_ISSET(...))
        {
           ...
        }
        */
    }

    /* TODO close file descriptors */

    return 0;
}
