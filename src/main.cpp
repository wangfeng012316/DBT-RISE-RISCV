/*******************************************************************************
 * Copyright (C) 2017, 2018 MINRES Technologies GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *******************************************************************************/

#include <iostream>
#include <iss/iss.h>

#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <iss/arch/riscv_hart_msu_vp.h>
#include <iss/arch/rv32imac.h>
#include <iss/arch/rv32gc.h>
#include <iss/arch/rv64gc.h>
#include <iss/arch/rv64i.h>
#include <iss/llvm/jit_helper.h>
#include <iss/log_categories.h>
#include <iss/plugin/cycle_estimate.h>
#include <iss/plugin/instruction_count.h>

namespace po = boost::program_options;

int main(int argc, char *argv[]) {
    /*
     *  Define and parse the program options
     */
    po::variables_map clim;
    po::options_description desc("Options");
    // clang-format off
    desc.add_options()
        ("help,h", "Print help message")
        ("verbose,v", po::value<int>()->implicit_value(0), "Sets logging verbosity")
        ("logfile,f", po::value<std::string>(), "Sets default log file.")
        ("disass,d", po::value<std::string>()->implicit_value(""), "Enables disassembly")
        ("gdb-port,g", po::value<unsigned>()->default_value(0), "enable gdb server and specify port to use")
        ("instructions,i", po::value<uint64_t>()->default_value(std::numeric_limits<uint64_t>::max()), "max. number of instructions to simulate")
        ("reset,r", po::value<std::string>(), "reset address")
        ("dump-ir", "dump the intermediate representation")
        ("elf", po::value<std::vector<std::string>>(), "ELF file(s) to load")
        ("mem,m", po::value<std::string>(), "the memory input file")
        ("plugin,p", po::value<std::vector<std::string>>(), "plugin to activate")
        ("isa", po::value<std::string>()->default_value("rv32gc"), "isa to use for simulation");
    // clang-format on
    auto parsed = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
    try {
        po::store(parsed, clim); // can throw
        // --help option
        if (clim.count("help")) {
            std::cout << "DBT-RISE-RiscV simulator for RISC-V" << std::endl << desc << std::endl;
            return 0;
        }
        po::notify(clim); // throws on error, so do after help in case
    } catch (po::error &e) {
        // there are problems
        std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }
    std::vector<std::string> args = collect_unrecognized(parsed.options, po::include_positional);

    if (clim.count("verbose")) {
        auto l = logging::as_log_level(clim["verbose"].as<int>());
        LOGGER(DEFAULT)::reporting_level() = l;
        LOGGER(connection)::reporting_level() = l;
    }
    if (clim.count("logfile")) {
        // configure the connection logger
        auto f = fopen(clim["logfile"].as<std::string>().c_str(), "w");
        LOG_OUTPUT(DEFAULT)::stream() = f;
        LOG_OUTPUT(connection)::stream() = f;
    }

    std::vector<iss::vm_plugin *> plugin_list;
    auto res = 0;
    try {
        // application code comes here //
        iss::init_jit(argc, argv);
        bool dump = clim.count("dump-ir");
        // instantiate the simulator
        std::unique_ptr<iss::vm_if> vm{nullptr};
        std::unique_ptr<iss::arch_if> cpu{nullptr};
        std::string isa_opt(clim["isa"].as<std::string>());
        if (isa_opt=="rv64ia") {
            iss::arch::rv64i* lcpu = new iss::arch::riscv_hart_msu_vp<iss::arch::rv64i>();
            vm = iss::create(lcpu, clim["gdb-port"].as<unsigned>());
            cpu.reset(lcpu);
        } else if (isa_opt=="rv64gc") {
            iss::arch::rv64gc* lcpu = new iss::arch::riscv_hart_msu_vp<iss::arch::rv64gc>();
            vm = iss::create(lcpu, clim["gdb-port"].as<unsigned>());
            cpu.reset(lcpu);
        } else if (isa_opt=="rv32imac") {
            iss::arch::rv32imac* lcpu = new iss::arch::riscv_hart_msu_vp<iss::arch::rv32imac>();
            vm = iss::create(lcpu, clim["gdb-port"].as<unsigned>());
            cpu.reset(lcpu);
        } else if (isa_opt=="rv32gc") {
            iss::arch::rv32gc* lcpu = new iss::arch::riscv_hart_msu_vp<iss::arch::rv32gc>();
            vm = iss::create(lcpu, clim["gdb-port"].as<unsigned>());
            cpu.reset(lcpu);
        } else {
            LOG(ERROR) << "Illegal argument value for '--isa': " << clim["isa"].as<std::string>() << std::endl;
            return 127;
        }
        if (clim.count("plugin")) {
            for (std::string opt_val : clim["plugin"].as<std::vector<std::string>>()) {
                std::string plugin_name{opt_val};
                std::string filename{"cycles.txt"};
                std::size_t found = opt_val.find('=');
                if (found != std::string::npos) {
                    plugin_name = opt_val.substr(0, found);
                    filename = opt_val.substr(found + 1, opt_val.size());
                }
                if (plugin_name == "ic") {
                    auto *ic_plugin = new iss::plugin::instruction_count(filename);
                    vm->register_plugin(*ic_plugin);
                    plugin_list.push_back(ic_plugin);
                } else if (plugin_name == "ce") {
                    auto *ce_plugin = new iss::plugin::cycle_estimate(filename);
                    vm->register_plugin(*ce_plugin);
                    plugin_list.push_back(ce_plugin);
                } else {
                    LOG(ERROR) << "Unknown plugin name: " << plugin_name << ", valid names are 'ce', 'ic'" << std::endl;
                    return 127;
                }
            }
        }
        if (clim.count("disass")) {
            vm->setDisassEnabled(true);
            LOGGER(disass)::reporting_level() = logging::INFO;
            auto file_name = clim["disass"].as<std::string>();
            if (file_name.length() > 0) {
                LOG_OUTPUT(disass)::stream() = fopen(file_name.c_str(), "w");
                LOGGER(disass)::print_time() = false;
                LOGGER(disass)::print_severity() = false;
            }
        }
        uint64_t start_address = 0;
        if (clim.count("mem"))
            vm->get_arch()->load_file(clim["mem"].as<std::string>(), iss::arch::traits<iss::arch::rv32imac>::MEM);
        if (clim.count("elf"))
            for (std::string input : clim["elf"].as<std::vector<std::string>>()) {
                auto start_addr = vm->get_arch()->load_file(input);
                if (start_addr.second) start_address = start_addr.first;
            }
        for (std::string input : args) {
            auto start_addr = vm->get_arch()->load_file(input); // treat remaining arguments as elf files
            if (start_addr.second) start_address = start_addr.first;
        }
        if (clim.count("reset")) {
            auto str = clim["reset"].as<std::string>();
            start_address = str.find("0x") == 0 ? std::stoull(str.substr(2), nullptr, 16) : std::stoull(str, nullptr, 10);
        }
        vm->reset(start_address);
        auto cycles = clim["instructions"].as<uint64_t>();
        res = vm->start(cycles, dump);
    } catch (std::exception &e) {
        LOG(ERROR) << "Unhandled Exception reached the top of main: " << e.what() << ", application will now exit"
                   << std::endl;
        res = 2;
    }
    // cleanup to let plugins report of needed
    for (auto *p : plugin_list) {
        delete p;
    }
    return res;
}
