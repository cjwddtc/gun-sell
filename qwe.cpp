#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
using boost::asio::ip::tcp;
using boost::asio::buffer;
using boost::asio::buffer_cast;
using boost::asio::streambuf;
using boost::lexical_cast;

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: client <host>" << std::endl;
      return 1;
    }
    boost::asio::io_service io_service;
    tcp::resolver resolver(io_service);
    tcp::resolver::query query(argv[1], "31415");
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
    tcp::socket socket(io_service);
    boost::asio::connect(socket, endpoint_iterator);
    std::array<char, 1024> buf;
    int id = -1;
    std::string s("connect");
    boost::asio::write(socket, buffer(s));
    while (true) {
      boost::system::error_code error;
      std::string str;
      size_t len = socket.read_some(buffer(buf));
      buf[len] = 0;
      char *ptr = buf.data();
      id = atoi(ptr);
      if (id == -2) {
        break;
      }
      ptr = strchr(ptr, ' ') + 1;
      std::cout << ptr << std::endl;
      do {
        if (id != -1) {
          std::cout << id;
        }
        std::cout << ">";
        std::getline(std::cin, str);
      } while (str.empty());
      boost::asio::write(socket, buffer(str));
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}
