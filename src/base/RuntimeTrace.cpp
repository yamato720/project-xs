#include "base/RuntimeTrace.h"

#include "base/CycleSimulator.h"
#include "base/Error.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/PortGroup.h"
#include "base/SimulationSession.h"
#include "base/State.h"
#include "base/StateArray.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace project_xs::sim {
namespace {

std::string long_double_string(long double value);

std::vector<std::string> make_name_options(const std::string& name,
                                           const std::vector<std::string>& aliases = {}) {
    std::vector<std::string> options;
    options.push_back(name);
    for (const auto& alias : aliases) {
        if (!alias.empty() &&
            std::find(options.begin(), options.end(), alias) == options.end()) {
            options.push_back(alias);
        }
    }
    return options;
}

std::vector<std::string> make_array_element_name_options(const StateArrayBase& array,
                                                         std::size_t flat_index) {
    return array.element_name_options(flat_index);
}

void append_state_samples(WaveformFrame& frame,
                          const std::string& scope,
                          std::vector<std::string> name_path,
                          std::vector<std::vector<std::string>> name_options_path,
                          std::vector<std::string> description_path,
                          std::vector<std::string> timing_kind_path,
                          std::vector<long double> timing_frequency_hz_path,
                          std::vector<std::uint64_t> timing_cycle_path,
                          const StateSet& states,
                          PortValueBase base) {
    for (const auto& entry : states.entries()) {
        const StateBase& state = entry->state();
        auto path = name_path;
        path.push_back(state.name());
        auto options_path = name_options_path;
        options_path.push_back(make_name_options(state.name()));
        auto descriptions = description_path;
        descriptions.push_back(state.description());
        auto timing_kinds = timing_kind_path;
        timing_kinds.push_back("");
        auto timing_frequencies = timing_frequency_hz_path;
        timing_frequencies.push_back(0.0L);
        auto timing_cycles = timing_cycle_path;
        timing_cycles.push_back(0);
        frame.signals.push_back(WaveformSignalSample{
            scope,
            state.name(),
            WaveformSignalKind::State,
            state.type_name(),
            state.width_bits(),
            state.value_string(base),
            true,
            std::move(path),
            std::move(options_path),
            std::move(descriptions),
            std::move(timing_kinds),
            std::move(timing_frequencies),
            std::move(timing_cycles),
        });
    }
}

void append_array_samples(WaveformFrame& frame,
                          const std::string& scope,
                          std::vector<std::string> name_path,
                          std::vector<std::vector<std::string>> name_options_path,
                          std::vector<std::string> description_path,
                          std::vector<std::string> timing_kind_path,
                          std::vector<long double> timing_frequency_hz_path,
                          std::vector<std::uint64_t> timing_cycle_path,
                          const StateArrayRegistry& arrays,
                          PortValueBase base) {
    for (const auto* array : arrays.entries()) {
        for (std::size_t flat = 0; flat < array->element_count(); ++flat) {
            const auto element_options = make_array_element_name_options(*array, flat);
            auto path = name_path;
            path.push_back(array->name());
            path.push_back(element_options.front());
            auto options_path = name_options_path;
            options_path.push_back(make_name_options(array->name()));
            options_path.push_back(element_options);
            auto descriptions = description_path;
            descriptions.push_back(array->description());
            descriptions.push_back(array->description());
            auto timing_kinds = timing_kind_path;
            timing_kinds.push_back("");
            timing_kinds.push_back("");
            auto timing_frequencies = timing_frequency_hz_path;
            timing_frequencies.push_back(0.0L);
            timing_frequencies.push_back(0.0L);
            auto timing_cycles = timing_cycle_path;
            timing_cycles.push_back(0);
            timing_cycles.push_back(0);
            frame.signals.push_back(WaveformSignalSample{
                scope + "." + array->name(),
                std::to_string(flat),
                WaveformSignalKind::StateArrayElement,
                array->type_name(),
                array->width_bits(),
                array->element_value_string(flat, base),
                true,
                std::move(path),
                std::move(options_path),
                std::move(descriptions),
                std::move(timing_kinds),
                std::move(timing_frequencies),
                std::move(timing_cycles),
            });
        }
    }
}

void append_port_samples(WaveformFrame& frame,
                         const std::string& scope,
                         std::vector<std::string> name_path,
                         std::vector<std::vector<std::string>> name_options_path,
                         std::vector<std::string> description_path,
                         std::vector<std::string> timing_kind_path,
                         std::vector<long double> timing_frequency_hz_path,
                         std::vector<std::uint64_t> timing_cycle_path,
                         const PortGroup& group,
                         PortValueBase base) {
    const std::string group_scope = scope + "." + group.name();
    for (const auto& input : group.inputs()) {
        auto path = name_path;
        path.push_back(group.name());
        path.push_back(input->name());
        auto options_path = name_options_path;
        options_path.push_back(make_name_options(group.name()));
        options_path.push_back(make_name_options(input->name()));
        auto descriptions = description_path;
        descriptions.push_back("");
        descriptions.push_back("");
        auto timing_kinds = timing_kind_path;
        timing_kinds.push_back("");
        timing_kinds.push_back("");
        auto timing_frequencies = timing_frequency_hz_path;
        timing_frequencies.push_back(0.0L);
        timing_frequencies.push_back(0.0L);
        auto timing_cycles = timing_cycle_path;
        timing_cycles.push_back(0);
        timing_cycles.push_back(0);
        frame.signals.push_back(WaveformSignalSample{
            group_scope,
            input->name(),
            WaveformSignalKind::PortInput,
            input->type_string(),
            input->width_bits(),
            input->value_string(base),
            input->valid(),
            std::move(path),
            std::move(options_path),
            std::move(descriptions),
            std::move(timing_kinds),
            std::move(timing_frequencies),
            std::move(timing_cycles),
        });
    }
    for (const auto& output : group.outputs()) {
        auto path = name_path;
        path.push_back(group.name());
        path.push_back(output->name());
        auto options_path = name_options_path;
        options_path.push_back(make_name_options(group.name()));
        options_path.push_back(make_name_options(output->name()));
        auto descriptions = description_path;
        descriptions.push_back("");
        descriptions.push_back("");
        auto timing_kinds = timing_kind_path;
        timing_kinds.push_back("");
        timing_kinds.push_back("");
        auto timing_frequencies = timing_frequency_hz_path;
        timing_frequencies.push_back(0.0L);
        timing_frequencies.push_back(0.0L);
        auto timing_cycles = timing_cycle_path;
        timing_cycles.push_back(0);
        timing_cycles.push_back(0);
        frame.signals.push_back(WaveformSignalSample{
            group_scope,
            output->name(),
            WaveformSignalKind::PortOutput,
            output->type_string(),
            output->width_bits(),
            output->value_string(base),
            output->valid(),
            std::move(path),
            std::move(options_path),
            std::move(descriptions),
            std::move(timing_kinds),
            std::move(timing_frequencies),
            std::move(timing_cycles),
        });
    }
}

void append_component_samples(WaveformFrame& frame,
                              const std::string& scope,
                              std::vector<std::string> name_path,
                              std::vector<std::vector<std::string>> name_options_path,
                              std::vector<std::string> description_path,
                              std::vector<std::string> timing_kind_path,
                              std::vector<long double> timing_frequency_hz_path,
                              std::vector<std::uint64_t> timing_cycle_path,
                              const KernelComponent& component,
                              PortValueBase base) {
    const std::string component_scope = scope + "." + component.name();
    name_path.push_back(component.name());
    name_options_path.push_back(make_name_options(component.name(), component.name_aliases()));
    description_path.push_back(component.description());
    timing_kind_path.push_back("kernel_component");
    timing_frequency_hz_path.push_back(component.frequency_hz());
    timing_cycle_path.push_back(component.current_cycle());
    append_state_samples(frame,
                         component_scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         component.state_set(),
                         base);
    append_array_samples(frame,
                         component_scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         component.state_array_registry(),
                         base);
    for (const auto& group : component.port_groups()) {
        append_port_samples(frame,
                            component_scope,
                            name_path,
                            name_options_path,
                            description_path,
                            timing_kind_path,
                            timing_frequency_hz_path,
                            timing_cycle_path,
                            *group,
                            base);
    }
}

void append_kernel_samples(WaveformFrame& frame,
                           const std::string& scope,
                           std::vector<std::string> name_path,
                           std::vector<std::vector<std::string>> name_options_path,
                           std::vector<std::string> description_path,
                           std::vector<std::string> timing_kind_path,
                           std::vector<long double> timing_frequency_hz_path,
                           std::vector<std::uint64_t> timing_cycle_path,
                           const Kernel& kernel,
                           PortValueBase base) {
    const std::string kernel_scope = scope + "." + kernel.name();
    name_path.push_back(kernel.name());
    name_options_path.push_back(make_name_options(kernel.name(), kernel.name_aliases()));
    description_path.push_back(kernel.description());
    timing_kind_path.push_back("kernel");
    timing_frequency_hz_path.push_back(kernel.frequency_hz());
    timing_cycle_path.push_back(kernel.current_cycle());
    append_state_samples(frame,
                         kernel_scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         kernel.state_set(),
                         base);
    append_array_samples(frame,
                         kernel_scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         kernel.state_array_registry(),
                         base);
    for (const auto& group : kernel.port_groups()) {
        append_port_samples(frame,
                            kernel_scope,
                            name_path,
                            name_options_path,
                            description_path,
                            timing_kind_path,
                            timing_frequency_hz_path,
                            timing_cycle_path,
                            *group,
                            base);
    }
    for (const auto& component : kernel.components()) {
        append_component_samples(frame,
                                 kernel_scope,
                                 name_path,
                                 name_options_path,
                                 description_path,
                                 timing_kind_path,
                                 timing_frequency_hz_path,
                                 timing_cycle_path,
                                 *component,
                                 base);
    }
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    escaped += "\\u00";
                    static constexpr char kHex[] = "0123456789abcdef";
                    escaped += kHex[(static_cast<unsigned char>(ch) >> 4) & 0xf];
                    escaped += kHex[static_cast<unsigned char>(ch) & 0xf];
                } else {
                    escaped += ch;
                }
                break;
        }
    }
    return escaped;
}

