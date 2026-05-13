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

std::string severity_name(Severity severity) {
    switch (severity) {
        case Severity::Warning:
            return "warning";
        case Severity::Error:
        default:
            return "error";
    }
}

std::string format(const Diagnostic& diagnostic) {
    std::string text = "[" + severity_name(diagnostic.severity) + "][" +
                       stage_name(diagnostic.stage) + "][" +
                       kind_name(diagnostic.kind) + "]";
    if (!diagnostic.scope.empty()) {
        text += "[" + diagnostic.scope + "]";
    }
    text += " " + diagnostic.detail;
    return text;
}

std::runtime_error make(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail) {
    return make(Diagnostic{
        Severity::Error,
        stage,
        kind,
        std::string(scope),
        std::move(detail),
    });
}

std::runtime_error make(const Diagnostic& diagnostic) {
    return std::runtime_error(format(diagnostic));
}

[[noreturn]] void raise(Stage stage,
                        Kind kind,
                        std::string_view scope,
                        std::string detail) {
    throw make(stage, kind, scope, std::move(detail));
}

[[noreturn]] void raise(const Diagnostic& diagnostic) {
    throw make(diagnostic);
}

void append(std::vector<Diagnostic>& diagnostics,
            Severity severity,
            Stage stage,
            Kind kind,
            std::string_view scope,
            std::string detail) {
    diagnostics.push_back(Diagnostic{
        severity,
        stage,
        kind,
        std::string(scope),
        std::move(detail),
    });
}

void throw_if_any_error(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == Severity::Error) {
            throw make(diagnostic);
        }
    }
}

}  // namespace project_xs::sim::error
