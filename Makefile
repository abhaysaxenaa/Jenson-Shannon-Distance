compare: compare.c
	gcc compare.c -o compare -lm -pthread -g -fsanitize=address,undefined