std::string quoted(const std::string& value) {
    return "\"" + json_escape(value) + "\"";
}

void append_string_array_json(std::ostream& os, const std::vector<std::string>& values) {
    os << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            os << ",";
        }
        os << quoted(values[index]);
    }
    os << "]";
}

void append_string_array_array_json(std::ostream& os,
                                    const std::vector<std::vector<std::string>>& values) {
    os << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            os << ",";
        }
        append_string_array_json(os, values[index]);
    }
    os << "]";
}

void append_long_double_array_json(std::ostream& os, const std::vector<long double>& values) {
    os << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            os << ",";
        }
        os << long_double_string(values[index]);
    }
    os << "]";
}

void append_uint64_array_json(std::ostream& os, const std::vector<std::uint64_t>& values) {
    os << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            os << ",";
        }
        os << values[index];
    }
    os << "]";
}

std::string shell_quote(const std::string& value) {
    std::string quoted_value = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted_value += "'\"'\"'";
        } else {
            quoted_value += ch;
        }
    }
    quoted_value += "'";
    return quoted_value;
}

std::string long_double_string(long double value) {
    std::ostringstream oss;
    oss << std::setprecision(21) << static_cast<long double>(value);
    return oss.str();
}

std::string sanitize_path_component(const std::string& value) {
    std::string clean;
    clean.reserve(value.size());
    for (const char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.') {
            clean += ch;
        } else {
            clean += '_';
        }
    }
    return clean.empty() ? "unnamed" : clean;
}

std::string padded_index(std::uint64_t index) {
    std::ostringstream oss;
    oss << std::setw(6) << std::setfill('0') << index;
    return oss.str();
}

std::string compact_scaled_cycle(std::uint64_t cycle) {
    const char* suffix = "";
    long double value = static_cast<long double>(cycle);
    if (cycle >= 1000000000ULL) {
        value /= 1000000000.0L;
        suffix = "g";
    } else if (cycle >= 1000000ULL) {
        value /= 1000000.0L;
        suffix = "m";
    } else if (cycle >= 1000ULL) {
        value /= 1000.0L;
        suffix = "k";
    }

    std::ostringstream oss;
    if (*suffix == '\0') {
        oss << cycle;
    } else {
        oss << std::fixed << std::setprecision(3) << static_cast<double>(value);
        std::string text = oss.str();
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
        return text + suffix;
    }
    return oss.str();
}

std::string current_datetime_label() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_snapshot{};
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &now);
#else
    localtime_r(&now, &tm_snapshot);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_snapshot, "%Y%m%d.%H%M%S");
    return oss.str();
}

void raise_io(const std::string& scope, const std::string& detail) {
    error::raise(error::Stage::IO, error::Kind::IOError, scope, detail);
}

void raise_data(const std::string& scope, const std::string& detail) {
    error::raise(error::Stage::Data, error::Kind::DataFormat, scope, detail);
}

void create_directories_or_throw(const std::filesystem::path& path) {
    try {
        std::filesystem::create_directories(path);
    } catch (const std::filesystem::filesystem_error& ex) {
        raise_io("SnapshotCapture", "cannot create directory " + path.string() +
                                        ": " + ex.what());
    }
}

void ensure_parent_directory(const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        create_directories_or_throw(parent);
    }
}

std::ofstream open_output_file(const std::string& path,
                               std::ios::openmode mode = std::ios::out) {
    ensure_parent_directory(path);
    std::ofstream out(path, mode);
    if (!out) {
        raise_io("SnapshotCapture", "cannot open output file " + path);
    }
    return out;
}

void append_signals_json(std::ostream& os, const std::vector<WaveformSignalSample>& signals) {
    os << "[";
    for (std::size_t index = 0; index < signals.size(); ++index) {
        const auto& signal = signals[index];
        if (index != 0) {
            os << ",";
        }
        os << "{";
        os << "\"scope\":" << quoted(signal.scope);
        os << ",\"name\":" << quoted(signal.name);
        os << ",\"kind\":" << quoted(waveform_signal_kind_name(signal.kind));
        os << ",\"type\":" << quoted(signal.type_name);
        os << ",\"width_bits\":" << signal.width_bits;
        os << ",\"value\":" << quoted(signal.value);
        os << ",\"valid\":" << (signal.valid ? "true" : "false");
        os << ",\"name_path\":";
        append_string_array_json(os, signal.name_path);
        os << ",\"name_options_path\":";
        append_string_array_array_json(os, signal.name_options_path);
        os << ",\"description_path\":";
        append_string_array_json(os, signal.description_path);
        os << ",\"timing_kind_path\":";
        append_string_array_json(os, signal.timing_kind_path);
        os << ",\"timing_frequency_hz_path\":";
        append_long_double_array_json(os, signal.timing_frequency_hz_path);
        os << ",\"timing_cycle_path\":";
        append_uint64_array_json(os, signal.timing_cycle_path);
        os << "}";
    }
    os << "]";
}

WaveformFrame capture_component_waveform_frame(const KernelComponent& component,
                                               std::uint64_t frame_index,
                                               PortValueBase base) {
    WaveformFrame frame;
    frame.cycle = frame_index;
    const std::string scope = component.name();
    const std::vector<std::string> name_path{component.name()};
    const std::vector<std::vector<std::string>> name_options_path{
        make_name_options(component.name(), component.name_aliases()),
    };
    const std::vector<std::string> description_path{component.description()};
    const std::vector<std::string> timing_kind_path{"kernel_component"};
    const std::vector<long double> timing_frequency_hz_path{component.frequency_hz()};
    const std::vector<std::uint64_t> timing_cycle_path{component.current_cycle()};
    append_state_samples(frame,
                         scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         component.state_set(),
                         base);
    append_array_samples(frame,
                         scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         component.state_array_registry(),
                         base);
    for (const auto& group : component.port_groups()) {
        append_port_samples(frame,
                            scope,
                            name_path,
                            name_options_path,
                            description_path,
                            timing_kind_path,
                            timing_frequency_hz_path,
                            timing_cycle_path,
                            *group,
                            base);
    }
    return frame;
}

