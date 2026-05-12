#include "base/Axi.h"

namespace project_xs::sim {

AxiLiteIf::AxiLiteIf(std::uint32_t* awaddr_ptr,
                     bool* awvalid_ptr,
                     bool* awready_ptr,
                     std::uint32_t* wdata_ptr,
                     bool* wvalid_ptr,
                     bool* wready_ptr,
                     std::uint8_t* bresp_ptr,
                     bool* bvalid_ptr,
                     bool* bready_ptr,
                     std::uint32_t* araddr_ptr,
                     bool* arvalid_ptr,
                     bool* arready_ptr,
                     std::uint32_t* rdata_ptr,
                     std::uint8_t* rresp_ptr,
                     bool* rvalid_ptr,
                     bool* rready_ptr)
    : awaddr(nullptr),
      awvalid(nullptr),
      awready(nullptr),
      wdata(nullptr),
      wvalid(nullptr),
      wready(nullptr),
      bresp(nullptr),
      bvalid(nullptr),
      bready(nullptr),
      araddr(nullptr),
      arvalid(nullptr),
      arready(nullptr),
      rdata(nullptr),
      rresp(nullptr),
      rvalid(nullptr),
      rready(nullptr) {
    awaddr_state_ = awaddr_ptr ? *awaddr_ptr : 0U;
    awvalid_state_ = awvalid_ptr ? *awvalid_ptr : false;
    awready_state_ = awready_ptr ? *awready_ptr : false;
    wdata_state_ = wdata_ptr ? *wdata_ptr : 0U;
    wvalid_state_ = wvalid_ptr ? *wvalid_ptr : false;
    wready_state_ = wready_ptr ? *wready_ptr : false;
    bresp_state_ = bresp_ptr ? *bresp_ptr : std::uint8_t{0};
    bvalid_state_ = bvalid_ptr ? *bvalid_ptr : false;
    bready_state_ = bready_ptr ? *bready_ptr : false;
    araddr_state_ = araddr_ptr ? *araddr_ptr : 0U;
    arvalid_state_ = arvalid_ptr ? *arvalid_ptr : false;
    arready_state_ = arready_ptr ? *arready_ptr : false;
    rdata_state_ = rdata_ptr ? *rdata_ptr : 0U;
    rresp_state_ = rresp_ptr ? *rresp_ptr : std::uint8_t{0};
    rvalid_state_ = rvalid_ptr ? *rvalid_ptr : false;
    rready_state_ = rready_ptr ? *rready_ptr : false;

    state_set_.register_state(awaddr_state_);
    state_set_.register_state(awvalid_state_);
    state_set_.register_state(awready_state_);
    state_set_.register_state(wdata_state_);
    state_set_.register_state(wvalid_state_);
    state_set_.register_state(wready_state_);
    state_set_.register_state(bresp_state_);
    state_set_.register_state(bvalid_state_);
    state_set_.register_state(bready_state_);
    state_set_.register_state(araddr_state_);
    state_set_.register_state(arvalid_state_);
    state_set_.register_state(arready_state_);
    state_set_.register_state(rdata_state_);
    state_set_.register_state(rresp_state_);
    state_set_.register_state(rvalid_state_);
    state_set_.register_state(rready_state_);

    awaddr = awaddr_state_.make_reg_output_port();
    awvalid = awvalid_state_.make_wire_output_port();
    awready = awready_state_.make_wire_input_port();
    wdata = wdata_state_.make_reg_output_port();
    wvalid = wvalid_state_.make_wire_output_port();
    wready = wready_state_.make_wire_input_port();
    bresp = bresp_state_.make_reg_input_port();
    bvalid = bvalid_state_.make_wire_input_port();
    bready = bready_state_.make_wire_output_port();
    araddr = araddr_state_.make_reg_output_port();
    arvalid = arvalid_state_.make_wire_output_port();
    arready = arready_state_.make_wire_input_port();
    rdata = rdata_state_.make_reg_input_port();
    rresp = rresp_state_.make_reg_input_port();
    rvalid = rvalid_state_.make_wire_input_port();
    rready = rready_state_.make_wire_output_port();
}

AxiStreamIf::AxiStreamIf(std::uint64_t* tdata_ptr,
                         bool* tvalid_ptr,
                         bool* tready_ptr,
                         bool* tlast_ptr)
    : tdata(nullptr),
      tvalid(nullptr),
      tready(nullptr),
      tlast(nullptr) {
    tdata_state_ = tdata_ptr ? *tdata_ptr : 0ULL;
    tvalid_state_ = tvalid_ptr ? *tvalid_ptr : false;
    tready_state_ = tready_ptr ? *tready_ptr : false;

    state_set_.register_state(tdata_state_);
    state_set_.register_state(tvalid_state_);
    state_set_.register_state(tready_state_);

    tdata = tdata_state_.make_wire_output_port();
    tvalid = tvalid_state_.make_wire_output_port();
    tready = tready_state_.make_wire_input_port();

    if (tlast_ptr) {
        tlast_state_ =
            std::make_unique<State<bool>>("axis_tlast", "AXI-Stream last", *tlast_ptr);
        state_set_.register_state(*tlast_state_);
        tlast = tlast_state_->make_wire_output_port();
    }
}

AxiMasterIf::AxiMasterIf(std::uint64_t* araddr_ptr,
                         bool* arvalid_ptr,
                         bool* arready_ptr,
                         std::uint64_t* rdata_ptr,
                         bool* rvalid_ptr,
                         bool* rready_ptr,
                         bool* rlast_ptr,
                         std::uint64_t* awaddr_ptr,
                         bool* awvalid_ptr,
                         bool* awready_ptr,
                         std::uint64_t* wdata_ptr,
                         bool* wvalid_ptr,
                         bool* wready_ptr,
                         bool* wlast_ptr,
                         std::uint8_t* bresp_ptr,
                         bool* bvalid_ptr,
                         bool* bready_ptr)
    : araddr(nullptr),
      arvalid(nullptr),
      arready(nullptr),
      rdata(nullptr),
      rvalid(nullptr),
      rready(nullptr),
      rlast(nullptr),
      awaddr(nullptr),
      awvalid(nullptr),
      awready(nullptr),
      wdata(nullptr),
      wvalid(nullptr),
      wready(nullptr),
      wlast(nullptr),
      bresp(nullptr),
      bvalid(nullptr),
      bready(nullptr) {
    araddr_state_ = araddr_ptr ? *araddr_ptr : 0ULL;
    arvalid_state_ = arvalid_ptr ? *arvalid_ptr : false;
    arready_state_ = arready_ptr ? *arready_ptr : false;
    rdata_state_ = rdata_ptr ? *rdata_ptr : 0ULL;
    rvalid_state_ = rvalid_ptr ? *rvalid_ptr : false;
    rready_state_ = rready_ptr ? *rready_ptr : false;
    rlast_state_ = rlast_ptr ? *rlast_ptr : false;
    awaddr_state_ = awaddr_ptr ? *awaddr_ptr : 0ULL;
    awvalid_state_ = awvalid_ptr ? *awvalid_ptr : false;
    awready_state_ = awready_ptr ? *awready_ptr : false;
    wdata_state_ = wdata_ptr ? *wdata_ptr : 0ULL;
    wvalid_state_ = wvalid_ptr ? *wvalid_ptr : false;
    wready_state_ = wready_ptr ? *wready_ptr : false;
    wlast_state_ = wlast_ptr ? *wlast_ptr : false;
    bresp_state_ = bresp_ptr ? *bresp_ptr : std::uint8_t{0};
    bvalid_state_ = bvalid_ptr ? *bvalid_ptr : false;
    bready_state_ = bready_ptr ? *bready_ptr : false;

    state_set_.register_state(araddr_state_);
    state_set_.register_state(arvalid_state_);
    state_set_.register_state(arready_state_);
    state_set_.register_state(rdata_state_);
    state_set_.register_state(rvalid_state_);
    state_set_.register_state(rready_state_);
    state_set_.register_state(rlast_state_);
    state_set_.register_state(awaddr_state_);
    state_set_.register_state(awvalid_state_);
    state_set_.register_state(awready_state_);
    state_set_.register_state(wdata_state_);
    state_set_.register_state(wvalid_state_);
    state_set_.register_state(wready_state_);
    state_set_.register_state(wlast_state_);
    state_set_.register_state(bresp_state_);
    state_set_.register_state(bvalid_state_);
    state_set_.register_state(bready_state_);

    araddr = araddr_state_.make_reg_output_port();
    arvalid = arvalid_state_.make_wire_output_port();
    arready = arready_state_.make_wire_input_port();
    rdata = rdata_state_.make_reg_input_port();
    rvalid = rvalid_state_.make_wire_input_port();
    rready = rready_state_.make_wire_output_port();
    rlast = rlast_state_.make_wire_input_port();
    awaddr = awaddr_state_.make_reg_output_port();
    awvalid = awvalid_state_.make_wire_output_port();
    awready = awready_state_.make_wire_input_port();
    wdata = wdata_state_.make_wire_output_port();
    wvalid = wvalid_state_.make_wire_output_port();
    wready = wready_state_.make_wire_input_port();
    wlast = wlast_state_.make_wire_output_port();
    bresp = bresp_state_.make_reg_input_port();
    bvalid = bvalid_state_.make_wire_input_port();
    bready = bready_state_.make_wire_output_port();
}

}  // namespace project_xs::sim
