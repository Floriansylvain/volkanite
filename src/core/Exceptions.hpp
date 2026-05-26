#ifndef VOLKANITE_EXCEPTIONS_H
#define VOLKANITE_EXCEPTIONS_H

#include <string>

namespace EngineExceptions {

class NotInitialized : public std::logic_error {
  public:
    explicit NotInitialized(const std::string &message) : std::logic_error(message) {}
    explicit NotInitialized(const char *message) : std::logic_error(message) {}
};

class Compatibility : public std::runtime_error {
  public:
    explicit Compatibility(const std::string &message) : std::runtime_error(message) {}
    explicit Compatibility(const char *message) : std::runtime_error(message) {}
};

class Shader : public std::runtime_error {
  public:
    explicit Shader(const std::string &message) : std::runtime_error(message) {}
    explicit Shader(const char *message) : std::runtime_error(message) {}
};

class Render : public std::runtime_error {
  public:
    explicit Render(const std::string &message) : std::runtime_error(message) {}
    explicit Render(const char *message) : std::runtime_error(message) {}
};

} // namespace EngineExceptions

#endif
