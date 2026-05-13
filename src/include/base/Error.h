#ifndef PROJECT_XS_BASE_ERROR_H
#define PROJECT_XS_BASE_ERROR_H

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

// 诊断严重程度。
enum class Severity {
    Error,
    Warning,
};

// 一条结构化诊断。
struct Diagnostic {
    Severity severity = Severity::Error;
    Stage stage = Stage::Elaboration;
    Kind kind = Kind::ConstraintViolation;
    std::string scope;
    std::string detail;
};

// 返回阶段名字符串。
std::string stage_name(Stage stage);

// 返回错误类别名字符串。
std::string kind_name(Kind kind);

// 返回诊断严重程度字符串。
std::string severity_name(Severity severity);

// 把结构化诊断格式化成统一文本。
std::string format(const Diagnostic& diagnostic);

// 组装统一格式的运行时错误对象。
std::runtime_error make(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail);

// 由结构化诊断直接组装运行时错误对象。
std::runtime_error make(const Diagnostic& diagnostic);

// 直接抛出统一格式错误。
[[noreturn]] void raise(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail);

// 直接抛出结构化诊断。
[[noreturn]] void raise(const Diagnostic& diagnostic);

// 追加一条诊断。
void append(std::vector<Diagnostic>& diagnostics,
            Severity severity,
            Stage stage,
            Kind kind,
            std::string_view scope,
            std::string detail);

// 如果诊断列表中存在 Error，直接抛出第一条 Error。
void throw_if_any_error(const std::vector<Diagnostic>& diagnostics);

}  // namespace project_xs::sim::error

#endif
