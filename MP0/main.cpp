#include <stdio.h>
#include <string.h>
#define MAX 10000

int main(int argc, char const *argv[])
{
	int ans = 0;
	FILE *fp;
	char charset[MAX], input[200], c;
	strcpy(charset, argv[1]);
	if (argc == 3) {
		fp = fopen(argv[2], "r");
	} else {
		fp = stdin;
	}
	while ((c = fgetc(fp)) != EOF) {
		if (c == '\n') {
			printf("%d\n", ans);
			ans = 0;
		} else {
			for (int i = 0; i < strlen(charset); i++) {
				if (charset[i] == c) {
					ans ++;
					break;
				}
			}
		}		
	}
	return 0;
}
