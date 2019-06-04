#include <cxxabi.h>
#include <duktape.h>
#include <rpcws.hpp>

#include "utils.h"

LOAD_ENV(YSRV_ENDPOINT, "ws://127.0.0.1:23456/api/token");

static duk_context *ctx = duk_create_heap_default();

template <unsigned max = 100> nlohmann::json duk_get_json(duk_context *ctx, duk_idx_t idx) {
  using namespace nlohmann;
  switch (duk_get_type(ctx, idx)) {
  case DUK_TYPE_UNDEFINED:
  case DUK_TYPE_NULL: return nullptr;
  case DUK_TYPE_NUMBER: return duk_get_number(ctx, idx);
  case DUK_TYPE_STRING: {
    size_t len;
    auto tmp = duk_get_lstring(ctx, idx, &len);
    return std::string{ tmp, len };
  }
  case DUK_TYPE_OBJECT: {
    if (duk_is_array(ctx, idx)) {
      auto ret = json::array();
      duk_enum(ctx, idx, DUK_ENUM_ARRAY_INDICES_ONLY | DUK_ENUM_SORT_ARRAY_INDICES);
      while (duk_next(ctx, -1, true)) {
        ret.push_back(duk_get_json<max>(ctx, -1));
        duk_pop_2(ctx);
      }
      duk_pop(ctx);
      return ret;
    } else {
      auto ret = json::object();
      duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY);
      while (duk_next(ctx, -1, true)) {
        auto key = duk_get_string(ctx, -2);
        ret[key] = duk_get_json<max>(ctx, -1);
        duk_pop_2(ctx);
      }
      duk_pop(ctx);
      return ret;
    }
  }
  }
  return nullptr;
}

void duk_push_json(duk_context *ctx, nlohmann::json data) {
  using namespace nlohmann;
  switch (data.type()) {
  case json::value_t::null: duk_push_null(ctx); break;
  case json::value_t::boolean: duk_push_boolean(ctx, data.get<bool>()); break;
  case json::value_t::number_integer: duk_push_int(ctx, data.get<duk_int_t>()); break;
  case json::value_t::number_unsigned: duk_push_uint(ctx, data.get<duk_uint_t>()); break;
  case json::value_t::number_float: duk_push_number(ctx, data.get<duk_double_t>()); break;
  case json::value_t::string: {
    auto str = data.get<std::string>();
    duk_push_lstring(ctx, str.c_str(), str.length());
  } break;
  case json::value_t::object: {
    duk_push_object(ctx);
    for (auto &[key, value] : data.items()) {
      duk_push_json(ctx, value);
      duk_put_prop_lstring(ctx, -2, key.c_str(), key.length());
    }
  } break;
  case json::value_t::array: {
    duk_push_array(ctx);
    duk_uarridx_t i = 0;
    for (auto &value : data) {
      duk_push_json(ctx, data);
      duk_put_prop_index(ctx, -2, i++);
    }
  } break;
  default: duk_push_undefined(ctx);
  }
}

int main() {
  using namespace rpcws;
  try {
    static RPC endpoint{ std::make_unique<server_wsio>(YSRV_ENDPOINT) };
    static auto &handler = endpoint.layer<server_wsio>().handler();
    duk_int_t rc;
    static void *registry;

    rc = duk_peval_string(ctx, R"((function(reg) {
      return new Proxy({}, {
        set: function(tgt, prop, value) {
          tgt[prop] = value;
          reg(prop);
          return value;
        }
      })
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
            auto rc = duk_safe_call(
                ctx,
                +[](duk_context *ctx, void *udata) -> duk_ret_t {
                  duk_call_prop(ctx, -3, 1);
                  return 1;
                },
                nullptr, 3, 1);
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
    duk_put_global_string(ctx, "registry");
    duk_peval_string_noresult(ctx, "registry.test = function(a) { throw 'OK' };");
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
    endpoint.start();
  } catch (std::runtime_error &e) {
    int status;
    std::cerr << abi::__cxa_demangle(typeid(e).name(), 0, 0, &status) << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}