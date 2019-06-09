#include "lib.h"
#include "utils.h"

#include <duktape.h>
#include <epoll.hpp>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <iostream>
#include <linux/fs.h>
#include <linux/if.h>
#include <map>
#include <random>
#include <rpcws.hpp>
#include <streambuf>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <utime.h>

#define COPY_DEF(name)                                                                                                                               \
  { #name, (duk_double_t)name }

#define duk_dump(ctx)                                                                                                                                \
  duk_push_context_dump(ctx);                                                                                                                        \
  printf("%s\n", duk_to_string(ctx, -1));                                                                                                            \
  duk_pop(ctx);

namespace fs = std::filesystem;

nlohmann::json duk_get_json(duk_context *ctx, duk_idx_t idx) {
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
  case DUK_TYPE_BOOLEAN: return duk_get_boolean(ctx, idx) ? json(true) : json(false);
  case DUK_TYPE_OBJECT: {
    if (duk_is_array(ctx, idx)) {
      auto ret = json::array();
      duk_enum(ctx, idx, DUK_ENUM_ARRAY_INDICES_ONLY | DUK_ENUM_SORT_ARRAY_INDICES);
      while (duk_next(ctx, -1, true)) {
        ret.push_back(duk_get_json(ctx, -1));
        duk_pop_2(ctx);
      }
      duk_pop(ctx);
      return ret;
    } else {
      auto ret = json::object();
      duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY);
      while (duk_next(ctx, -1, true)) {
        auto key = duk_get_string(ctx, -2);
        ret[key] = duk_get_json(ctx, -1);
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

void duk_push_stat(duk_context *ctx, struct stat64 obj) {
  duk_push_object(ctx);
  auto atime                   = static_cast<duk_double_t>(obj.st_atim.tv_sec * 1000) + static_cast<duk_double_t>(obj.st_atim.tv_nsec / 1.e6);
  auto mtime                   = static_cast<duk_double_t>(obj.st_mtim.tv_sec * 1000) + static_cast<duk_double_t>(obj.st_mtim.tv_nsec / 1.e6);
  auto ctime                   = static_cast<duk_double_t>(obj.st_ctim.tv_sec * 1000) + static_cast<duk_double_t>(obj.st_ctim.tv_nsec / 1.e6);
  duk_number_list_entry temp[] = {
    { "dev", static_cast<duk_double_t>(obj.st_dev) },
    { "ino", static_cast<duk_double_t>(obj.st_ino) },
    { "mode", static_cast<duk_double_t>(obj.st_mode) },
    { "nlink", static_cast<duk_double_t>(obj.st_nlink) },
    { "uid", static_cast<duk_double_t>(obj.st_uid) },
    { "gid", static_cast<duk_double_t>(obj.st_gid) },
    { "rdev", static_cast<duk_double_t>(obj.st_rdev) },
    { "size", static_cast<duk_double_t>(obj.st_size) },
    { "blksize", static_cast<duk_double_t>(obj.st_blksize) },
    { "blocks", static_cast<duk_double_t>(obj.st_blocks) },
    { "atimeMs", atime },
    { "mtimeMs", mtime },
    { "ctimeMs", ctime },
    { "birthtimeMs", static_cast<duk_double_t>(0) },
    { nullptr, (double)0 },
  };
  duk_put_number_list(ctx, -1, temp);
  duk_get_global_string(ctx, "Date");
  duk_dup_top(ctx);
  duk_push_number(ctx, atime);
  duk_new(ctx, 1);
  duk_put_prop_string(ctx, -3, "atime");
  duk_dup_top(ctx);
  duk_push_number(ctx, mtime);
  duk_new(ctx, 1);
  duk_put_prop_string(ctx, -3, "mtime");
  duk_dup_top(ctx);
  duk_push_number(ctx, ctime);
  duk_new(ctx, 1);
  duk_put_prop_string(ctx, -3, "ctime");
  duk_push_number(ctx, 0);
  duk_new(ctx, 1);
  duk_put_prop_string(ctx, -2, "birthtime");
}

static const std::map<std::string, int> fopenmap = {
  { "r", O_RDONLY }, { "w", O_WRONLY | O_CREAT | O_TRUNC }, { "a", O_WRONLY | O_CREAT | O_APPEND },
  { "r+", O_RDWR },  { "w+", O_RDWR | O_CREAT | O_TRUNC },  { "a+", O_RDWR | O_CREAT | O_APPEND },
};

template <fs::file_type type> duk_ret_t is_filetype(duk_context *ctx) {
  duk_push_this(ctx);
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("type"));
  duk_push_boolean(ctx, duk_require_uint(ctx, -1) == (duk_uint_t)type);
  return 1;
}

static std::random_device rand_dev;
static std::mt19937 rand_gen(rand_dev());

static inline unsigned int duk_get_unique_id(duk_context *ctx, duk_idx_t idx) {
  std::uniform_int_distribution<unsigned int> dis;
  unsigned int num;
  do
    num = dis(rand_gen);
  while (duk_has_prop_index(ctx, idx, num));
  return num;
}

static inline void lib_common(duk_context *ctx) {
  static auto timer_handler = holder<std::shared_ptr<epoll>>()->reg([ctx](const epoll_event &ev) {
    uint64_t tmp;
    itimerspec spec;
    read(ev.data.fd, &tmp, sizeof tmp);
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("timer"));
    duk_get_prop_index(ctx, -1, ev.data.fd);
    auto rc = duk_pcall(ctx, 0);
    if (rc != DUK_EXEC_SUCCESS) { std::cerr << duk_safe_to_string(ctx, -1) << std::endl; }
    duk_pop(ctx);
    timerfd_gettime(ev.data.fd, &spec);
    if (spec.it_interval.tv_nsec == 0 && spec.it_interval.tv_sec == 0) {
      duk_del_prop_index(ctx, -1, ev.data.fd);
      holder<std::shared_ptr<epoll>>()->del(ev.data.fd);
      close(ev.data.fd);
    }
    duk_pop(ctx);
  });
  duk_push_bare_object(ctx);
  duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("timer"));
  duk_push_c_function(
      ctx,
      +[](duk_context *ctx) -> duk_ret_t {
        duk_require_function(ctx, 0);
        auto delay      = duk_opt_uint(ctx, 1, 0);
        auto interval   = duk_opt_uint(ctx, 2, 0);
        auto tfd        = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        itimerspec spec = {
          .it_interval = { .tv_sec = (interval * 1000000) / 1000000000, .tv_nsec = (interval * 1000000) % 1000000000 },
          .it_value    = { .tv_sec = (delay * 1000000) / 1000000000, .tv_nsec = (delay * 1000000) % 1000000000 },
        };
        if (!delay && !interval) { spec.it_value.tv_nsec = 1; }
        timerfd_settime(tfd, 0, &spec, nullptr);
        holder<std::shared_ptr<epoll>>()->add(EPOLLIN, tfd, timer_handler);
        duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("timer"));
        duk_dup(ctx, 0);
        duk_put_prop_index(ctx, -2, tfd);
        duk_push_int(ctx, tfd);
        return 1;
      },
      3);
  duk_put_global_string(ctx, "timer");
  duk_push_c_function(
      ctx,
      +[](duk_context *ctx) -> duk_ret_t {
        auto tfd = duk_require_int(ctx, 0);
        duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("timer"));
        if (!duk_has_prop_index(ctx, -1, tfd)) duk_range_error(ctx, "invalid timer handler");
        duk_del_prop_index(ctx, -1, tfd);
        holder<std::shared_ptr<epoll>>()->del(tfd);
        close(tfd);
        return 0;
      },
      1);
  duk_put_global_string(ctx, "clearTimer");
  duk_push_c_function(
      ctx,
      +[](duk_context *ctx) -> duk_ret_t {
        auto max = duk_get_top(ctx);
        for (int i = 0; i < max; i++) { std::cout << duk_safe_to_string(ctx, i); }
        std::cout << std::endl;
        return 0;
      },
      DUK_VARARGS);
  duk_put_global_string(ctx, "debug");
}

