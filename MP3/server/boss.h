struct pipe_pair
{
    char *input_pipe;
    char *output_pipe;
};

struct fd_pair
{
    int input_fd;
    int output_fd;
};

struct server_config
{
    char *mine_file;
    struct pipe_pair *pipes;
    int num_miners;
};

/* server->client protocol
(char*1)
c: //command
	(char*1)
		s: //status
		q: //quit
p: //print
	(int) //len
	(char * len) //data to print
n: //new work
	(int) //now need to find num
	(struct MD5Context)
	(char*1, char*1) //range

	client->server protocol
(int) //num
(int) //len
(char * len) //found string(need to be added)
(int) //len
(char * len) //name
*/