WaveformFrame capture_kernel_waveform_frame(const Kernel& kernel,
                                            std::uint64_t frame_index,
                                            PortValueBase base) {
    WaveformFrame frame;
    frame.cycle = frame_index;
    const std::string scope = kernel.name();
    const std::vector<std::string> name_path{kernel.name()};
    const std::vector<std::vector<std::string>> name_options_path{
        make_name_options(kernel.name(), kernel.name_aliases()),
    };
    const std::vector<std::string> description_path{kernel.description()};
    const std::vector<std::string> timing_kind_path{"kernel"};
    const std::vector<long double> timing_frequency_hz_path{kernel.frequency_hz()};
    const std::vector<std::uint64_t> timing_cycle_path{kernel.current_cycle()};
    append_state_samples(frame,
                         scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         kernel.state_set(),
                         base);
    append_array_samples(frame,
                         scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         kernel.state_array_registry(),
                         base);
    for (const auto& group : kernel.port_groups()) {
        append_port_samples(frame,
                            scope,
                            name_path,
                            name_options_path,
                            description_path,
                            timing_kind_path,
                            timing_frequency_hz_path,
                            timing_cycle_path,
                            *group,
                            base);
    }
    for (const auto& component : kernel.components()) {
        append_component_samples(frame,
                                 scope,
                                 name_path,
                                 name_options_path,
                                 description_path,
                                 timing_kind_path,
                                 timing_frequency_hz_path,
                                 timing_cycle_path,
                                 *component,
                                 base);
    }
    return frame;
}

void append_waveform_frame_common(std::ostream& os,
                                  const std::string& object_kind,
                                  const std::string& object_name,
                                  long double frequency_hz,
                                  std::uint64_t current_cycle,
                                  std::uint64_t sequence,
                                  SnapshotCaptureStage stage,
                                  const WaveformFrame& frame) {
    os << "{";
    os << "\"format\":\"project_xs.waveform_frame\"";
    os << ",\"version\":1";
    os << ",\"object_kind\":" << quoted(object_kind);
    os << ",\"object_name\":" << quoted(object_name);
    os << ",\"root_timing\":{";
    os << "\"kind\":" << quoted(object_kind);
    os << ",\"name\":" << quoted(object_name);
    os << ",\"frequency_hz\":" << long_double_string(frequency_hz);
    os << ",\"cycle\":" << current_cycle;
    os << "}";
    os << ",\"sequence\":" << sequence;
    os << ",\"stage\":" << quoted(snapshot_capture_stage_name(stage));
    os << ",\"cycle\":" << frame.cycle;
    os << ",\"signals\":";
    append_signals_json(os, frame.signals);
    os << "}\n";
}

class BinaryWriter {
  public:
    explicit BinaryWriter(const std::string& path)
        : out_(open_output_file(path, std::ios::out | std::ios::binary)) {}

    template <typename T>
    void write_pod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "checkpoint POD value required");
        out_.write(reinterpret_cast<const char*>(&value), sizeof(T));
        if (!out_) {
            raise_io("CheckpointWriter", "write failed");
        }
    }

    void write_bool(bool value) {
        const std::uint8_t stored = value ? 1 : 0;
        write_pod(stored);
    }

    void write_string(const std::string& value) {
        write_size(value.size());
        if (!value.empty()) {
            out_.write(value.data(), static_cast<std::streamsize>(value.size()));
            if (!out_) {
                raise_io("CheckpointWriter", "write string failed");
            }
        }
    }

    void write_size(std::size_t value) {
        write_pod(static_cast<std::uint64_t>(value));
    }

    void write_bytes(const std::vector<std::byte>& value) {
        write_size(value.size());
        if (!value.empty()) {
            out_.write(reinterpret_cast<const char*>(value.data()),
                       static_cast<std::streamsize>(value.size()));
            if (!out_) {
                raise_io("CheckpointWriter", "write bytes failed");
            }
        }
    }

  private:
    std::ofstream out_;
};

class BinaryReader {
  public:
    explicit BinaryReader(const std::string& path)
        : in_(path, std::ios::in | std::ios::binary) {
        if (!in_) {
            raise_io("CheckpointReader", "cannot open checkpoint file " + path);
        }
    }

    template <typename T>
    T read_pod() {
        static_assert(std::is_trivially_copyable_v<T>, "checkpoint POD value required");
        T value{};
        in_.read(reinterpret_cast<char*>(&value), sizeof(T));
        if (!in_) {
            raise_io("CheckpointReader", "read failed");
        }
        return value;
    }

    bool read_bool() {
        const auto stored = read_pod<std::uint8_t>();
        if (stored > 1) {
            raise_data("CheckpointReader", "invalid bool value");
        }
        return stored != 0;
    }

    std::size_t read_size() {
        const auto value = read_pod<std::uint64_t>();
        return static_cast<std::size_t>(value);
    }

    std::string read_string() {
        const auto size = read_size();
        std::string value(size, '\0');
        if (size != 0) {
            in_.read(value.data(), static_cast<std::streamsize>(size));
            if (!in_) {
                raise_io("CheckpointReader", "read string failed");
            }
        }
        return value;
    }

    std::vector<std::byte> read_bytes() {
        const auto size = read_size();
        std::vector<std::byte> value(size);
        if (size != 0) {
            in_.read(reinterpret_cast<char*>(value.data()),
                     static_cast<std::streamsize>(size));
            if (!in_) {
                raise_io("CheckpointReader", "read bytes failed");
            }
        }
        return value;
    }

  private:
    std::ifstream in_;
};

void write_checkpoint_header(BinaryWriter& writer, const std::string& kind) {
    writer.write_string("ProjectXSCheckpoint");
    writer.write_pod<std::uint32_t>(2);
    writer.write_string(kind);
}

std::uint32_t read_checkpoint_header(BinaryReader& reader, const std::string& expected_kind) {
    const std::string magic = reader.read_string();
    const auto version = reader.read_pod<std::uint32_t>();
    const std::string kind = reader.read_string();
    if (magic != "ProjectXSCheckpoint" || version < 1 || version > 2 ||
        kind != expected_kind) {
        raise_data("CheckpointReader", "checkpoint header mismatch");
    }
    return version;
}

void expect_string(const std::string& actual,
                   const std::string& expected,
                   const std::string& scope) {
    if (actual != expected) {
        raise_data(scope, "checkpoint layout mismatch");
    }
}

void expect_size(std::size_t actual, std::size_t expected, const std::string& scope) {
    if (actual != expected) {
        raise_data(scope, "checkpoint layout mismatch");
    }
}

void write_state_snapshot(BinaryWriter& writer, const StateSnapshot& snapshot) {
    writer.write_string(snapshot.name);
    writer.write_string(snapshot.type_name);
    writer.write_size(snapshot.data_size);
    writer.write_size(snapshot.width_bits);
    writer.write_bytes(snapshot.value_storage);
}

StateSnapshot read_state_snapshot(BinaryReader& reader, const StateSnapshot& layout) {
    StateSnapshot snapshot = layout;
    expect_string(reader.read_string(), layout.name, "StateSnapshot");
    expect_string(reader.read_string(), layout.type_name, "StateSnapshot");
    expect_size(reader.read_size(), layout.data_size, "StateSnapshot");
    expect_size(reader.read_size(), layout.width_bits, "StateSnapshot");
    snapshot.value_storage = reader.read_bytes();
    expect_size(snapshot.value_storage.size(), layout.value_storage.size(), "StateSnapshot");
    return snapshot;
}

