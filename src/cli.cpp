#include <cxxabi.h>
#include <rpcws.hpp>

#include "utils.h"

LOAD_ENV(YSRV_ENDPOINT, "ws://127.0.0.1:23456/api/token");

int main(int argc, char **argv) {
  if (argc != 3) return EXIT_FAILURE;
  using namespace rpcws;
  try {
    auto ep = std::make_shared<epoll>();
    static RPC::Client endpoint{ std::make_unique<client_wsio>(YSRV_ENDPOINT, ep) };
    endpoint.call(argv[1], json::object({ { "command", argv[2] } }))
        .then([&](json res) {
          std::cout << "recv: " << res << std::endl;
          ep->shutdown();
        })
        .fail([&](std::exception_ptr ex) {
          try {
            std::rethrow_exception(ex);
          } catch (std::runtime_error &e) {
            int status;
            std::cerr << abi::__cxa_demangle(typeid(e).name(), 0, 0, &status) << ": " << e.what() << std::endl;
          }
          ep->shutdown();
        });
    endpoint.start();
    ep->wait();
  } catch (std::runtime_error &e) {
    int status;
    std::cerr << abi::__cxa_demangle(typeid(e).name(), 0, 0, &status) << ": " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}