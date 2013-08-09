#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/ioctl.h>


#define FLIP_CONF_UP   0x0
#define FLIP_CONF_LOW  0x1
#define FLIP_IN_EMPTY  (0x1 << 1)
#define FLIP_OUT_EMPTY (0x1 << 2)

#define FLIP_IO  0xF4
#define FLIP_CMD_DIR  _IOW(FLIP_IO, 1, int)

#define DEFAULT_BUF_SIZE 1024

#define FLIP_DEV "/dev/flip0"



int main(int argc, char *argv[])
{
	int dir;
	int fd;
	int ret;

	char *p;

	if (argc < 2) {
		printf("usage: flip_ioctl < 0 | 1 >\n");
		printf("       '0': turn to upper case\n");
		printf("       '1': turn to lower case\n");
		exit(0);
	}

	p = argv[1];
	while (*p) {
		if (!isdigit(*p++)) {
			printf("arguments must be a number!\n");
			exit(0);
		}
	}

	dir = atoi(argv[1]);
	if (dir != 0 && dir != 1) {
		printf("arguments can only be '0' or '1'!\n");
		exit(0);
	}

	
	if ((fd = open(FLIP_DEV, O_RDWR)) < 0) {
		printf("can not open '/dev/flip0', make sure it exist!\n");
		exit(0);
	}



	printf("set flip direction to %s\n", dir & 1 ? "lower" : "upper");
	ret = ioctl(fd, FLIP_CMD_DIR, &dir);

	if (ret < 0) 
		perror("ioctl failed!\n");
	
	close(fd);
	return ret;
}
