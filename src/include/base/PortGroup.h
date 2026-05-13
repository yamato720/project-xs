#ifndef PROJECT_XS_BASE_PORT_GROUP_H
#define PROJECT_XS_BASE_PORT_GROUP_H

#include "base/Port.h"
#include "base/Error.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace project_xs::sim {

// 无协议端口组。
// 设计目标：
// - 仅承载普通端口，不包含 AXI/FIFO 等协议含义
// - 支持多输入、多输出
// - 允许给端口组命名，便于表示一个逻辑接口域
// - 提供统一的 sync / emit / end_cycle / clear 操作
class PortGroup {
  public:
    // 端口组运行时快照。
    using Snapshot = PortGroupSnapshot;

    // 构造一个具名端口组。
    explicit PortGroup(std::string name);

    // 返回端口组名称。
    const std::string& name() const { return name_; }

    // 向端口组内加入一个输入端口。
    void add_input(const std::shared_ptr<Port>& input);

    // 向端口组内加入一个输出端口。
    void add_output(const std::shared_ptr<Port>& output);

    // 返回所有输入/输出端口。
    const std::vector<std::shared_ptr<Port>>& inputs() const { return inputs_; }
    const std::vector<std::shared_ptr<Port>>& outputs() const { return outputs_; }

    // 取第 index 个输入/输出端口。
    const std::shared_ptr<Port>& input_at(std::size_t index) const;
    const std::shared_ptr<Port>& output_at(std::size_t index) const;

    // 按名字查找输入/输出端口。
    // find_* 找不到时返回空指针；get_* 找不到时抛异常。
    // 这是端口组层面的统一命名查询入口。
    std::shared_ptr<Port> find_input(std::string_view name) const;

    // 按名字查找一个输出端口。
    std::shared_ptr<Port> find_output(std::string_view name) const;

    // 按名字查找一个端口，不区分输入/输出。
    std::shared_ptr<Port> find_port(std::string_view name) const;

    // 按名字获取一个输入端口；找不到时报错。
    const std::shared_ptr<Port>& get_input(std::string_view name) const;

    // 按名字获取一个输出端口；找不到时报错。
    const std::shared_ptr<Port>& get_output(std::string_view name) const;

    // 按名字获取一个端口，不区分输入/输出；找不到时报错。
    const std::shared_ptr<Port>& get_port(std::string_view name) const;

    // 返回指定输入端口的信息字符串。
    std::string input_info(std::string_view name,
                           PortValueBase base = PortValueBase::Decimal) const;

    // 返回指定输出端口的信息字符串。
    std::string output_info(std::string_view name,
                            PortValueBase base = PortValueBase::Decimal) const;

    // 返回指定端口的信息字符串，不区分输入/输出。
    std::string port_info(std::string_view name,
                          PortValueBase base = PortValueBase::Decimal) const;

    // 统一同步所有输入端口。
    void sync_inputs();

    // 统一发射所有输出端口当前绑定变量的值。
    void emit_outputs();

    // 统一提交所有端口的拍末状态。
    void end_cycle();

    // 清空组内所有端口状态。
    void clear();

    // 把组内所有端口初始化为“0 可见”状态。
    void initialize_zero();

    // 返回组内所有输入/输出端口的信息字符串。
    // 每个端口通过 Port::info() 生成，端口之间以 " | " 拼接。
    // 这三个 all_*_info 是端口组层面的统一批量信息接口。
    std::string all_inputs_info(PortValueBase base = PortValueBase::Decimal) const;
    std::string all_outputs_info(PortValueBase base = PortValueBase::Decimal) const;
    std::string all_ports_info(PortValueBase base = PortValueBase::Decimal) const;

    // 兼容旧接口：返回全部输入端口的信息字符串。
    std::string info_inputs(PortValueBase base = PortValueBase::Decimal) const;

    // 兼容旧接口：返回全部输出端口的信息字符串。
    std::string info_outputs(PortValueBase base = PortValueBase::Decimal) const;

    // 返回整个端口组的摘要信息。
    std::string info(PortValueBase base = PortValueBase::Decimal) const;

    // 收集当前端口组的结构化诊断。
    void collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const;

    // 校验当前端口组是否合法，并返回诊断列表。
    std::vector<error::Diagnostic> validate() const;

    // 校验当前端口组是否合法；若存在 Error 直接抛出第一条。
    void validate_or_throw() const;

    // 保存当前端口组运行态。
    Snapshot snapshot() const;

    // 恢复当前端口组运行态。
    void restore(const Snapshot& snapshot);

  private:
    // 把一组端口逐个转成信息字符串并拼接。
    static std::string info_ports(const std::vector<std::shared_ptr<Port>>& ports,
                                  PortValueBase base);

    // 当前端口组的逻辑名称。
    std::string name_;

    // 当前端口组内注册的全部输入端口。
    std::vector<std::shared_ptr<Port>> inputs_;

    // 当前端口组内注册的全部输出端口。
    std::vector<std::shared_ptr<Port>> outputs_;
};

}  // namespace project_xs::sim

#endif
