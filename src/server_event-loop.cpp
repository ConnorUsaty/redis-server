#include "ServerEventLoop.h"

int main() {
  const int PORT = 1234;
  ServerEventLoop server(PORT);

  return server.run_server();
}