void write_state_set_snapshot(BinaryWriter& writer, const StateSetSnapshot& snapshot) {
    writer.write_size(snapshot.states.size());
    for (const auto& state : snapshot.states) {
        write_state_snapshot(writer, state);
    }
}

StateSetSnapshot read_state_set_snapshot(BinaryReader& reader,
                                         const StateSetSnapshot& layout) {
    StateSetSnapshot snapshot;
    const auto count = reader.read_size();
    expect_size(count, layout.states.size(), "StateSetSnapshot");
    snapshot.states.reserve(layout.states.size());
    for (std::size_t index = 0; index < layout.states.size(); ++index) {
        snapshot.states.push_back(read_state_snapshot(reader, layout.states[index]));
    }
    return snapshot;
}

void write_state_array_snapshot(BinaryWriter& writer, const StateArraySnapshot& snapshot) {
    writer.write_string(snapshot.name);
    writer.write_string(snapshot.type_name);
    writer.write_size(snapshot.data_size);
    writer.write_size(snapshot.width_bits);
    writer.write_size(snapshot.shape.size());
    for (const auto extent : snapshot.shape) {
        writer.write_size(extent);
    }
    writer.write_bytes(snapshot.value_storage);
}

StateArraySnapshot read_state_array_snapshot(BinaryReader& reader,
                                             const StateArraySnapshot& layout) {
    StateArraySnapshot snapshot = layout;
    expect_string(reader.read_string(), layout.name, "StateArraySnapshot");
    expect_string(reader.read_string(), layout.type_name, "StateArraySnapshot");
    expect_size(reader.read_size(), layout.data_size, "StateArraySnapshot");
    expect_size(reader.read_size(), layout.width_bits, "StateArraySnapshot");
    const auto shape_size = reader.read_size();
    expect_size(shape_size, layout.shape.size(), "StateArraySnapshot");
    for (std::size_t index = 0; index < layout.shape.size(); ++index) {
        expect_size(reader.read_size(), layout.shape[index], "StateArraySnapshot");
    }
    snapshot.value_storage = reader.read_bytes();
    expect_size(snapshot.value_storage.size(),
                layout.value_storage.size(),
                "StateArraySnapshot");
    return snapshot;
}

void write_state_array_registry_snapshot(BinaryWriter& writer,
                                         const StateArrayRegistrySnapshot& snapshot) {
    writer.write_size(snapshot.arrays.size());
    for (const auto& array : snapshot.arrays) {
        write_state_array_snapshot(writer, array);
    }
}

StateArrayRegistrySnapshot read_state_array_registry_snapshot(
    BinaryReader& reader,
    const StateArrayRegistrySnapshot& layout) {
    StateArrayRegistrySnapshot snapshot;
    const auto count = reader.read_size();
    expect_size(count, layout.arrays.size(), "StateArrayRegistrySnapshot");
    snapshot.arrays.reserve(layout.arrays.size());
    for (std::size_t index = 0; index < layout.arrays.size(); ++index) {
        snapshot.arrays.push_back(read_state_array_snapshot(reader, layout.arrays[index]));
    }
    return snapshot;
}

void write_port_snapshot(BinaryWriter& writer, const PortSnapshot& snapshot) {
    writer.write_string(snapshot.name);
    writer.write_pod(static_cast<std::uint8_t>(snapshot.direction));
    writer.write_size(snapshot.width_bits);
    writer.write_size(snapshot.data_size);
    writer.write_bytes(snapshot.visible_storage);
    writer.write_bytes(snapshot.pending_storage);
    writer.write_bytes(snapshot.bound_storage);
    writer.write_bool(snapshot.valid);
    writer.write_bool(snapshot.pending_valid);
}

PortSnapshot read_port_snapshot(BinaryReader& reader, const PortSnapshot& layout) {
    PortSnapshot snapshot = layout;
    expect_string(reader.read_string(), layout.name, "PortSnapshot");
    const auto direction = static_cast<PortDirection>(reader.read_pod<std::uint8_t>());
    if (direction != layout.direction) {
        raise_data("PortSnapshot", "checkpoint direction mismatch");
    }
    expect_size(reader.read_size(), layout.width_bits, "PortSnapshot");
    expect_size(reader.read_size(), layout.data_size, "PortSnapshot");
    snapshot.visible_storage = reader.read_bytes();
    snapshot.pending_storage = reader.read_bytes();
    snapshot.bound_storage = reader.read_bytes();
    expect_size(snapshot.visible_storage.size(), layout.visible_storage.size(), "PortSnapshot");
    expect_size(snapshot.pending_storage.size(), layout.pending_storage.size(), "PortSnapshot");
    expect_size(snapshot.bound_storage.size(), layout.bound_storage.size(), "PortSnapshot");
    snapshot.valid = reader.read_bool();
    snapshot.pending_valid = reader.read_bool();
    return snapshot;
}

void write_port_group_snapshot(BinaryWriter& writer, const PortGroupSnapshot& snapshot) {
    writer.write_string(snapshot.name);
    writer.write_size(snapshot.inputs.size());
    for (const auto& input : snapshot.inputs) {
        write_port_snapshot(writer, input);
    }
    writer.write_size(snapshot.outputs.size());
    for (const auto& output : snapshot.outputs) {
        write_port_snapshot(writer, output);
    }
}

PortGroupSnapshot read_port_group_snapshot(BinaryReader& reader,
                                           const PortGroupSnapshot& layout) {
    PortGroupSnapshot snapshot;
    snapshot.name = layout.name;
    expect_string(reader.read_string(), layout.name, "PortGroupSnapshot");
    const auto input_count = reader.read_size();
    expect_size(input_count, layout.inputs.size(), "PortGroupSnapshot");
    snapshot.inputs.reserve(layout.inputs.size());
    for (std::size_t index = 0; index < layout.inputs.size(); ++index) {
        snapshot.inputs.push_back(read_port_snapshot(reader, layout.inputs[index]));
    }
    const auto output_count = reader.read_size();
    expect_size(output_count, layout.outputs.size(), "PortGroupSnapshot");
    snapshot.outputs.reserve(layout.outputs.size());
    for (std::size_t index = 0; index < layout.outputs.size(); ++index) {
        snapshot.outputs.push_back(read_port_snapshot(reader, layout.outputs[index]));
    }
    return snapshot;
}

void write_component_snapshot_body(BinaryWriter& writer,
                                   const KernelComponentSnapshot& snapshot) {
    writer.write_string(snapshot.name);
    writer.write_pod(snapshot.current_cycle);
    writer.write_pod(snapshot.frequency_hz);
    writer.write_pod(snapshot.align_remaining);
    writer.write_pod(snapshot.phase);
    write_state_set_snapshot(writer, snapshot.states);
    write_state_array_registry_snapshot(writer, snapshot.state_arrays);
    writer.write_size(snapshot.port_groups.size());
    for (const auto& group : snapshot.port_groups) {
        write_port_group_snapshot(writer, group);
    }
}

KernelComponentSnapshot read_component_snapshot_body(
    BinaryReader& reader,
    const KernelComponentSnapshot& layout,
    std::uint32_t version) {
    KernelComponentSnapshot snapshot = layout;
    expect_string(reader.read_string(), layout.name, "KernelComponentSnapshot");
    if (version >= 2) {
        snapshot.current_cycle = reader.read_pod<std::uint64_t>();
        snapshot.frequency_hz = reader.read_pod<long double>();
    }
    snapshot.align_remaining = reader.read_pod<std::uint64_t>();
    snapshot.phase = reader.read_pod<std::uint64_t>();
    snapshot.states = read_state_set_snapshot(reader, layout.states);
    snapshot.state_arrays =
        read_state_array_registry_snapshot(reader, layout.state_arrays);
    const auto port_group_count = reader.read_size();
    expect_size(port_group_count, layout.port_groups.size(), "KernelComponentSnapshot");
    snapshot.port_groups.clear();
    snapshot.port_groups.reserve(layout.port_groups.size());
    for (std::size_t index = 0; index < layout.port_groups.size(); ++index) {
        snapshot.port_groups.push_back(
            read_port_group_snapshot(reader, layout.port_groups[index]));
    }
    return snapshot;
}

