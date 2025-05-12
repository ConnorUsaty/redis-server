.PHONY: all
all: client.exe server_threaded.exe

client.exe: client.cpp
	g++ -Wall -Wextra -O2 -g client.cpp -o client.exe

server_threaded.exe: servers/server_threaded.cpp
	g++ -Wall -Wextra -O2 -g servers/server_threaded.cpp -o server_threaded.exe
