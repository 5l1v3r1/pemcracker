# pemcracker
Tool to crack encrypted PEM files

This tool is inspired by [pemcrack](https://github.com/robertdavidgraham/pemcrack) by Robert Graham.  The purpose is to attempt to recover the password for encrypted PEM files while utilizing all the CPU cores.

It still uses high level OpenSSL calls in order to guess the password.  As an optimization, instead of continually checking against the PEM on disk, it is loaded into memory in each thread.

    bwall@ragnarok:~$ ./pemcracker 
    pemcracker 0.1.0
    pemcracker <path to pem> <word file>

    pemcracker 0.1.0 by Brian Wallace (@botnet_hunter)
