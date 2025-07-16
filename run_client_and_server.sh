#!/bin/bash

cd build || exit

./server_event-loop.exe > /dev/null 2>&1 &
SERVER_PID=$!

echo "Setting up server..."
sleep 2
./client.exe

# force kill server when client exits
kill $SERVER_PID
echo "Killed server process"
