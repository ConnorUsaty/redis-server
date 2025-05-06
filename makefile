.PHONY: all
all: client.exe server.exe

client.exe: client.cpp
	g++ -Wall -Wextra -O2 -g client.cpp -o client.exe

server.exe: server.cpp
	g++ -Wall -Wextra -O2 -g server.cpp -o server.exe
