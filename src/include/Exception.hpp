#ifndef EXCEPTION_H_2048
#define EXCEPTION_H_2048

#include <string>
#include <exception>

namespace Http {

class Exception : public std::exception {
public:
  virtual const char* what() const noexcept override {
    return errorMessage.c_str();
  }

protected:
  std::string errorMessage;
};

}

#endif