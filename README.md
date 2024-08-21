"Make clean" is provided for you to clean up any object files and executables by running the "make" command.\
Use make to build the httpproxy executable.\
The file is already formatted by clang.\
To use the executable follow this example : "./httpserver serverport# clientport# -u -c 6 -m 35000" where as port# represents a port number, -u represents enabling LRU cache system, and -c # represents number of files that can be inside the cache, and -m # represents the number of bytes a file can have that can be put inside the cache.\
Flags may be executed in any order. Though it is also server port first and client port second. Default is  values when not using flags is 3 files in a cache,65536 bytes in a file, and FIFO cacahe system is enabled.
Examples : "./httpproxy 9090 8080".\
"./httpproxy 9091 -u -m 35535 -c 5 8081"\
"./httpproxy 8000 -c 2 -m 50500 9090"\
\
\
\
Thank you for using the program!
