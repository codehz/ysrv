#pragma once
#include <duktape.h>
#include <epoll.hpp>
#include <json.hpp>

nlohmann::json duk_get_json(duk_context *ctx, duk_idx_t idx);
void duk_push_json(duk_context *ctx, nlohmann::json data);
void init_duk_stdlib(duk_context *_ctx);