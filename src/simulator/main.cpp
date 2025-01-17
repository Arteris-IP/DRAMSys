/*
 * Copyright (c) 2023, RPTU Kaiserslautern-Landau
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors:
 *    Derek Christ
 */

#include "simulator/Initiator.h"
#include "simulator/MemoryManager.h"
#include "simulator/SimpleInitiator.h"
#include "simulator/generator/TrafficGenerator.h"
#include "simulator/hammer/RowHammer.h"
#include "simulator/player/StlPlayer.h"
#include "simulator/util.h"

#include <DRAMSys/simulation/DRAMSysRecordable.h>

#include <systemc>
#include <tlm>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <filesystem>
#include <fstream>
#include <random>

static constexpr std::string_view TRACE_DIRECTORY = "traces";

int sc_main(int argc, char **argv)
{
    std::filesystem::path resourceDirectory = DRAMSYS_RESOURCE_DIR;
    if (argc >= 3)
    {
        resourceDirectory = argv[2];
    }

    std::filesystem::path baseConfig = resourceDirectory / "ddr4-example.json";
    if (argc >= 2)
    {
        baseConfig = argv[1];
    }

    DRAMSys::Config::Configuration configuration =
        DRAMSys::Config::from_path(baseConfig.c_str(), resourceDirectory.c_str());

    if (!configuration.tracesetup.has_value())
        SC_REPORT_FATAL("Simulator", "No traffic initiators specified");

    std::unique_ptr<DRAMSys::DRAMSys> dramSys;
    if (configuration.simconfig.DatabaseRecording.value_or(false))
    {
        dramSys = std::make_unique<DRAMSys::DRAMSysRecordable>("DRAMSys", configuration);
    }
    else
    {
        dramSys = std::make_unique<DRAMSys::DRAMSys>("DRAMSys", configuration);
    }

    bool storageEnabled = dramSys->getConfig().storeMode == DRAMSys::Configuration::StoreMode::Store;
    MemoryManager memoryManager(storageEnabled);

    std::vector<std::unique_ptr<Initiator>> initiators;

    unsigned int terminatedInitiators = 0;
    auto termianteInitiator = [&initiators, &terminatedInitiators]()
    {
        terminatedInitiators++;

        if (terminatedInitiators == initiators.size())
            sc_core::sc_stop();
    };

    uint64_t totalTransactions{};
    uint64_t transactionsFinished = 0;
    auto transactionFinished = [&totalTransactions, &transactionsFinished, &configuration]()
    {
        transactionsFinished++;

        if (configuration.simconfig.SimulationProgressBar.value_or(false))
            loadBar(transactionsFinished, totalTransactions);
    };

    for (auto const &initiator_config : configuration.tracesetup.value())
    {
        uint64_t memorySize = dramSys->getConfig().memSpec->getSimMemSizeInBytes();
        unsigned int defaultDataLength = dramSys->getConfig().memSpec->defaultBytesPerBurst;

        auto initiator = std::visit(
            [=, &memoryManager](auto &&config) -> std::unique_ptr<Initiator>
            {
                using T = std::decay_t<decltype(config)>;
                if constexpr (std::is_same_v<T, DRAMSys::Config::TrafficGenerator> ||
                              std::is_same_v<T, DRAMSys::Config::TrafficGeneratorStateMachine>)
                {
                    return std::make_unique<TrafficGenerator>(config,
                                                              memoryManager,
                                                              memorySize,
                                                              defaultDataLength,
                                                              transactionFinished,
                                                              termianteInitiator);
                }
                else if constexpr (std::is_same_v<T, DRAMSys::Config::TracePlayer>)
                {
                    std::filesystem::path tracePath =
                        resourceDirectory / TRACE_DIRECTORY / config.name;

                    StlPlayer::TraceType traceType;

                    auto extension = tracePath.extension();
                    if (extension == ".stl")
                        traceType = StlPlayer::TraceType::Absolute;
                    else if (extension == ".rstl")
                        traceType = StlPlayer::TraceType::Relative;
                    else
                    {
                        std::string report = extension.string() + " is not a valid trace format.";
                        SC_REPORT_FATAL("Simulator", report.c_str());
                    }

                    StlPlayer player(
                        tracePath.c_str(), config.clkMhz, defaultDataLength, traceType, false);

                    return std::make_unique<SimpleInitiator<StlPlayer>>(config.name.c_str(),
                                                                        memoryManager,
                                                                        std::nullopt,
                                                                        std::nullopt,
                                                                        transactionFinished,
                                                                        termianteInitiator,
                                                                        std::move(player));
                }
                else if constexpr (std::is_same_v<T, DRAMSys::Config::RowHammer>)
                {
                    RowHammer hammer(
                        config.numRequests, config.clkMhz, config.rowIncrement, defaultDataLength);

                    return std::make_unique<SimpleInitiator<RowHammer>>(config.name.c_str(),
                                                                        memoryManager,
                                                                        1,
                                                                        1,
                                                                        transactionFinished,
                                                                        termianteInitiator,
                                                                        std::move(hammer));
                }
            },
            initiator_config);

        totalTransactions += initiator->totalRequests();

        initiator->bind(dramSys->tSocket);
        initiators.push_back(std::move(initiator));
    }

    // Store the starting of the simulation in wall-clock time:
    auto start = std::chrono::high_resolution_clock::now();
    
    // Start the SystemC simulation
    sc_set_stop_mode(sc_core::SC_STOP_FINISH_DELTA);
    sc_core::sc_start();
    
    if (!sc_core::sc_end_of_simulation_invoked())
    {
        SC_REPORT_WARNING("sc_main", "Simulation stopped without explicit sc_stop()");
        sc_core::sc_stop();
    }

    auto finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = finish - start;
    std::cout << "Simulation took " + std::to_string(elapsed.count()) + " seconds." << std::endl;

    return 0;
}
