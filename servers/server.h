#include <string>
#include <vector>

struct Conn {
  /* Struct that contains all relevant data for an open connection */
  int fd = -1;  // -1 means connection closed

  bool want_read = false;
  bool want_write = false;
  bool want_close = false;

  std::vector<uint8_t> read_buf;
  std::vector<uint8_t> write_buf;

  Conn() = default;
};

// function forward declarations
void respond_to_client(std::vector<uint8_t>& write_buf, std::string client_msg);
bool parse_buffer(Conn* conn);
void handle_write(Conn* conn);
void handle_read(Conn* conn);
void fd_set_nb(int fd);
Conn* handle_accept(int server_fd);
int setup_socket(const int PORT);