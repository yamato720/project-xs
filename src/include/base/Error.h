#ifndef PROJECT_XS_BASE_ERROR_H
#define PROJECT_XS_BASE_ERROR_H

#include <stdexcept>
#include <string>
#include <string_view>

namespace project_xs::sim::error {

// 错误发生阶段。
enum class Stage {
    CompileTime,
    Elaboration,
    Validate,
    Runtime,
    Backend,
    IO,
    Data,
};

// 错误类别。
enum class Kind {
    InvalidArgument,
    NotFound,
    DuplicateName,
    LayoutMismatch,
    TypeMismatch,
    DirectionMismatch,
    ConnectionMismatch,
    ConstraintViolation,
    Unsupported,
    IOError,
    DataFormat,
};

// 返回阶段名字符串。
std::string stage_name(Stage stage);

// 返回错误类别名字符串。
std::string kind_name(Kind kind);

// 组装统一格式的运行时错误对象。
std::runtime_error make(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail);

// 直接抛出统一格式错误。
[[noreturn]] void raise(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail);

}  // namespace project_xs::sim::error

#endif
