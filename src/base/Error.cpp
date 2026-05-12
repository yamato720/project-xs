#include "base/Error.h"

#include <utility>

namespace project_xs::sim::error {

std::string stage_name(Stage stage) {
    switch (stage) {
        case Stage::CompileTime:
            return "compile-time";
        case Stage::Elaboration:
            return "elaboration";
        case Stage::Validate:
            return "validate";
        case Stage::Runtime:
            return "runtime";
        case Stage::Backend:
            return "backend";
        case Stage::IO:
            return "io";
        case Stage::Data:
        default:
            return "data";
    }
}

std::string kind_name(Kind kind) {
    switch (kind) {
        case Kind::InvalidArgument:
            return "invalid-argument";
        case Kind::NotFound:
            return "not-found";
        case Kind::DuplicateName:
            return "duplicate-name";
        case Kind::LayoutMismatch:
            return "layout-mismatch";
        case Kind::TypeMismatch:
            return "type-mismatch";
        case Kind::DirectionMismatch:
            return "direction-mismatch";
        case Kind::ConnectionMismatch:
            return "connection-mismatch";
        case Kind::ConstraintViolation:
            return "constraint";
        case Kind::Unsupported:
            return "unsupported";
        case Kind::IOError:
            return "io";
        case Kind::DataFormat:
        default:
            return "data-format";
    }
}

std::runtime_error make(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail) {
    std::string text = "[" + stage_name(stage) + "][" + kind_name(kind) + "]";
    if (!scope.empty()) {
        text += "[" + std::string(scope) + "]";
    }
    text += " " + std::move(detail);
    return std::runtime_error(text);
}

[[noreturn]] void raise(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail) {
    throw make(stage, kind, scope, std::move(detail));
}

}  // namespace project_xs::sim::error