static inline void lib_fs(duk_context *ctx) {
  duk_push_object(ctx);
  {
    duk_push_object(ctx);
    duk_number_list_entry temp[] = {
      COPY_DEF(F_OK),
      COPY_DEF(R_OK),
      COPY_DEF(W_OK),
      COPY_DEF(X_OK),
      { "COPYFILE_EXCL", (double)1 },
      { "COPYFILE_FICLONE", (double)2 },
      { "COPYFILE_FICLONE_FORCE", (double)4 },
      COPY_DEF(O_RDONLY),
      COPY_DEF(O_WRONLY),
      COPY_DEF(O_RDWR),
      COPY_DEF(O_CREAT),
      COPY_DEF(O_EXCL),
      COPY_DEF(O_NOCTTY),
      COPY_DEF(O_TRUNC),
      COPY_DEF(O_APPEND),
      COPY_DEF(O_DIRECTORY),
      COPY_DEF(O_NOATIME),
      COPY_DEF(O_NOFOLLOW),
      COPY_DEF(O_SYNC),
      COPY_DEF(O_DSYNC),
      COPY_DEF(O_DIRECT),
      COPY_DEF(O_NONBLOCK),
      COPY_DEF(S_IFMT),
      COPY_DEF(S_IFREG),
      COPY_DEF(S_IFDIR),
      COPY_DEF(S_IFCHR),
      COPY_DEF(S_IFBLK),
      COPY_DEF(S_IFIFO),
      COPY_DEF(S_IFLNK),
      COPY_DEF(S_IFSOCK),
      COPY_DEF(S_IRWXU),
      COPY_DEF(S_IRUSR),
      COPY_DEF(S_IWUSR),
      COPY_DEF(S_IXUSR),
      COPY_DEF(S_IRWXG),
      COPY_DEF(S_IRGRP),
      COPY_DEF(S_IWGRP),
      COPY_DEF(S_IXGRP),
      COPY_DEF(S_IRWXO),
      COPY_DEF(S_IROTH),
      COPY_DEF(S_IWOTH),
      COPY_DEF(S_IXOTH),
      { nullptr, 0.0 },
    };
    duk_put_number_list(ctx, -1, temp);
  }
  duk_put_prop_string(ctx, -2, "constants");
  {
    duk_push_c_function(
        ctx,
        +[](duk_context *ctx) -> duk_ret_t {
          if (!duk_is_constructor_call(ctx)) return DUK_RET_TYPE_ERROR;
          duk_push_this(ctx);
          duk_dup(ctx, 0);
          duk_put_prop_string(ctx, -2, "name");
          duk_dup(ctx, 1);
          duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("type"));
          return 0;
        },
        2);
    duk_push_object(ctx);
    duk_function_list_entry temp[] = {
      { "isBlockDevice", is_filetype<fs::file_type::block>, 0 },    { "isCharacterDevice", is_filetype<fs::file_type::character>, 0 },
      { "isDirectory", is_filetype<fs::file_type::directory>, 0 },  { "isFIFO", is_filetype<fs::file_type::fifo>, 0 },
      { "isFile", is_filetype<fs::file_type::regular>, 0 },         { "isSocket", is_filetype<fs::file_type::socket>, 0 },
      { "isSymbolicLink", is_filetype<fs::file_type::symlink>, 0 }, { nullptr, nullptr, 0 },
    };
    duk_put_function_list(ctx, -1, temp);
    duk_put_prop_string(ctx, -2, "prototype");
  }
  static auto fsDirect = duk_get_heapptr(ctx, -1);
  duk_put_prop_string(ctx, -2, "Direct");
  {
    duk_function_list_entry temp[] = {
      { "accessSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto name  = duk_require_string(ctx, 0);
          auto flags = F_OK;
          if (!duk_is_undefined(ctx, 1)) { flags = duk_require_uint(ctx, 1); }
          auto rc = access(name, flags);
          if (rc == 0) return 0;
          duk_generic_error(ctx, "access error: %s", strerror(errno));
          errno = 0;
          return duk_throw(ctx);
        },
        2 },
      { "appendFileSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path = duk_require_string(ctx, 0);
          int mode  = 0666;
          int flag  = O_WRONLY | O_CREAT | O_APPEND;
          int rc;
          if (duk_is_buffer(ctx, 1)) {
            duk_size_t size;
            auto data = duk_get_buffer(ctx, 1, &size);
            if (duk_is_object(ctx, 2)) {
              if (duk_has_prop_string(ctx, 2, "mode")) {
                duk_get_prop_string(ctx, 2, "mode");
                mode = duk_require_uint(ctx, -1);
                duk_pop(ctx);
              }
              if (duk_has_prop_string(ctx, 2, "flag")) {
                duk_get_prop_string(ctx, 2, "flag");
                auto temp = duk_require_string(ctx, -1);
                if (auto it = fopenmap.find(temp); it != fopenmap.end()) {
                  flag = it->second;
                } else {
                  duk_generic_error(ctx, "append failed: invalid flag: %s", temp);
                  return duk_throw(ctx);
                }
                duk_pop(ctx);
              }
            }
            auto file = unix_file(path, flag, mode);
            if (!file) {
              duk_generic_error(ctx, "open failed: %s", strerror(errno));
              errno = 0;
              return duk_throw(ctx);
            }
            rc = write(file, data, size);
            if (rc == -1) {
              duk_generic_error(ctx, "write failed: %s", strerror(errno));
              errno = 0;
              return duk_throw(ctx);
            }
          } else if (duk_is_string(ctx, 1)) {
            duk_size_t size;
            auto data = duk_get_lstring(ctx, 1, &size);
            if (duk_is_object(ctx, 2)) {
              if (duk_has_prop_string(ctx, 2, "mode")) {
                duk_get_prop_string(ctx, 2, "mode");
                mode = duk_require_uint(ctx, -1);
                duk_pop(ctx);
              }
              if (duk_has_prop_string(ctx, 2, "flag")) {
                duk_get_prop_string(ctx, 2, "flag");
                auto temp = duk_require_string(ctx, -1);
                if (auto it = fopenmap.find(temp); it != fopenmap.end()) {
                  flag = it->second;
                } else {
                  duk_generic_error(ctx, "append failed: invalid flag: %s", temp);
                  return duk_throw(ctx);
                }
                duk_pop(ctx);
              }
              if (duk_has_prop_string(ctx, 2, "encoding")) {
                auto encoding = duk_get_string(ctx, 2);
                if (strcmp(encoding, "utf8") != 0) {
                  duk_generic_error(ctx, "not support encoding: %s", encoding);
                  return duk_throw(ctx);
                }
              }
            }
            auto file = unix_file(path, flag, mode);
            if (!file) {
              duk_generic_error(ctx, "open failed: %s", strerror(errno));
              errno = 0;
              return duk_throw(ctx);
            }
            rc = write(file, data, size);
            if (rc == -1) {
              duk_generic_error(ctx, "write failed: %s", strerror(errno));
              errno = 0;
              return duk_throw(ctx);
            }
          }
          return 0;
        },
        3 },
      { "chmodSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path = duk_require_string(ctx, 0);
          auto mode = duk_get_uint(ctx, 1);
          auto rc   = chmod(path, mode);
          if (rc == -1) {
            duk_generic_error(ctx, "chmod failed: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          return 0;
        },
        2 },
      { "statSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path = duk_require_string(ctx, 0);
          struct stat64 s;
          auto rc = stat64(path, &s);
          if (rc == -1) {
            duk_generic_error(ctx, "lstat64 failed: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          duk_push_stat(ctx, s);
          return 1;
        },
        1 },
      { "lstatSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path = duk_require_string(ctx, 0);
          struct stat64 s;
          auto rc = lstat64(path, &s);
          if (rc == -1) {
            duk_generic_error(ctx, "lstat64 failed: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          duk_push_stat(ctx, s);
          return 1;
        },
        1 },
      { "chownSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path = duk_require_string(ctx, 0);
          auto uid  = duk_get_uint(ctx, 1);
          auto gid  = duk_get_uint(ctx, 2);
          auto rc   = chown(path, uid, gid);
          if (rc == -1) {
            duk_generic_error(ctx, "chown failed: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          return 0;
        },
        3 },
      { "lchownSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path = duk_require_string(ctx, 0);
          auto uid  = duk_get_uint(ctx, 1);
          auto gid  = duk_get_uint(ctx, 2);
          auto rc   = lchown(path, uid, gid);
          if (rc == -1) {
            duk_generic_error(ctx, "chown failed: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          return 0;
        },
        3 },
      { "copyFileSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto src      = duk_require_string(ctx, 0);
          auto dst      = duk_require_string(ctx, 1);
          auto flags    = duk_opt_uint(ctx, 2, 0);
          auto src_file = unix_file(src, O_RDONLY);
          if (!src_file) {
            duk_generic_error(ctx, "failed to open src: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          auto dst_file = unix_file(dst, O_WRONLY | (flags & 1 ? O_EXCL : O_CREAT), 0666);
          if (!dst_file) {
            duk_generic_error(ctx, "failed to open dst: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          if (flags & 6) {
            if (ioctl(dst_file, FICLONE, (int)src_file) == -1) {
              if (flags & 4) {
                duk_generic_error(ctx, "failed to reflink: %s", strerror(errno));
                errno = 0;
                return duk_throw(ctx);
              }
            } else {
              return 0;
            }
          }
          struct stat64 s;
          auto rc = fstat64(src_file, &s);
          if (rc == -1) {
            duk_generic_error(ctx, "failed to stat file: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          rc = sendfile64(dst_file, src_file, 0, s.st_size);
          if (rc == -1) {
            duk_generic_error(ctx, "failed to copy file: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          return 0;
        },
        3 },
      { "existsSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path = duk_require_string(ctx, 0);
          duk_push_boolean(ctx, access(path, F_OK) == 0);
          errno = 0;
          return 1;
        },
        1 },
      { "linkSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto src = duk_require_string(ctx, 0);
          auto dst = duk_require_string(ctx, 1);
          auto rc  = link(src, dst);
          if (rc == -1) {
            duk_generic_error(ctx, "failed to link file: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          return 0;
        },
        2 },
      { "mkdirSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path   = duk_require_string(ctx, 0);
          bool rec    = false;
          mode_t mode = 0777;
          if (!duk_is_undefined(ctx, 1)) {
            duk_require_object(ctx, 1);
            if (duk_has_prop_string(ctx, 1, "recursive")) {
              duk_get_prop_string(ctx, 1, "recursive");
              rec = duk_require_boolean(ctx, -1);
              duk_pop(ctx);
            }
            if (duk_has_prop_string(ctx, 1, "mode")) {
              duk_get_prop_string(ctx, 1, "mode");
              mode = duk_require_uint(ctx, -1);
              duk_pop(ctx);
            }
          }
          int rc = 0;
          if (rec) {
            fs::path fullpath = path;
            unix_file fd      = AT_FDCWD;
            for (auto part : fullpath) {
              struct stat64 s;
              rc = fstatat64(fd, part.c_str(), &s, 0);
              if (rc != 0) {
                if (errno != ENOENT) {
                  duk_generic_error(ctx, "failed to stat: %s", strerror(errno));
                  errno = 0;
                  return duk_throw(ctx);
                }
                errno = 0;
              } else {
                fd = openat64(fd, part.c_str(), O_DIRECTORY);
                if (!fd) {
                  duk_generic_error(ctx, "failed to open: %s", strerror(errno));
                  errno = 0;
                  return duk_throw(ctx);
                }
                continue;
              }
              rc = mkdirat(fd, part.c_str(), mode);
              if (rc != 0) {
                duk_generic_error(ctx, "failed to mkdir: %s", strerror(errno));
                errno = 0;
                return duk_throw(ctx);
              }
              fd = openat64(fd, part.c_str(), O_DIRECTORY);
              if (!fd) {
                duk_generic_error(ctx, "failed to open: %s", strerror(errno));
                errno = 0;
                return duk_throw(ctx);
              }
            }
          } else {
            rc = mkdir(path, mode);
            if (rc != 0) {
              duk_generic_error(ctx, "failed to mkdir: %s", strerror(errno));
              errno = 0;
              return duk_throw(ctx);
            }
          }
          return 0;
        },
        2 },
      { "mkdtempSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto temp = strdup(duk_require_string(ctx, 0));
          defer free_temp{ [=] { free(temp); } };
          if (!mkdtemp(temp)) {
            duk_generic_error(ctx, "failed to mkdtemp: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          duk_push_string(ctx, temp);
          return 1;
        },
        1 },
      { "readdirSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path      = duk_require_string(ctx, 0);
          bool withFileTypes = false;
          if (!duk_is_undefined(ctx, 1)) {
            duk_require_object(ctx, 1);
            if (duk_has_prop_string(ctx, 1, "withFileTypes")) {
              duk_get_prop_string(ctx, 1, "withFileTypes");
              withFileTypes = duk_get_boolean(ctx, -1);
              duk_pop(ctx);
            }
          }
          auto arr          = duk_push_array(ctx);
          duk_uarridx_t idx = 0;
          try {
            for (auto it : fs::directory_iterator(path)) {
              if (withFileTypes) {
                it.status().type();
                duk_push_heapptr(ctx, fsDirect);
                duk_push_string(ctx, it.path().filename().c_str());
                duk_push_uint(ctx, (duk_uint_t)it.status().type());
                duk_new(ctx, 2);
                duk_put_prop_index(ctx, arr, idx);
              } else {
                duk_push_string(ctx, it.path().filename().c_str());
                duk_put_prop_index(ctx, arr, idx);
              }
              idx++;
            }
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 1;
        },
        2 },
      { "readFileSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path  = duk_require_string(ctx, 0);
          bool as_string = false;
          if (!duk_is_undefined(ctx, 1)) {
            duk_require_object(ctx, 1);
            if (duk_has_prop_string(ctx, 1, "encoding")) {
              duk_get_prop_string(ctx, 1, "encoding");
              auto encoding = duk_require_string(ctx, -1);
              duk_pop(ctx);
              if (strcmp(encoding, "utf8") != 0) {
                duk_generic_error(ctx, "not support encoding: %s", encoding);
                return duk_throw(ctx);
              }
              as_string = true;
            }
          }
          try {
            std::ifstream t(path);
            std::string str((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
            if (as_string) {
              duk_push_string(ctx, str.c_str());
            } else {
              duk_push_fixed_buffer(ctx, str.size());
              strncpy((char *)duk_get_buffer(ctx, -1, nullptr), str.data(), str.length());
            }
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 1;
        },
        2 },
      { "readlinkSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path = duk_require_string(ctx, 0);
          try {
            auto resolved = fs::read_symlink(path);
            duk_push_string(ctx, resolved.c_str());
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 1;
        },
        1 },
      { "realpathSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path = duk_require_string(ctx, 0);
          try {
            auto resolved = fs::absolute(path);
            duk_push_string(ctx, resolved.c_str());
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 1;
        },
        1 },
      { "renameSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path oldpath = duk_require_string(ctx, 0);
          fs::path newpath = duk_require_string(ctx, 1);
          try {
            fs::rename(oldpath, newpath);
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 0;
        },
        2 },
      { "rmdirSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path = duk_require_string(ctx, 0);
          try {
            fs::remove(path);
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 0;
        },
        1 },
      { "symlinkSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path target = duk_require_string(ctx, 0);
          fs::path path   = duk_require_string(ctx, 1);
          try {
            fs::create_symlink(target, path);
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 0;
        },
        2 },
      { "truncateSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path = duk_require_string(ctx, 0);
          auto len      = duk_require_uint(ctx, 1);
          try {
            fs::resize_file(path, len);
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 0;
        },
        2 },
      { "unlinkSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path = duk_require_string(ctx, 0);
          try {
            fs::remove(path);
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 0;
        },
        1 },
      { "utimesSync",
        +[](duk_context *ctx) -> duk_ret_t {
          auto path   = duk_require_string(ctx, 0);
          auto atime  = duk_require_uint(ctx, 1);
          auto mtime  = duk_require_uint(ctx, 2);
          utimbuf buf = {
            .actime  = atime,
            .modtime = mtime,
          };
          auto rc = utime(path, &buf);
          if (rc != 0) {
            duk_generic_error(ctx, "failed to utime: %s", strerror(errno));
            errno = 0;
            return duk_throw(ctx);
          }
          return 0;
        },
        3 },
      { "writeFileSync",
        +[](duk_context *ctx) -> duk_ret_t {
          fs::path path = duk_require_string(ctx, 0);
          mode_t mode   = 0666;
          if (!duk_is_undefined(ctx, 2)) {
            duk_require_object(ctx, 2);
            if (duk_has_prop_string(ctx, 2, "mode")) {
              duk_get_prop_string(ctx, 2, "mode");
              mode = duk_get_uint(ctx, -1);
              duk_pop(ctx);
            }
          }
          char const *data = nullptr;
          size_t len       = 0;
          if (duk_is_string(ctx, 1)) {
            data = duk_get_lstring(ctx, 1, &len);
          } else if (duk_is_buffer(ctx, 1)) {
            data = (char const *)duk_get_buffer(ctx, 1, &len);
          } else if (duk_is_buffer_data(ctx, 1)) {
            data = (char const *)duk_get_buffer_data(ctx, 1, &len);
          }
          try {
            std::ofstream ofs(path);
            ofs.write(data, len);
          } catch (fs::filesystem_error &e) {
            duk_generic_error(ctx, "%s", e.what());
            return duk_throw(ctx);
          }
          return 0;
        },
        1 },
      { nullptr, nullptr, 0 },
    };
    duk_put_function_list(ctx, -1, temp);
  }
  duk_put_global_string(ctx, "fs");
}

static inline void lib_os(duk_context *ctx) {
  duk_push_object(ctx);
  duk_push_string(ctx, "\n");
  duk_put_prop_string(ctx, -2, "EOL");
  {
    duk_push_object(ctx);
    duk_number_list_entry sig_list[] = {
      COPY_DEF(SIGHUP),   COPY_DEF(SIGINT),  COPY_DEF(SIGQUIT), COPY_DEF(SIGILL),    COPY_DEF(SIGTRAP), COPY_DEF(SIGABRT),      COPY_DEF(SIGIOT),
      COPY_DEF(SIGBUS),   COPY_DEF(SIGFPE),  COPY_DEF(SIGKILL), COPY_DEF(SIGUSR1),   COPY_DEF(SIGUSR2), COPY_DEF(SIGSEGV),      COPY_DEF(SIGPIPE),
      COPY_DEF(SIGALRM),  COPY_DEF(SIGTERM), COPY_DEF(SIGCHLD), COPY_DEF(SIGSTKFLT), COPY_DEF(SIGCONT), COPY_DEF(SIGSTOP),      COPY_DEF(SIGTSTP),
      COPY_DEF(SIGTTIN),  COPY_DEF(SIGTTOU), COPY_DEF(SIGURG),  COPY_DEF(SIGXCPU),   COPY_DEF(SIGXFSZ), COPY_DEF(SIGVTALRM),    COPY_DEF(SIGPROF),
      COPY_DEF(SIGWINCH), COPY_DEF(SIGIO),   COPY_DEF(SIGPOLL), COPY_DEF(SIGPWR),    COPY_DEF(SIGSYS),  { nullptr, (double)0 },
    };
    duk_put_number_list(ctx, -1, sig_list);
    duk_put_prop_string(ctx, -2, "constants");
  }
  duk_function_list_entry funcs[] = {
    { "cpus",
      +[](duk_context *ctx) -> duk_ret_t {
        std::ifstream cpustat{ "/proc/stat" };
        std::ifstream cpuinfo{ "/proc/cpuinfo" };
        duk_push_array(ctx);
        auto idx = -1;
        for (std::string line; std::getline(cpustat, line); idx++) {
          std::string_view sv(line);
          if (sv.substr(0, 3) == "cpu") {
            if (idx != -1) {
              duk_push_object(ctx);
              for (std::string infoline; std::getline(cpuinfo, infoline);) {
                std::string_view ilsv(infoline);
                if (ilsv.find("model name\t: ") == 0) {
                  ilsv.remove_prefix(sizeof "model name\t: ");
                  duk_push_string(ctx, ilsv.data());
                  duk_put_prop_string(ctx, -2, "model");
                } else if (ilsv.find("cpu MHz\t\t: ") == 0) {
                  ilsv.remove_prefix(sizeof "cpu MHz\t\t: ");
                  duk_push_number(ctx, atof(ilsv.data()));
                  duk_put_prop_string(ctx, -2, "speed");
                  break;
                }
              }
              sv.remove_prefix(sv.find(' ') + 1);
              {
                uint64_t clock_ticks = sysconf(_SC_CLK_TCK);
                uint64_t user;
                uint64_t nice;
                uint64_t sys;
                uint64_t idle;
                uint64_t dummy;
                uint64_t irq;
                sscanf(sv.data(), "%" PRIu64 " %" PRIu64 " %" PRIu64 "%" PRIu64 " %" PRIu64 " %" PRIu64, &user, &nice, &sys, &idle, &dummy, &irq);
                duk_push_object(ctx);
                duk_number_list_entry time_list[] = {
                  { "user", (double)user * clock_ticks }, { "nice", (double)nice * clock_ticks }, { "sys", (double)sys * clock_ticks },
                  { "idle", (double)idle * clock_ticks }, { "irq", (double)irq * clock_ticks },   { nullptr, (double)0 },
                };
                duk_put_number_list(ctx, -1, time_list);
                duk_put_prop_string(ctx, -2, "times");
              }
              duk_put_prop_index(ctx, -2, idx);
            }
          } else
            break;
        }
        return 1;
      },
      0 },
    { "endianness",
      +[](duk_context *ctx) -> duk_ret_t {
        if (std::endian::native == std::endian::big) {
          duk_push_string(ctx, "BE");
        } else {
          duk_push_string(ctx, "LE");
        }
        return 1;
      },
      0 },
    { "freemem",
      +[](duk_context *ctx) -> duk_ret_t {
        struct sysinfo info;
        sysinfo(&info);
        duk_push_uint(ctx, info.freeram);
        return 1;
      },
      0 },
    { "totalmem",
      +[](duk_context *ctx) -> duk_ret_t {
        struct sysinfo info;
        sysinfo(&info);
        duk_push_uint(ctx, info.totalram);
        return 1;
      },
      0 },
    { "uptime",
      +[](duk_context *ctx) -> duk_ret_t {
        struct sysinfo info;
        sysinfo(&info);
        duk_push_uint(ctx, info.uptime);
        return 1;
      },
      0 },
    { "getPriority",
      +[](duk_context *ctx) -> duk_ret_t {
        auto pid = duk_opt_uint(ctx, 0, getpid());
        auto r   = getpriority(PRIO_PROCESS, (int)pid);
        duk_push_uint(ctx, r);
        return 1;
      },
      1 },
    { "setPriority",
      +[](duk_context *ctx) -> duk_ret_t {
        auto priority = 0;
        auto pid      = getpid();
        if (!duk_is_undefined(ctx, 1)) {
          pid      = duk_require_uint(ctx, 0);
          priority = duk_require_int(ctx, 1);
        } else {
          priority = duk_require_int(ctx, 0);
        }
        auto rc = setpriority(PRIO_PROCESS, (int)pid, priority);
        if (rc != 0) {
          duk_generic_error(ctx, "failed to setpriority: %s", strerror(errno));
          errno = 0;
          return duk_throw(ctx);
        }
        return 0;
      },
      2 },
    { "homedir",
      +[](duk_context *ctx) -> duk_ret_t {
        duk_push_string(ctx, getenv("HOME") ?: "/");
        return 1;
      },
      0 },
    { "tmpdir",
      +[](duk_context *ctx) -> duk_ret_t {
        duk_push_string(ctx, getenv("TEMP") ?: "/tmp");
        return 1;
      },
      0 },
    { "type",
      +[](duk_context *ctx) -> duk_ret_t {
        duk_push_string(ctx, "Linux");
        return 1;
      },
      0 },
    { "hostname",
      +[](duk_context *ctx) -> duk_ret_t {
        char hostname[HOST_NAME_MAX];
        gethostname(hostname, sizeof hostname);
        duk_push_string(ctx, hostname);
        return 1;
      },
      0 },
    { "loadavg",
      +[](duk_context *ctx) -> duk_ret_t {
        struct sysinfo info;
        sysinfo(&info);
        duk_push_array(ctx);
        duk_push_number(ctx, info.loads[0] / 65536.0);
        duk_put_prop_index(ctx, -2, 0);
        duk_push_number(ctx, info.loads[1] / 65536.0);
        duk_put_prop_index(ctx, -2, 1);
        duk_push_number(ctx, info.loads[2] / 65536.0);
        duk_put_prop_index(ctx, -2, 2);
        return 1;
      },
      0 },
    { "platform",
      +[](duk_context *ctx) -> duk_ret_t {
        duk_push_string(ctx, "linux");
        return 1;
      },
      0 },
    { "release",
      +[](duk_context *ctx) -> duk_ret_t {
        struct utsname buf;
        auto rc = uname(&buf);
        if (rc != 0) {
          duk_generic_error(ctx, "failed to uname: %s", strerror(errno));
          errno = 0;
          return duk_throw(ctx);
        }
        duk_push_string(ctx, buf.release);
        return 1;
      },
      0 },
    { nullptr, nullptr, 0 },
  };
  duk_put_function_list(ctx, -1, funcs);
  duk_put_global_string(ctx, "os");
}

static inline void lib_rpc(duk_context *ctx) {
  duk_push_c_function(
      ctx,
      +[](duk_context *ctx) -> duk_ret_t {
        if (!duk_is_constructor_call(ctx)) return DUK_RET_TYPE_ERROR;
        duk_push_this(ctx);
        auto addr = duk_require_string(ctx, 0);
        duk_require_function(ctx, 1);
        try {
          auto io = std::make_unique<rpcws::client_wsio>(addr, holder<std::shared_ptr<epoll>>());
          duk_push_pointer(ctx, new rpcws::RPC::Client{ std::move(io) });
          duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("obj"));
          duk_push_bare_object(ctx);
          duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("event"));
          duk_push_bare_object(ctx);
          duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("callback"));
          duk_push_false(ctx);
          duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("started"));
          duk_dup(ctx, 1);
          duk_put_prop_string(ctx, 2, DUK_HIDDEN_SYMBOL("error"));
          duk_push_c_function(
              ctx,
              +[](duk_context *ctx) -> duk_ret_t {
                duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("obj"));
                delete (rpcws::RPC::Client *)duk_get_pointer(ctx, -1);
                return 0;
              },
              1);
          duk_set_finalizer(ctx, -2);
        } catch (std::exception &e) {
          duk_generic_error(ctx, "%s", e.what());
          return duk_throw(ctx);
        }
        return 0;
      },
      2);
  duk_push_object(ctx);
  duk_function_list_entry funcs[] = {
    { "start",
      +[](duk_context *ctx) -> duk_ret_t {
        duk_require_function(ctx, 0);
        duk_push_this(ctx);
        auto self = duk_get_heapptr(ctx, -1);
        duk_get_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("obj"));
        auto &it = *(rpcws::RPC::Client *)duk_get_pointer(ctx, 2);
        duk_pop(ctx);
        duk_get_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("started"));
        if (duk_get_boolean(ctx, 2)) {
          duk_generic_error(ctx, "rpc client started");
          return duk_throw(ctx);
        }
        duk_pop(ctx);
        duk_dup(ctx, 0);
        duk_put_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("connected"));
        duk_push_true(ctx);
        duk_put_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("started"));
        it.start()
            .then([=] {
              assert(duk_get_top(ctx) == 0);
              duk_push_heapptr(ctx, self);
              duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("connected"));
              duk_dup(ctx, 0);
              if (duk_pcall_method(ctx, 0) != DUK_EXEC_SUCCESS) { std::cerr << duk_safe_to_string(ctx, -1) << std::endl; }
              duk_pop_n(ctx, duk_get_top(ctx));
            })
            .fail([=](std::exception_ptr e) {
              assert(duk_get_top(ctx) == 0);
              duk_push_heapptr(ctx, self);
              duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("error"));
              duk_dup(ctx, 0);
              try {
                std::rethrow_exception(e);
              } catch (std::exception &e) { duk_generic_error(ctx, "%s", e.what()); }
              if (duk_pcall_method(ctx, 1) != DUK_EXEC_SUCCESS) { std::cerr << duk_safe_to_string(ctx, -1) << std::endl; }
              duk_pop_n(ctx, duk_get_top(ctx));
            });
        return 0;
      },
      1 },
    { "stop",
      +[](duk_context *ctx) -> duk_ret_t {
        duk_push_this(ctx);
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("obj"));
        auto &it = *(rpcws::RPC::Client *)duk_get_pointer(ctx, 1);
        it.stop();
        return 0;
      },
      0 },
    { "call",
      +[](duk_context *ctx) -> duk_ret_t {
        auto name = duk_require_string(ctx, 0);
        duk_require_object(ctx, 1);
        duk_require_function(ctx, 2);
        duk_push_this(ctx);
        auto self = duk_get_heapptr(ctx, -1);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("obj"));
        auto &it = *(rpcws::RPC::Client *)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("callback"));
        auto uid = duk_get_unique_id(ctx, -1);
        duk_dup(ctx, 2);
        duk_put_prop_index(ctx, -2, uid);
        duk_pop(ctx);
        duk_pop(ctx);
        auto data = duk_get_json(ctx, 1);
        it.call(name, data)
            .then([=](auto ret) {
              assert(duk_get_top(ctx) == 0);
              duk_push_heapptr(ctx, self);
              duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("callback"));
              duk_get_prop_index(ctx, 1, uid);
              duk_del_prop_index(ctx, 1, uid);
              duk_dup(ctx, 0);
              duk_push_undefined(ctx);
              duk_push_json(ctx, ret);
              if (duk_pcall_method(ctx, 2) != DUK_EXEC_SUCCESS) { std::cerr << duk_safe_to_string(ctx, -1) << std::endl; }
              duk_pop_n(ctx, duk_get_top(ctx));
            })
            .fail([=](auto e) {
              assert(duk_get_top(ctx) == 0);
              duk_push_heapptr(ctx, self);
              duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("callback"));
              duk_get_prop_index(ctx, 1, uid);
              duk_del_prop_index(ctx, 1, uid);
              duk_dup(ctx, 0);
              try {
                std::rethrow_exception(e);
              } catch (std::exception &e) { duk_generic_error(ctx, "%s", e.what()); }
              if (duk_pcall_method(ctx, 1) != DUK_EXEC_SUCCESS) { std::cerr << duk_safe_to_string(ctx, -1) << std::endl; }
              duk_pop_n(ctx, duk_get_top(ctx));
            });
        return 0;
      },
      3 },
    { "on",
      +[](duk_context *ctx) -> duk_ret_t {
        auto name = duk_require_string(ctx, 0);
        duk_require_function(ctx, 1);
        duk_push_this(ctx);
        auto self = duk_get_heapptr(ctx, -1);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("obj"));
        auto &it = *(rpcws::RPC::Client *)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("event"));
        duk_dup(ctx, 0);
        if (duk_has_prop(ctx, -2)) {
          duk_generic_error(ctx, "key '%s' exists", name);
          return duk_throw(ctx);
        }
        duk_dup(ctx, 0);
        duk_dup(ctx, 1);
        duk_put_prop(ctx, -3);
        it.on(name, [=, xname = std::string{ name }](auto data) {
            assert(duk_get_top(ctx) == 0);
            duk_push_heapptr(ctx, self);
            duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("event"));
            duk_get_prop_string(ctx, 1, xname.c_str());
            duk_dup(ctx, 0);
            duk_push_string(ctx, xname.c_str());
            duk_push_json(ctx, data);
            if (duk_pcall_method(ctx, 2) != DUK_EXEC_SUCCESS) { std::cerr << duk_safe_to_string(ctx, -1) << std::endl; }
            duk_pop_n(ctx, duk_get_top(ctx));
          });
        return 0;
      },
      2 },
    { "off",
      +[](duk_context *ctx) -> duk_ret_t {
        auto name = duk_require_string(ctx, 0);
        duk_push_this(ctx);
        auto self = duk_get_heapptr(ctx, -1);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("obj"));
        auto &it = *(rpcws::RPC::Client *)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("event"));
        duk_dup(ctx, 0);
        if (!duk_has_prop(ctx, -2)) {
          duk_generic_error(ctx, "key '%s' not exists", name);
          return duk_throw(ctx);
        }
        it.off(name);
        duk_dup(ctx, 0);
        duk_del_prop(ctx, -2);
        return 0;
      },
      2 },
    { nullptr, nullptr, 0 },
  };
  duk_put_function_list(ctx, -1, funcs);
  duk_put_prop_string(ctx, -2, "prototype");
  duk_put_global_string(ctx, "rpc");
}

void init_duk_stdlib(duk_context *ctx) {
  lib_common(ctx);
  assert(duk_get_top(ctx) == 0);
  lib_fs(ctx);
  assert(duk_get_top(ctx) == 0);
  lib_os(ctx);
  assert(duk_get_top(ctx) == 0);
  lib_rpc(ctx);
  assert(duk_get_top(ctx) == 0);
}