void write_kernel_snapshot_body(BinaryWriter& writer, const KernelSnapshot& snapshot) {
    writer.write_string(snapshot.name);
    writer.write_pod(snapshot.current_cycle);
    writer.write_pod(snapshot.frequency_hz);
    writer.write_pod(snapshot.elapsed_cycles);
    writer.write_bool(snapshot.terminate_requested);
    write_state_set_snapshot(writer, snapshot.states);
    write_state_array_registry_snapshot(writer, snapshot.state_arrays);
    writer.write_size(snapshot.port_groups.size());
    for (const auto& group : snapshot.port_groups) {
        write_port_group_snapshot(writer, group);
    }
    writer.write_size(snapshot.components.size());
    for (const auto& component : snapshot.components) {
        write_component_snapshot_body(writer, component);
    }
}

KernelSnapshot read_kernel_snapshot_body(BinaryReader& reader,
                                         const KernelSnapshot& layout,
                                         std::uint32_t version) {
    KernelSnapshot snapshot = layout;
    expect_string(reader.read_string(), layout.name, "KernelSnapshot");
    if (version >= 2) {
        snapshot.current_cycle = reader.read_pod<std::uint64_t>();
        snapshot.frequency_hz = reader.read_pod<long double>();
    }
    snapshot.elapsed_cycles = reader.read_pod<std::uint64_t>();
    snapshot.terminate_requested = reader.read_bool();
    snapshot.states = read_state_set_snapshot(reader, layout.states);
    snapshot.state_arrays =
        read_state_array_registry_snapshot(reader, layout.state_arrays);
    const auto port_group_count = reader.read_size();
    expect_size(port_group_count, layout.port_groups.size(), "KernelSnapshot");
    snapshot.port_groups.clear();
    snapshot.port_groups.reserve(layout.port_groups.size());
    for (std::size_t index = 0; index < layout.port_groups.size(); ++index) {
        snapshot.port_groups.push_back(
            read_port_group_snapshot(reader, layout.port_groups[index]));
    }
    const auto component_count = reader.read_size();
    expect_size(component_count, layout.components.size(), "KernelSnapshot");
    snapshot.components.clear();
    snapshot.components.reserve(layout.components.size());
    for (std::size_t index = 0; index < layout.components.size(); ++index) {
        snapshot.components.push_back(
            read_component_snapshot_body(reader, layout.components[index], version));
    }
    return snapshot;
}

void write_cycle_simulator_snapshot_body(BinaryWriter& writer,
                                         const CycleSimulatorSnapshot& snapshot) {
    writer.write_string(snapshot.name);
    writer.write_pod(snapshot.current_cycle);
    writer.write_pod(snapshot.max_cycles);
    writer.write_pod(snapshot.frequency_hz);
    writer.write_bool(snapshot.finished);
    write_state_set_snapshot(writer, snapshot.states);
    write_state_array_registry_snapshot(writer, snapshot.state_arrays);
    writer.write_size(snapshot.port_groups.size());
    for (const auto& group : snapshot.port_groups) {
        write_port_group_snapshot(writer, group);
    }
    writer.write_size(snapshot.kernels.size());
    for (const auto& kernel : snapshot.kernels) {
        write_kernel_snapshot_body(writer, kernel);
    }
}

CycleSimulatorSnapshot read_cycle_simulator_snapshot_body(
    BinaryReader& reader,
    const CycleSimulatorSnapshot& layout,
    std::uint32_t version) {
    CycleSimulatorSnapshot snapshot = layout;
    expect_string(reader.read_string(), layout.name, "CycleSimulatorSnapshot");
    snapshot.current_cycle = reader.read_pod<std::uint64_t>();
    snapshot.max_cycles = reader.read_pod<std::uint64_t>();
    snapshot.frequency_hz = reader.read_pod<long double>();
    snapshot.finished = reader.read_bool();
    snapshot.states = read_state_set_snapshot(reader, layout.states);
    snapshot.state_arrays =
        read_state_array_registry_snapshot(reader, layout.state_arrays);
    const auto port_group_count = reader.read_size();
    expect_size(port_group_count, layout.port_groups.size(), "CycleSimulatorSnapshot");
    snapshot.port_groups.clear();
    snapshot.port_groups.reserve(layout.port_groups.size());
    for (std::size_t index = 0; index < layout.port_groups.size(); ++index) {
        snapshot.port_groups.push_back(
            read_port_group_snapshot(reader, layout.port_groups[index]));
    }
    const auto kernel_count = reader.read_size();
    expect_size(kernel_count, layout.kernels.size(), "CycleSimulatorSnapshot");
    snapshot.kernels.clear();
    snapshot.kernels.reserve(layout.kernels.size());
    for (std::size_t index = 0; index < layout.kernels.size(); ++index) {
        auto kernel = read_kernel_snapshot_body(reader, layout.kernels[index], version);
        if (version < 2) {
            kernel.current_cycle = snapshot.current_cycle;
            kernel.frequency_hz = snapshot.frequency_hz;
            for (auto& component : kernel.components) {
                component.current_cycle = snapshot.current_cycle;
                component.frequency_hz = snapshot.frequency_hz;
            }
        }
        snapshot.kernels.push_back(std::move(kernel));
    }
    return snapshot;
}

void write_session_snapshot_body(BinaryWriter& writer,
                                 const SimulationSessionSnapshot& snapshot) {
    writer.write_pod(snapshot.frequency_hz);
    writer.write_pod(snapshot.run_time_seconds);
    writer.write_pod(snapshot.current_tick);
    writer.write_bool(snapshot.finished);
    writer.write_size(snapshot.simulators.size());
    for (const auto& simulator : snapshot.simulators) {
        writer.write_string(simulator.name);
        writer.write_pod(simulator.accumulated_hz);
        write_cycle_simulator_snapshot_body(writer, simulator.simulator);
    }
}

SimulationSessionSnapshot read_session_snapshot_body(
    BinaryReader& reader,
    const SimulationSessionSnapshot& layout,
    std::uint32_t version) {
    SimulationSessionSnapshot snapshot = layout;
    snapshot.frequency_hz = reader.read_pod<long double>();
    snapshot.run_time_seconds = reader.read_pod<long double>();
    snapshot.current_tick = reader.read_pod<std::uint64_t>();
    snapshot.finished = reader.read_bool();
    const auto simulator_count = reader.read_size();
    expect_size(simulator_count, layout.simulators.size(), "SimulationSessionSnapshot");
    snapshot.simulators.clear();
    snapshot.simulators.reserve(layout.simulators.size());
    for (std::size_t index = 0; index < layout.simulators.size(); ++index) {
        ScheduledSimulatorSnapshot scheduled;
        scheduled.name = reader.read_string();
        expect_string(scheduled.name, layout.simulators[index].name, "SimulationSessionSnapshot");
        scheduled.accumulated_hz = reader.read_pod<long double>();
        scheduled.simulator =
            read_cycle_simulator_snapshot_body(reader, layout.simulators[index].simulator, version);
        snapshot.simulators.push_back(std::move(scheduled));
    }
    return snapshot;
}

}  // namespace

StateSet::Snapshot StateSet::snapshot() const {
    Snapshot shot;
    shot.states.reserve(entries_.size());
    for (const auto& entry : entries_) {
        const StateBase& state = entry->state();
        StateSnapshot state_shot;
        state_shot.name = state.name();
        state_shot.type_name = state.type_name();
        state_shot.type_index = state.type_index();
        state_shot.data_size = state.data_size();
        state_shot.width_bits = state.width_bits();
        state_shot.value_storage.resize(state.data_size());
        std::memcpy(state_shot.value_storage.data(), state.value_ptr(), state.data_size());
        shot.states.push_back(std::move(state_shot));
    }
    return shot;
}

