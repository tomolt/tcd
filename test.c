int say_hello(int x, int y, int z);

int main(int argc, char **argv)
{
	int x = 0x55;
	int y = 0x56;
	int z = 0x57;

	char str[] = "String";

	say_hello(10 * 331, 0x256, 0x257);
	return 0;
}
