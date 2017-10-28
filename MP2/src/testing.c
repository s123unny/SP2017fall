#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
	char filename[304], data[300], type;
	scanf("%s%s", filename, data);
	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	write(fd, data, strlen(data));
	scanf("%s", data);
	write(fd, data, strlen(data));
	return 0;
}