void StateSet::restore(const Snapshot& snapshot) {
    if (entries_.size() != snapshot.states.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "StateSet",
                     "snapshot entry count mismatch");
    }

    for (std::size_t index = 0; index < entries_.size(); ++index) {
        StateBase& state = entries_[index]->state();
        const StateSnapshot& state_shot = snapshot.states[index];
        if (state.name() != state_shot.name ||
            state.type_index() != state_shot.type_index ||
            state.data_size() != state_shot.data_size ||
            state.width_bits() != state_shot.width_bits ||
            state_shot.value_storage.size() != state.data_size()) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::LayoutMismatch,
                         "StateSet",
                         "snapshot layout mismatch on " + state.name());
        }
        std::memcpy(state.value_ptr(), state_shot.value_storage.data(), state.data_size());
    }
}

StateArrayRegistry::Snapshot StateArrayRegistry::snapshot() const {
    Snapshot shot;
    shot.arrays.reserve(entries_.size());
    for (const auto* entry : entries_) {
        shot.arrays.push_back(entry->snapshot());
    }
    return shot;
}

void StateArrayRegistry::restore(const Snapshot& snapshot) {
    if (entries_.size() != snapshot.arrays.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "StateArrayRegistry",
                     "snapshot entry count mismatch");
    }

    for (std::size_t index = 0; index < entries_.size(); ++index) {
        entries_[index]->restore(snapshot.arrays[index]);
    }
}

PortSnapshot Port::snapshot() const {
    PortSnapshot shot;
    shot.name = name_;
    shot.direction = direction_;
    shot.width_bits = width_bits_;
    shot.data_size = data_size_;
    shot.type_index = type_index_;
    shot.visible_storage = visible_storage_;
    shot.pending_storage = pending_storage_;
    shot.bound_storage.resize(data_size_);
    std::memcpy(shot.bound_storage.data(), bound_variable_, data_size_);
    shot.valid = valid_;
    shot.pending_valid = pending_valid_;
    return shot;
}

void Port::restore(const PortSnapshot& snapshot) {
    if (name_ != snapshot.name ||
        direction_ != snapshot.direction ||
        width_bits_ != snapshot.width_bits ||
        data_size_ != snapshot.data_size ||
        type_index_ != snapshot.type_index ||
        snapshot.visible_storage.size() != data_size_ ||
        snapshot.pending_storage.size() != data_size_ ||
        snapshot.bound_storage.size() != data_size_) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "Port",
                     "snapshot layout mismatch on " + name_);
    }

    visible_storage_ = snapshot.visible_storage;
    pending_storage_ = snapshot.pending_storage;
    valid_ = snapshot.valid;
    pending_valid_ = snapshot.pending_valid;
    std::memcpy(bound_variable_, snapshot.bound_storage.data(), data_size_);
}

PortGroup::Snapshot PortGroup::snapshot() const {
    Snapshot shot;
    shot.name = name_;
    shot.inputs.reserve(inputs_.size());
    shot.outputs.reserve(outputs_.size());
    for (const auto& input : inputs_) {
        shot.inputs.push_back(input->snapshot());
    }
    for (const auto& output : outputs_) {
        shot.outputs.push_back(output->snapshot());
    }
    return shot;
}

void PortGroup::restore(const Snapshot& snapshot) {
    if (name_ != snapshot.name ||
        inputs_.size() != snapshot.inputs.size() ||
        outputs_.size() != snapshot.outputs.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "PortGroup",
                     "snapshot layout mismatch: " + name_);
    }

    for (std::size_t index = 0; index < inputs_.size(); ++index) {
        inputs_[index]->restore(snapshot.inputs[index]);
    }
    for (std::size_t index = 0; index < outputs_.size(); ++index) {
        outputs_[index]->restore(snapshot.outputs[index]);
    }
}

KernelComponent::Snapshot KernelComponent::snapshot() const {
    Snapshot shot;
    shot.name = name_;
    shot.current_cycle = current_cycle_;
    shot.frequency_hz = frequency_hz_;
    shot.align_remaining = align_remaining_;
    shot.phase = phase_;
    shot.states = state_set_.snapshot();
    shot.state_arrays = state_array_registry_.snapshot();
    shot.port_groups.reserve(port_groups_.size());
    for (const auto& group : port_groups_) {
        shot.port_groups.push_back(group->snapshot());
    }
    return shot;
}

void KernelComponent::restore(const Snapshot& snapshot) {
    if (name_ != snapshot.name || port_groups_.size() != snapshot.port_groups.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "KernelComponent",
                     "snapshot layout mismatch on " + name_);
    }

    align_remaining_ = snapshot.align_remaining;
    phase_ = snapshot.phase;
    set_current_cycle_from_parent(snapshot.current_cycle);
    set_frequency_hz_from_parent(snapshot.frequency_hz);
    state_set_.restore(snapshot.states);
    state_array_registry_.restore(snapshot.state_arrays);
    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        port_groups_[index]->restore(snapshot.port_groups[index]);
    }
}

Kernel::Snapshot Kernel::snapshot() const {
    Snapshot shot;
    shot.name = name_;
    shot.current_cycle = current_cycle_;
    shot.frequency_hz = frequency_hz_;
    shot.elapsed_cycles = elapsed_cycles_;
    shot.terminate_requested = terminate_requested_;
    shot.states = state_set_.snapshot();
    shot.state_arrays = state_array_registry_.snapshot();
    shot.port_groups.reserve(port_groups_.size());
    for (const auto& group : port_groups_) {
        shot.port_groups.push_back(group->snapshot());
    }
    shot.components.reserve(components_.size());
    for (const auto& component : components_) {
        shot.components.push_back(component->snapshot());
    }
    return shot;
}

void Kernel::restore(const Snapshot& snapshot) {
    if (name_ != snapshot.name ||
        port_groups_.size() != snapshot.port_groups.size() ||
        components_.size() != snapshot.components.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "Kernel",
                     "snapshot layout mismatch on " + name_);
    }

    elapsed_cycles_ = snapshot.elapsed_cycles;
    terminate_requested_ = snapshot.terminate_requested;
    set_current_cycle_from_parent(snapshot.current_cycle);
    set_frequency_hz_from_parent(snapshot.frequency_hz);
    state_set_.restore(snapshot.states);
    state_array_registry_.restore(snapshot.state_arrays);
    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        port_groups_[index]->restore(snapshot.port_groups[index]);
    }
    for (std::size_t index = 0; index < components_.size(); ++index) {
        components_[index]->restore(snapshot.components[index]);
    }
}

CycleSimulator::Snapshot CycleSimulator::snapshot() const {
    Snapshot shot;
    shot.name = name_;
    shot.current_cycle = current_cycle_;
    shot.max_cycles = max_cycles_;
    shot.frequency_hz = frequency_hz_;
    shot.finished = finished_;
    shot.states = state_set_.snapshot();
    shot.state_arrays = state_array_registry_.snapshot();
    shot.port_groups.reserve(port_groups_.size());
    for (const auto& group : port_groups_) {
        shot.port_groups.push_back(group->snapshot());
    }
    shot.kernels.reserve(kernels_.size());
    for (const auto& kernel : kernels_) {
        shot.kernels.push_back(kernel->snapshot());
    }
    return shot;
}

void CycleSimulator::restore(const Snapshot& snapshot) {
    if (name_ != snapshot.name ||
        port_groups_.size() != snapshot.port_groups.size() ||
        kernels_.size() != snapshot.kernels.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "CycleSimulator",
                     "snapshot layout mismatch on " + name_);
    }

    current_cycle_ = snapshot.current_cycle;
    max_cycles_ = snapshot.max_cycles;
    set_frequency_hz(snapshot.frequency_hz);
    finished_ = snapshot.finished;
    state_set_.restore(snapshot.states);
    state_array_registry_.restore(snapshot.state_arrays);
    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        port_groups_[index]->restore(snapshot.port_groups[index]);
    }
    for (std::size_t index = 0; index < kernels_.size(); ++index) {
        kernels_[index]->restore(snapshot.kernels[index]);
    }
}

