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
	(int*1) //len2
	(char * len1, char*1, char*1) //new range

	client->server protocol
(int) //len
(char * len) //found string (need to be added part)
(char * 33) //hash
(int) //len
(char * len) //name
*/

struct result
{
	int num, len; //include '\0'
	char md5[33];
	unsigned char string[100];
};
