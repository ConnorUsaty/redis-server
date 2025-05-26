.PHONY: all
all: client.exe server_threaded.exe server_event-loop.exe format check-format

client.exe: client.cpp
	g++ -Wall -Wextra -O2 -g client.cpp -o client.exe

server_threaded.exe: servers/server_threaded.cpp
	g++ -Wall -Wextra -O2 -g servers/server_threaded.cpp -o server_threaded.exe

server_event-loop.exe: servers/server_event-loop.cpp
	g++ -Wall -Wextra -O2 -g servers/server_event-loop.cpp -o server_event-loop.exe

format:
	find . -name "*.cpp" -o -name "*.h" | xargs clang-format -i

check-format:
	find . -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror