#include <stdio.h>

int say_hello(int x, int y, int z)
{
	printf("Hello ");
	printf("World!\n");
	int value = x * y + z;
	int b = z * x - 3;
	value += b;
	return value;
}
