#include <cxxabi.h>
#include <rpcws.hpp>

#include "utils.h"

LOAD_ENV(YSRV_ENDPOINT, "ws://127.0.0.1:23456/api/token");

int main() {
  using namespace rpcws;
  try {
    static RPC::Client endpoint{ std::make_unique<client_wsio>(YSRV_ENDPOINT) };
    endpoint.call("test", json::object())
        .then([](json res) {
          std::cout << "recv: " << res << std::endl;
          endpoint.stop();
        })
        .fail([](std::exception_ptr ex) {
          try {
            std::rethrow_exception(ex);
          } catch (std::runtime_error &e) {
            int status;
            std::cerr << abi::__cxa_demangle(typeid(e).name(), 0, 0, &status) << ": " << e.what() << std::endl;
            endpoint.stop();
          }
        });
    endpoint.start();
  } catch (std::runtime_error &e) {
    int status;
    std::cerr << abi::__cxa_demangle(typeid(e).name(), 0, 0, &status) << ": " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}