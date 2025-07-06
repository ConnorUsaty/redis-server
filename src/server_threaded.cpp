#include "server.h"

int main() {
  const int PORT = 1234;
  ServerThreaded server(PORT);

  return server.run_server();
}