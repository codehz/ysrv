#include <cxxabi.h>
#include <duktape.h>
#include <fcntl.h>
#include <rpcws.hpp>
#include <sys/mman.h>
#include <sys/stat.h>

#include "lib.h"
#include "utils.h"

LOAD_ENV(YSRV_ENDPOINT, "ws://127.0.0.1:23456/api/token");

static duk_context *ctx = duk_create_heap_default();

int main() {
  using namespace rpcws;
  try {
    auto ep = std::make_shared<epoll>();
    static RPC endpoint{ std::make_unique<server_wsio>(YSRV_ENDPOINT, ep) };
    holder{ ep };
    holder{ *ctx };
    duk_int_t rc;
    static void *registry;

    init_duk_stdlib(ctx);

    rc = duk_peval_string(ctx, R"((function(reg) {
      var ret = new Proxy({}, {
        set: function(tgt, prop, value) {
          tgt[prop] = value;
          reg(prop);
          return value;
        },
        deleteProperty: function(tgt, prop) {
          return false;
        }
      });
      Object.defineProperty(this, 'exports', {
        value: ret
      });
      return ret;
    }))");
    if (rc) {
      printf("%s\n", duk_safe_to_string(ctx, -1));
      throw new std::runtime_error("failed to create proxy");
    }
    duk_push_c_function(
        ctx,
        +[](duk_context *) -> duk_ret_t {
          std::string name = duk_get_string(ctx, -1);
          endpoint.reg(name, [=](auto, json data) -> json {
            duk_push_heapptr(ctx, registry);
            duk_push_string(ctx, name.c_str());
            duk_push_json(ctx, data);
            auto rc = duk_pcall_prop(ctx, -3, 1);
            if (rc == DUK_EXEC_SUCCESS) {
              auto ret = duk_get_json(ctx, -1);
              duk_pop(ctx);
              return ret;
            } else {
              throw std::runtime_error(duk_to_string(ctx, -1));
            }
          });
          return 0;
        },
        1);
    duk_call(ctx, 1);
    registry = duk_get_heapptr(ctx, -1);
    duk_pop(ctx);
    duk_push_c_function(
        ctx,
        +[](duk_context *) -> duk_ret_t {
          auto a1 = duk_require_string(ctx, -1);
          endpoint.event(a1);
          duk_push_c_function(
              ctx,
              +[](duk_context *) -> duk_ret_t {
                duk_require_object(ctx, -1);
                auto data = duk_get_json(ctx, -1);
                duk_push_this(ctx);
                auto ev = duk_get_string(ctx, -1);
                endpoint.emit(ev, data);
                duk_pop(ctx);
                return 1;
              },
              1);
          duk_push_string(ctx, "bind");
          duk_dup(ctx, -3);
          duk_call_prop(ctx, -3, 1);
          return 1;
        },
        1);
    duk_put_global_string(ctx, "event");
    if (int fd = open("ysrc.js", 0); fd != -1) {
      struct stat stat;
      fstat(fd, &stat);
      auto target = mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
      duk_peval_lstring_noresult(ctx, (char const *)target, stat.st_size);
      munmap(target, stat.st_size);
    }
    endpoint.start();
    ep->wait();
  } catch (std::runtime_error &e) {
    int status;
    std::cerr << abi::__cxa_demangle(typeid(e).name(), 0, 0, &status) << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}