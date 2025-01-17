/*
 * Copyright (c) 2019, RPTU Kaiserslautern-Landau
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
 * Author: Lukas Steiner
 */

#ifndef REFRESHMANAGERALLBANK_H
#define REFRESHMANAGERALLBANK_H

#include "DRAMSys/controller/refresh/RefreshManagerIF.h"
#include "DRAMSys/controller/checker/CheckerIF.h"
#include "DRAMSys/configuration/Configuration.h"
#include "DRAMSys/configuration/memspec/MemSpec.h"

#include <vector>
#include <systemc>
#include <tlm>

namespace DRAMSys
{

class BankMachine;
class PowerDownManagerIF;

class RefreshManagerAllBank final : public RefreshManagerIF
{
public:
    RefreshManagerAllBank(const Configuration& config, std::vector<BankMachine*>& bankMachinesOnRank,
                          PowerDownManagerIF& powerDownManager, Rank rank);

    CommandTuple::Type getNextCommand() override;
    void evaluate() override;
    void update(Command) override;
    sc_core::sc_time getTimeForNextTrigger() override;

private:
    enum class State {Regular, Pulledin} state = State::Regular;
    const MemSpec& memSpec;
    std::vector<BankMachine*>& bankMachinesOnRank;
    PowerDownManagerIF& powerDownManager;
    tlm::tlm_generic_payload refreshPayload;
    sc_core::sc_time timeForNextTrigger = sc_core::sc_max_time();
    Command nextCommand = Command::NOP;

    unsigned activatedBanks = 0;

    int flexibilityCounter = 0;
    const int maxPostponed;
    const int maxPulledin;

    bool sleeping = false;
    const bool refreshManagement;
    const sc_core::sc_time scMaxTime = sc_core::sc_max_time();
};

} // namespace DRAMSys

#endif // REFRESHMANAGERALLBANK_H
