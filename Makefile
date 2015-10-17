
pemcracker: main.c src/Blob.c
	g++ -pthread main.c src/Blob.c -o pemcracker -lssl -lcrypto -O3