WaveformFrame capture_waveform_frame(const CycleSimulator& simulator,
                                     std::uint64_t cycle,
                                     PortValueBase base) {
    WaveformFrame frame;
    frame.cycle = cycle;

    const std::string simulator_scope = simulator.name();
    const std::vector<std::string> name_path{simulator.name()};
    const std::vector<std::vector<std::string>> name_options_path{
        make_name_options(simulator.name(), simulator.name_aliases()),
    };
    const std::vector<std::string> description_path{simulator.description()};
    const std::vector<std::string> timing_kind_path{"cycle_simulator"};
    const std::vector<long double> timing_frequency_hz_path{simulator.frequency_hz()};
    const std::vector<std::uint64_t> timing_cycle_path{simulator.current_cycle()};
    append_state_samples(frame,
                         simulator_scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         simulator.state_set(),
                         base);
    append_array_samples(frame,
                         simulator_scope,
                         name_path,
                         name_options_path,
                         description_path,
                         timing_kind_path,
                         timing_frequency_hz_path,
                         timing_cycle_path,
                         simulator.state_array_registry(),
                         base);
    for (const auto& group : simulator.port_groups()) {
        append_port_samples(frame,
                            simulator_scope,
                            name_path,
                            name_options_path,
                            description_path,
                            timing_kind_path,
                            timing_frequency_hz_path,
                            timing_cycle_path,
                            *group,
                            base);
    }
    for (const auto& kernel : simulator.kernels()) {
        append_kernel_samples(frame,
                              simulator_scope,
                              name_path,
                              name_options_path,
                              description_path,
                              timing_kind_path,
                              timing_frequency_hz_path,
                              timing_cycle_path,
                              *kernel,
                              base);
    }

    return frame;
}

WaveformFrame capture_waveform_frame(const CycleSimulator& simulator, PortValueBase base) {
    return capture_waveform_frame(simulator, simulator.current_cycle(), base);
}

void append_waveform_frame(WaveformTrace& trace,
                           const CycleSimulator& simulator,
                           PortValueBase base) {
    if (trace.name.empty()) {
        trace.name = simulator.name();
    }
    trace.frames.push_back(capture_waveform_frame(simulator, base));
}

const char* snapshot_capture_mode_name(SnapshotCaptureMode mode) {
    switch (mode) {
        case SnapshotCaptureMode::Manual:
            return "Manual";
        case SnapshotCaptureMode::Automatic:
            return "Automatic";
    }
    return "Unknown";
}

const char* snapshot_capture_stage_name(SnapshotCaptureStage stage) {
    switch (stage) {
        case SnapshotCaptureStage::Manual:
            return "Manual";
        case SnapshotCaptureStage::AutomaticSegmentBegin:
            return "AutomaticSegmentBegin";
        case SnapshotCaptureStage::AutomaticSegmentEnd:
            return "AutomaticSegmentEnd";
        case SnapshotCaptureStage::SessionStepBegin:
            return "SessionStepBegin";
        case SnapshotCaptureStage::SessionAfterDispatch:
            return "SessionAfterDispatch";
        case SnapshotCaptureStage::SessionStepEnd:
            return "SessionStepEnd";
        case SnapshotCaptureStage::CycleStepBegin:
            return "CycleStepBegin";
        case SnapshotCaptureStage::CycleAfterInputSync:
            return "CycleAfterInputSync";
        case SnapshotCaptureStage::CycleAfterRunSingle:
            return "CycleAfterRunSingle";
        case SnapshotCaptureStage::CycleAfterEmitOutputs:
            return "CycleAfterEmitOutputs";
        case SnapshotCaptureStage::CycleAfterKernelRun:
            return "CycleAfterKernelRun";
        case SnapshotCaptureStage::CycleAfterPortGroupEndCycle:
            return "CycleAfterPortGroupEndCycle";
        case SnapshotCaptureStage::CycleAfterKernelEndCycle:
            return "CycleAfterKernelEndCycle";
        case SnapshotCaptureStage::CycleStepEnd:
            return "CycleStepEnd";
        case SnapshotCaptureStage::KernelRunBegin:
            return "KernelRunBegin";
        case SnapshotCaptureStage::KernelAfterInputSync:
            return "KernelAfterInputSync";
        case SnapshotCaptureStage::KernelAfterRunSingle:
            return "KernelAfterRunSingle";
        case SnapshotCaptureStage::KernelAfterEmitOutputs:
            return "KernelAfterEmitOutputs";
        case SnapshotCaptureStage::KernelRunEnd:
            return "KernelRunEnd";
        case SnapshotCaptureStage::KernelEndCycleEnd:
            return "KernelEndCycleEnd";
        case SnapshotCaptureStage::ComponentRunBegin:
            return "ComponentRunBegin";
        case SnapshotCaptureStage::ComponentAfterInputSync:
            return "ComponentAfterInputSync";
        case SnapshotCaptureStage::ComponentAfterRunSingle:
            return "ComponentAfterRunSingle";
        case SnapshotCaptureStage::ComponentAfterEmitOutputs:
            return "ComponentAfterEmitOutputs";
        case SnapshotCaptureStage::ComponentRunEnd:
            return "ComponentRunEnd";
        case SnapshotCaptureStage::ComponentEndCycleEnd:
            return "ComponentEndCycleEnd";
    }
    return "Unknown";
}

const char* waveform_signal_kind_name(WaveformSignalKind kind) {
    switch (kind) {
        case WaveformSignalKind::State:
            return "State";
        case WaveformSignalKind::StateArrayElement:
            return "StateArrayElement";
        case WaveformSignalKind::PortInput:
            return "PortInput";
        case WaveformSignalKind::PortOutput:
            return "PortOutput";
    }
    return "Unknown";
}

std::string default_snapshot_capture_directory() {
    try {
        return (std::filesystem::current_path() / "snapshot_traces").string();
    } catch (const std::filesystem::filesystem_error& ex) {
        raise_io("SnapshotCapture", std::string("cannot resolve current path: ") + ex.what());
    }
    return "snapshot_traces";
}

std::string prepare_snapshot_capture_segment_directory(const std::string& root_directory,
                                                       const std::string& object_kind,
                                                       const std::string& object_name,
                                                       std::uint64_t start_cycle,
                                                       std::uint64_t segment_index) {
    const std::string base_name =
        sanitize_path_component(object_kind) + "." +
        current_datetime_label() + "." +
        compact_scaled_cycle(start_cycle);
    auto directory = std::filesystem::path(root_directory) /
                     (sanitize_path_component(object_kind) + "_" +
                      sanitize_path_component(object_name)) /
                     base_name;
    if (std::filesystem::exists(directory)) {
        directory = directory.string() + "." + padded_index(segment_index);
    }
    create_directories_or_throw(directory / "checkpoints");
    return directory.string();
}

std::string prepare_manual_checkpoint_path(const std::string& root_directory,
                                           const std::string& object_kind,
                                           const std::string& object_name,
                                           std::uint64_t sequence) {
    const auto path = std::filesystem::path(root_directory) /
                      (sanitize_path_component(object_kind) + "_" +
                       sanitize_path_component(object_name)) /
                      "checkpoints" /
                      ("manual_" + padded_index(sequence) + ".pxsckpt");
    create_directories_or_throw(path.parent_path());
    return path.string();
}

std::string snapshot_capture_waveform_path(const std::string& segment_directory) {
    return (std::filesystem::path(segment_directory) / "waveform.jsonl").string();
}

std::string snapshot_capture_first_checkpoint_path(const std::string& segment_directory) {
    return (std::filesystem::path(segment_directory) / "checkpoints" / "first.pxsckpt").string();
}

