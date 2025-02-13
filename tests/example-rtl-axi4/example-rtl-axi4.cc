/*
 * This is a small example showing howto connect an RTL AXI Device
 * to a SystemC/TLM simulation using the TLM-2-AXI bridges.
 *
 * Copyright (c) 2018 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sstream>

#define SC_INCLUDE_DYNAMIC_PROCESSES

#include "systemc"
using namespace sc_core;
using namespace sc_dt;
using namespace std;

#include "tlm.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"

#include "tlm-bridges/tlm2axi-bridge.h"
#include "checkers/pc-axi.h"
#include "traffic-generators/tg-tlm.h"
#include "traffic-generators/traffic-desc.h"

#include "test-modules/signals-axi.h"
#include "test-modules/utils.h"

#include "Vaxifull_dev.h"

using namespace utils;

#define CONNECT_DUT(DUT, SIGS, SIGNAME) DUT.s00_axi_ ## SIGNAME(SIGS.SIGNAME)

TrafficDesc transactions(merge({
    // Write something to address 8
    Write(8, DATA(0x1, 0x2, 0x3, 0x4)),
    // Read it back and check that we get the expected data.
    Read(8, 4),
    Expect(DATA(0x1, 0x2, 0x3, 0x4), 4)
}));

AXIPCConfig checker_config()
{
    AXIPCConfig cfg;

    cfg.enable_all_checks();

    return cfg;
}

// Top simulation module.
SC_MODULE(Top)
{
    sc_clock clk;
    sc_signal<bool> rst_n; // Active low.

    TLMTrafficGenerator tg;
    AXISignals<10, 128 > signals;
    tlm2axi_bridge<10, 128 > bridge;
    AXIProtocolChecker<10, 128 > checker;
    // dut is the RTL AXI device we're testing.
    Vaxifull_dev dut;

    Top(sc_module_name name) :
        clk("clk", sc_time(1, SC_US)),
        rst_n("rst_n"),
        tg("traffic_generator"),
        signals("signals"),
        bridge("bridge"),
        checker("checker", checker_config()),
        dut("dut")
    {
        // Configure the Traffic generator.
        tg.setStartDelay(sc_time(10, SC_US));
        tg.enableDebug();
        tg.addTransfers(transactions);
        tg.socket.bind(bridge.tgt_socket);

        // Wire up the clock and reset signals.
        bridge.clk(clk);
        bridge.resetn(rst_n);
        checker.clk(clk);
        checker.resetn(rst_n);
        dut.s00_axi_aclk(clk);
        dut.s00_axi_aresetn(rst_n);

        // Wire-up the bridge and checker.
        signals.connect(bridge);
        signals.connect(checker);

        // Since the AXI Dut doesn't use the same naming conventions
        // as AXISignals, we need to manually connect everything.
        dut.s00_axi_awvalid(signals.awvalid);
        dut.s00_axi_awready(signals.awready);
        dut.s00_axi_awaddr(signals.awaddr);
        dut.s00_axi_awprot(signals.awprot);
        dut.s00_axi_awuser(signals.awuser);
        dut.s00_axi_awregion(signals.awregion);
        dut.s00_axi_awqos(signals.awqos);
        dut.s00_axi_awcache(signals.awcache);
        dut.s00_axi_awburst(signals.awburst);
        dut.s00_axi_awsize(signals.awsize);
        dut.s00_axi_awlen(signals.awlen);
        dut.s00_axi_awid(signals.awid);
        dut.s00_axi_awlock(signals.awlock);

        dut.s00_axi_arvalid(signals.arvalid);
        dut.s00_axi_arready(signals.arready);
        dut.s00_axi_araddr(signals.araddr);
        dut.s00_axi_arprot(signals.arprot);
        dut.s00_axi_aruser(signals.aruser);
        dut.s00_axi_arregion(signals.arregion);
        dut.s00_axi_arqos(signals.arqos);
        dut.s00_axi_arcache(signals.arcache);
        dut.s00_axi_arburst(signals.arburst);
        dut.s00_axi_arsize(signals.arsize);
        dut.s00_axi_arlen(signals.arlen);
        dut.s00_axi_arid(signals.arid);
        dut.s00_axi_arlock(signals.arlock);

        dut.s00_axi_wvalid(signals.wvalid);
        dut.s00_axi_wready(signals.wready);
        dut.s00_axi_wdata(signals.wdata);
        dut.s00_axi_wstrb(signals.wstrb);
        dut.s00_axi_wuser(signals.wuser);
        dut.s00_axi_wlast(signals.wlast);

        dut.s00_axi_bvalid(signals.bvalid);
        dut.s00_axi_bready(signals.bready);
        dut.s00_axi_bresp(signals.bresp);
        dut.s00_axi_buser(signals.buser);
        dut.s00_axi_bid(signals.bid);

        dut.s00_axi_rvalid(signals.rvalid);
        dut.s00_axi_rready(signals.rready);
        dut.s00_axi_rdata(signals.rdata);
        dut.s00_axi_rresp(signals.rresp);
        dut.s00_axi_ruser(signals.ruser);
        dut.s00_axi_rid(signals.rid);
        dut.s00_axi_rlast(signals.rlast);
    }
};

int sc_main(int argc, char *argv[])
{
    Verilated::commandArgs(argc, argv);
    Top top("Top");

    sc_trace_file *trace_fp = sc_create_vcd_trace_file(argv[0]);

    top.signals.Trace(trace_fp);

    // Reset is active low. Emit a reset cycle.
    top.rst_n.write(false);
    sc_start(4, SC_US);
    top.rst_n.write(true);

    sc_start(14, SC_US);
    sc_stop();

    if (trace_fp) {
        sc_close_vcd_trace_file(trace_fp);
    }
    
    return 0;
}