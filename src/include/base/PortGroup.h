#ifndef PROJECT_XS_BASE_PORT_GROUP_H
#define PROJECT_XS_BASE_PORT_GROUP_H

#include "base/Port.h"

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
    std::shared_ptr<Port> find_input(std::string_view name) const;
    std::shared_ptr<Port> find_output(std::string_view name) const;
    const std::shared_ptr<Port>& get_input(std::string_view name) const;
    const std::shared_ptr<Port>& get_output(std::string_view name) const;

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
    std::string info_inputs(PortValueBase base = PortValueBase::Decimal) const;
    std::string info_outputs(PortValueBase base = PortValueBase::Decimal) const;

    // 复制另一个端口组的运行时状态。
    // 要求输入/输出端口数量、顺序和类型布局一致。
    void copy_runtime_from(const PortGroup& other);

  private:
    static std::string info_ports(const std::vector<std::shared_ptr<Port>>& ports,
                                  PortValueBase base);

    std::string name_;
    std::vector<std::shared_ptr<Port>> inputs_;
    std::vector<std::shared_ptr<Port>> outputs_;
};

}  // namespace project_xs::sim

#endif
