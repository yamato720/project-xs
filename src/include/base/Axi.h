#ifndef PROJECT_XS_BASE_AXI_H
#define PROJECT_XS_BASE_AXI_H

#include "base/Port.h"
#include "base/State.h"

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
    // 构造一个 AXI4-Lite 接口对象，并把端口绑定到给定信号上。
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
    // 写地址本体端口。
    std::shared_ptr<Port> awaddr;

    // 写地址 valid 端口。
    std::shared_ptr<Port> awvalid;

    // 写地址 ready 端口。
    std::shared_ptr<Port> awready;

    // 写数据通道。
    // 写数据本体端口。
    std::shared_ptr<Port> wdata;

    // 写数据 valid 端口。
    std::shared_ptr<Port> wvalid;

    // 写数据 ready 端口。
    std::shared_ptr<Port> wready;

    // 写响应通道。
    // 写响应码端口。
    std::shared_ptr<Port> bresp;

    // 写响应 valid 端口。
    std::shared_ptr<Port> bvalid;

    // 写响应 ready 端口。
    std::shared_ptr<Port> bready;

    // 读地址通道。
    // 读地址本体端口。
    std::shared_ptr<Port> araddr;

    // 读地址 valid 端口。
    std::shared_ptr<Port> arvalid;

    // 读地址 ready 端口。
    std::shared_ptr<Port> arready;

    // 读数据通道。
    // 读数据本体端口。
    std::shared_ptr<Port> rdata;

    // 读响应码端口。
    std::shared_ptr<Port> rresp;

    // 读数据 valid 端口。
    std::shared_ptr<Port> rvalid;

    // 读数据 ready 端口。
    std::shared_ptr<Port> rready;

  private:
    // AXI4-Lite 接口内部的单状态目录。
    StateSet state_set_;

    // 写地址通道状态。
    State<std::uint32_t> awaddr_state_{"axil_awaddr", "AXI-Lite 写地址", 0U};
    State<bool> awvalid_state_{"axil_awvalid", "AXI-Lite 写地址 valid", false};
    State<bool> awready_state_{"axil_awready", "AXI-Lite 写地址 ready", false};

    // 写数据通道状态。
    State<std::uint32_t> wdata_state_{"axil_wdata", "AXI-Lite 写数据", 0U};
    State<bool> wvalid_state_{"axil_wvalid", "AXI-Lite 写数据 valid", false};
    State<bool> wready_state_{"axil_wready", "AXI-Lite 写数据 ready", false};

    // 写响应通道状态。
    State<std::uint8_t> bresp_state_{"axil_bresp", "AXI-Lite 写响应码", std::uint8_t{0}};
    State<bool> bvalid_state_{"axil_bvalid", "AXI-Lite 写响应 valid", false};
    State<bool> bready_state_{"axil_bready", "AXI-Lite 写响应 ready", false};

    // 读地址通道状态。
    State<std::uint32_t> araddr_state_{"axil_araddr", "AXI-Lite 读地址", 0U};
    State<bool> arvalid_state_{"axil_arvalid", "AXI-Lite 读地址 valid", false};
    State<bool> arready_state_{"axil_arready", "AXI-Lite 读地址 ready", false};

    // 读数据通道状态。
    State<std::uint32_t> rdata_state_{"axil_rdata", "AXI-Lite 读数据", 0U};
    State<std::uint8_t> rresp_state_{"axil_rresp", "AXI-Lite 读响应码", std::uint8_t{0}};
    State<bool> rvalid_state_{"axil_rvalid", "AXI-Lite 读数据 valid", false};
    State<bool> rready_state_{"axil_rready", "AXI-Lite 读数据 ready", false};
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
    // 构造一个 AXI4-Stream 接口对象，并把端口绑定到给定信号上。
    AxiStreamIf(std::uint64_t* tdata,
                bool* tvalid,
                bool* tready,
                bool* tlast = nullptr);

    // Stream 数据端口。
    std::shared_ptr<Port> tdata;

    // Stream valid 端口。
    std::shared_ptr<Port> tvalid;

    // Stream ready 端口。
    std::shared_ptr<Port> tready;

    // Stream last 端口；若未提供则可能为空。
    std::shared_ptr<Port> tlast;

  private:
    // AXI4-Stream 接口内部的单状态目录。
    StateSet state_set_;

    // Stream 数据状态。
    State<std::uint64_t> tdata_state_{"axis_tdata", "AXI-Stream 数据", 0ULL};

    // Stream 握手状态。
    State<bool> tvalid_state_{"axis_tvalid", "AXI-Stream valid", false};
    State<bool> tready_state_{"axis_tready", "AXI-Stream ready", false};

    // Stream 最后一拍状态。
    std::unique_ptr<State<bool>> tlast_state_;
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
    // 构造一个 AXI4 Master 接口对象，并把端口绑定到给定信号上。
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
    // 读地址本体端口。
    std::shared_ptr<Port> araddr;

    // 读地址 valid 端口。
    std::shared_ptr<Port> arvalid;

    // 读地址 ready 端口。
    std::shared_ptr<Port> arready;

    // 读数据通道。
    // 读数据本体端口。
    std::shared_ptr<Port> rdata;

    // 读数据 valid 端口。
    std::shared_ptr<Port> rvalid;

    // 读数据 ready 端口。
    std::shared_ptr<Port> rready;

    // 读突发最后一拍标志端口。
    std::shared_ptr<Port> rlast;

    // 写地址通道。
    // 写地址本体端口。
    std::shared_ptr<Port> awaddr;

    // 写地址 valid 端口。
    std::shared_ptr<Port> awvalid;

    // 写地址 ready 端口。
    std::shared_ptr<Port> awready;

    // 写数据通道。
    // 写数据本体端口。
    std::shared_ptr<Port> wdata;

    // 写数据 valid 端口。
    std::shared_ptr<Port> wvalid;

    // 写数据 ready 端口。
    std::shared_ptr<Port> wready;

    // 写突发最后一拍标志端口。
    std::shared_ptr<Port> wlast;

    // 写响应通道。
    // 写响应码端口。
    std::shared_ptr<Port> bresp;

    // 写响应 valid 端口。
    std::shared_ptr<Port> bvalid;

    // 写响应 ready 端口。
    std::shared_ptr<Port> bready;

  private:
    // AXI4 Master 接口内部的单状态目录。
    StateSet state_set_;

    // 读地址通道状态。
    State<std::uint64_t> araddr_state_{"axi_araddr", "AXI 读地址", 0ULL};
    State<bool> arvalid_state_{"axi_arvalid", "AXI 读地址 valid", false};
    State<bool> arready_state_{"axi_arready", "AXI 读地址 ready", false};

    // 读数据通道状态。
    State<std::uint64_t> rdata_state_{"axi_rdata", "AXI 读数据", 0ULL};
    State<bool> rvalid_state_{"axi_rvalid", "AXI 读数据 valid", false};
    State<bool> rready_state_{"axi_rready", "AXI 读数据 ready", false};
    State<bool> rlast_state_{"axi_rlast", "AXI 读数据 last", false};

    // 写地址通道状态。
    State<std::uint64_t> awaddr_state_{"axi_awaddr", "AXI 写地址", 0ULL};
    State<bool> awvalid_state_{"axi_awvalid", "AXI 写地址 valid", false};
    State<bool> awready_state_{"axi_awready", "AXI 写地址 ready", false};

    // 写数据通道状态。
    State<std::uint64_t> wdata_state_{"axi_wdata", "AXI 写数据", 0ULL};
    State<bool> wvalid_state_{"axi_wvalid", "AXI 写数据 valid", false};
    State<bool> wready_state_{"axi_wready", "AXI 写数据 ready", false};
    State<bool> wlast_state_{"axi_wlast", "AXI 写数据 last", false};

    // 写响应通道状态。
    State<std::uint8_t> bresp_state_{"axi_bresp", "AXI 写响应码", std::uint8_t{0}};
    State<bool> bvalid_state_{"axi_bvalid", "AXI 写响应 valid", false};
    State<bool> bready_state_{"axi_bready", "AXI 写响应 ready", false};
};

}  // namespace project_xs::sim

#endif
