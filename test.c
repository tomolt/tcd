#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int say_hello(int x, int y, int z)
{
    printf("Hello ");
	printf("World!\n");
	int value = x * y + z;
	int b = z * x - 3;
	value += b;
    return value;
}

int main(int argc, char **argv)
{
    int x = 0x55;
    int y = 0x56;
    int z = 0x57;

	char str[] = "String";

    say_hello(10 * 331, 0x256, 0x257);
    return 0;
}
