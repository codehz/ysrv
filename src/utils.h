#pragma once
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h>

inline std::string GetEnvironmentVariableOrDefault(const std::string &variable_name, const std::string &default_value) {
  const char *value = getenv(variable_name.c_str());
  return value ? value : default_value;
}

#define LOAD_ENV(env, def) static const auto env = GetEnvironmentVariableOrDefault(#env, def)

template <typename T> class holder {
  static T *&ptr() {
    static T *target;
    return target;
  }

public:
  holder() {}
  holder(T &src) { ptr() = &src; }

  auto &operator*() noexcept { return *ptr(); }
  auto const &operator*() const noexcept { return *ptr(); }
  auto operator-> () const noexcept { return ptr(); }
  auto operator&() const noexcept { return ptr(); }

  operator bool() const noexcept { return ptr(); }
  operator T *() const noexcept { return ptr(); }
};

template <typename T> class holder<std::shared_ptr<T>> {
  static std::weak_ptr<T> &ptr() {
    static std::weak_ptr<T> target;
    return target;
  }

public:
  holder() {}
  holder(std::shared_ptr<T> &src) { ptr() = src; }

  auto &operator*() noexcept { return *ptr().lock(); }
  auto const &operator*() const noexcept { return *ptr().lock(); }
  auto operator-> () const noexcept { return ptr().lock(); }
  auto operator&() const noexcept { return ptr().lock(); }
  operator std::shared_ptr<T>() const noexcept { return ptr().lock(); }

  operator bool() const noexcept { return ptr().lock(); }
  operator T *() const noexcept { return ptr().lock().get(); }
};

class unix_file {
  int fd;

public:
  inline unix_file()
      : fd(-1){};
  inline unix_file(int fd)
      : fd(fd){};
  inline unix_file(const char *pathname, int flags)
      : fd(open64(pathname, flags)) {}
  inline unix_file(const char *pathname, int flags, mode_t mode)
      : fd(open64(pathname, flags, mode)) {}
  unix_file(unix_file const &) = delete;
  inline unix_file(unix_file &&rhs) {
    if (fd > 0) close(fd);
    fd     = rhs.fd;
    rhs.fd = -1;
  }
  unix_file &operator=(unix_file const &) = delete;
  inline unix_file &operator              =(unix_file &&rhs) {
    if (fd > 0) close(fd);
    fd     = rhs.fd;
    rhs.fd = -1;
    return *this;
  }
  inline unix_file &operator=(int nfd) {
    if (fd > 0) close(fd);
    fd = nfd;
    return *this;
  }
  inline ~unix_file() {
    if (fd > 0) close(fd);
  }

  inline operator bool() { return fd != -1; }
  inline operator int() { return fd; }
};

template <typename T> struct defer {
  T t;
  ~defer() { t(); }
};

template <typename T> defer(T t)->defer<T>;