std::string snapshot_capture_last_checkpoint_path(const std::string& segment_directory) {
    return (std::filesystem::path(segment_directory) / "checkpoints" / "last.pxsckpt").string();
}

void write_snapshot_capture_manifest(const std::string& segment_directory,
                                     const std::string& object_kind,
                                     const std::string& object_name,
                                     std::uint64_t segment_index,
                                     std::uint64_t frame_count,
                                     bool closed) {
    const auto manifest_path =
        (std::filesystem::path(segment_directory) / "manifest.json").string();
    auto out = open_output_file(manifest_path);
    out << "{\n";
    out << "  \"format\": \"project_xs.snapshot_segment\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"object_kind\": " << quoted(object_kind) << ",\n";
    out << "  \"object_name\": " << quoted(object_name) << ",\n";
    out << "  \"mode\": \"Automatic\",\n";
    out << "  \"segment_index\": " << segment_index << ",\n";
    out << "  \"frame_count\": " << frame_count << ",\n";
    out << "  \"closed\": " << (closed ? "true" : "false") << ",\n";
    out << "  \"waveform\": \"waveform.jsonl\",\n";
    out << "  \"checkpoints\": {\n";
    out << "    \"first\": \"checkpoints/first.pxsckpt\",\n";
    out << "    \"last\": \"checkpoints/last.pxsckpt\"\n";
    out << "  }\n";
    out << "}\n";
}

void render_snapshot_capture_html(const std::string& segment_directory) {
    const char* disable = std::getenv("PROJECT_XS_DISABLE_WAVEFORM_HTML");
    if (disable && std::string(disable) == "1") {
        return;
    }

    const char* python_env = std::getenv("PROJECT_XS_PYTHON");
    const std::string python = python_env ? python_env : "python3";
    const char* script_env = std::getenv("PROJECT_XS_WAVEFORM_HTML_SCRIPT");
    const std::string script = script_env ? script_env : "script/generate_waveform_html.py";

    const auto script_path = std::filesystem::path(script);
    if (!std::filesystem::exists(script_path)) {
        raise_io("SnapshotCapture",
                 "waveform html script not found: " + script_path.string());
    }

    const std::string command = shell_quote(python) + " " +
                                shell_quote(script_path.string()) + " " +
                                shell_quote(segment_directory) + " --quiet";
    const int status = std::system(command.c_str());
    if (status != 0) {
        raise_io("SnapshotCapture",
                 "waveform html generation failed for " + segment_directory);
    }
}

void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const KernelComponent& component,
                                 PortValueBase base) {
    auto out = open_output_file(path, std::ios::out | std::ios::app);
    append_waveform_frame_common(out,
                                 "kernel_component",
                                 component.name(),
                                 component.frequency_hz(),
                                 component.current_cycle(),
                                 sequence,
                                 stage,
                                 capture_component_waveform_frame(component, sequence, base));
}

void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const Kernel& kernel,
                                 PortValueBase base) {
    auto out = open_output_file(path, std::ios::out | std::ios::app);
    append_waveform_frame_common(out,
                                 "kernel",
                                 kernel.name(),
                                 kernel.frequency_hz(),
                                 kernel.current_cycle(),
                                 sequence,
                                 stage,
                                 capture_kernel_waveform_frame(kernel, sequence, base));
}

void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const CycleSimulator& simulator,
                                 PortValueBase base) {
    auto out = open_output_file(path, std::ios::out | std::ios::app);
    append_waveform_frame_common(out,
                                 "cycle_simulator",
                                 simulator.name(),
                                 simulator.frequency_hz(),
                                 simulator.current_cycle(),
                                 sequence,
                                 stage,
                                 capture_waveform_frame(simulator, base));
}

void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const SimulationSession& session,
                                 PortValueBase base) {
    auto out = open_output_file(path, std::ios::out | std::ios::app);
    out << "{";
    out << "\"format\":\"project_xs.waveform_frame\"";
    out << ",\"version\":1";
    out << ",\"object_kind\":\"simulation_session\"";
    out << ",\"object_name\":\"session\"";
    out << ",\"root_timing\":{";
    out << "\"kind\":\"simulation_session\"";
    out << ",\"name\":\"session\"";
    out << ",\"frequency_hz\":" << long_double_string(session.frequency_hz());
    out << ",\"cycle\":" << session.current_tick();
    out << "}";
    out << ",\"sequence\":" << sequence;
    out << ",\"stage\":" << quoted(snapshot_capture_stage_name(stage));
    out << ",\"current_tick\":" << session.current_tick();
    out << ",\"session_frequency_hz\":"
        << long_double_string(session.frequency_hz());
    out << ",\"time_seconds\":" << quoted(long_double_string(session.current_time_seconds()));
    out << ",\"finished\":" << (session.is_finished() ? "true" : "false");
    out << ",\"simulators\":[";
    const auto& simulators = session.simulators();
    for (std::size_t index = 0; index < simulators.size(); ++index) {
        if (index != 0) {
            out << ",";
        }
        const auto& simulator = *simulators[index];
        const auto frame = capture_waveform_frame(simulator, base);
        out << "{";
        out << "\"name\":" << quoted(simulator.name());
        out << ",\"cycle\":" << simulator.current_cycle();
        out << ",\"frequency_hz\":" << long_double_string(simulator.frequency_hz());
        out << ",\"session_ticks_per_simulator_cycle\":"
            << long_double_string(session.frequency_hz() / simulator.frequency_hz());
        out << ",\"simulator_cycles_per_session_tick\":"
            << long_double_string(simulator.frequency_hz() / session.frequency_hz());
        out << ",\"finished\":" << (simulator.is_finished() ? "true" : "false");
        out << ",\"signals\":";
        append_signals_json(out, frame.signals);
        out << "}";
    }
    out << "]";
    out << "}\n";
}

void save_checkpoint(const std::string& path, const KernelComponentSnapshot& snapshot) {
    BinaryWriter writer(path);
    write_checkpoint_header(writer, "kernel_component");
    write_component_snapshot_body(writer, snapshot);
}

void save_checkpoint(const std::string& path, const KernelSnapshot& snapshot) {
    BinaryWriter writer(path);
    write_checkpoint_header(writer, "kernel");
    write_kernel_snapshot_body(writer, snapshot);
}

void save_checkpoint(const std::string& path, const CycleSimulatorSnapshot& snapshot) {
    BinaryWriter writer(path);
    write_checkpoint_header(writer, "cycle_simulator");
    write_cycle_simulator_snapshot_body(writer, snapshot);
}

void save_checkpoint(const std::string& path, const SimulationSessionSnapshot& snapshot) {
    BinaryWriter writer(path);
    write_checkpoint_header(writer, "simulation_session");
    write_session_snapshot_body(writer, snapshot);
}

KernelComponentSnapshot load_kernel_component_checkpoint(
    const std::string& path,
    const KernelComponentSnapshot& layout) {
    BinaryReader reader(path);
    const auto version = read_checkpoint_header(reader, "kernel_component");
    return read_component_snapshot_body(reader, layout, version);
}

KernelSnapshot load_kernel_checkpoint(const std::string& path,
                                      const KernelSnapshot& layout) {
    BinaryReader reader(path);
    const auto version = read_checkpoint_header(reader, "kernel");
    return read_kernel_snapshot_body(reader, layout, version);
}

CycleSimulatorSnapshot load_cycle_simulator_checkpoint(
    const std::string& path,
    const CycleSimulatorSnapshot& layout) {
    BinaryReader reader(path);
    const auto version = read_checkpoint_header(reader, "cycle_simulator");
    return read_cycle_simulator_snapshot_body(reader, layout, version);
}

SimulationSessionSnapshot load_simulation_session_checkpoint(
    const std::string& path,
    const SimulationSessionSnapshot& layout) {
    BinaryReader reader(path);
    const auto version = read_checkpoint_header(reader, "simulation_session");
    return read_session_snapshot_body(reader, layout, version);
}

}  // namespace project_xs::sim
