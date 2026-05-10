#ifndef PROJECT_XS_BASE_AXI_H
#define PROJECT_XS_BASE_AXI_H

#include "base/Port.h"

#include <cstdint>
#include <memory>

namespace project_xs::sim {

// AXI4-Lite 接口抽象。
// 这是控制面接口，适合承载：
// - 标量配置寄存器
// - 启停控制
// - 状态查询
//
// 这里没有试图完整覆盖 AXI4-Lite 全协议，而是抽象出做周期仿真最常用的几类信号：
// - 写地址/写数据/写响应
// - 读地址/读数据
//
// 默认选择：
// - 地址和数据多用 RegPort，表示寄存一级的控制路径
// - ready/valid/bresp/rresp 可按需要混用 WirePort/RegPort
class AxiLiteIf {
  public:
    AxiLiteIf(std::uint32_t* awaddr,
              bool* awvalid,
              bool* awready,
              std::uint32_t* wdata,
              bool* wvalid,
              bool* wready,
              std::uint8_t* bresp,
              bool* bvalid,
              bool* bready,
              std::uint32_t* araddr,
              bool* arvalid,
              bool* arready,
              std::uint32_t* rdata,
              std::uint8_t* rresp,
              bool* rvalid,
              bool* rready);

    // 写地址通道。
    std::shared_ptr<Port> awaddr;
    std::shared_ptr<Port> awvalid;
    std::shared_ptr<Port> awready;

    // 写数据通道。
    std::shared_ptr<Port> wdata;
    std::shared_ptr<Port> wvalid;
    std::shared_ptr<Port> wready;

    // 写响应通道。
    std::shared_ptr<Port> bresp;
    std::shared_ptr<Port> bvalid;
    std::shared_ptr<Port> bready;

    // 读地址通道。
    std::shared_ptr<Port> araddr;
    std::shared_ptr<Port> arvalid;
    std::shared_ptr<Port> arready;

    // 读数据通道。
    std::shared_ptr<Port> rdata;
    std::shared_ptr<Port> rresp;
    std::shared_ptr<Port> rvalid;
    std::shared_ptr<Port> rready;
};

// AXI4-Stream 接口抽象。
// 适合模拟：
// - HLS stream
// - 有 valid/ready 握手的数据流
// - 可选的 last 信号
//
// 这层已经足够支撑大多数“组件间流式传值”的行为仿真。
class AxiStreamIf {
  public:
    AxiStreamIf(std::uint64_t* tdata,
                bool* tvalid,
                bool* tready,
                bool* tlast = nullptr);

    std::shared_ptr<Port> tdata;
    std::shared_ptr<Port> tvalid;
    std::shared_ptr<Port> tready;
    std::shared_ptr<Port> tlast;
};

// AXI4 Master 接口抽象。
// 这层偏向“内存映射主设备访问”：
// - 读地址 AR
// - 读数据 R
// - 写地址 AW
// - 写数据 W
// - 写响应 B
//
// 为了控制复杂度，这里只先放出常见核心字段，
// 省略 burst/len/size/id 等次一级细节；后续需要时可以继续补。
class AxiMasterIf {
  public:
    AxiMasterIf(std::uint64_t* araddr,
                bool* arvalid,
                bool* arready,
                std::uint64_t* rdata,
                bool* rvalid,
                bool* rready,
                bool* rlast,
                std::uint64_t* awaddr,
                bool* awvalid,
                bool* awready,
                std::uint64_t* wdata,
                bool* wvalid,
                bool* wready,
                bool* wlast,
                std::uint8_t* bresp,
                bool* bvalid,
                bool* bready);

    // 读地址通道。
    std::shared_ptr<Port> araddr;
    std::shared_ptr<Port> arvalid;
    std::shared_ptr<Port> arready;

    // 读数据通道。
    std::shared_ptr<Port> rdata;
    std::shared_ptr<Port> rvalid;
    std::shared_ptr<Port> rready;
    std::shared_ptr<Port> rlast;

    // 写地址通道。
    std::shared_ptr<Port> awaddr;
    std::shared_ptr<Port> awvalid;
    std::shared_ptr<Port> awready;

    // 写数据通道。
    std::shared_ptr<Port> wdata;
    std::shared_ptr<Port> wvalid;
    std::shared_ptr<Port> wready;
    std::shared_ptr<Port> wlast;

    // 写响应通道。
    std::shared_ptr<Port> bresp;
    std::shared_ptr<Port> bvalid;
    std::shared_ptr<Port> bready;
};

}  // namespace project_xs::sim

#endif
