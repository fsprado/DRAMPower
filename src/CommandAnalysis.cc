/*
 * Copyright (c) 2012-2014, TU Delft
 * Copyright (c) 2012-2014, TU Eindhoven
 * Copyright (c) 2012-2014, TU Kaiserslautern
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Karthik Chandrasekar,
 *          Matthias Jung,
 *          Omar Naji,
 *          Sven Goossens,
 *          Éder F. Zulian
 *          Felipe S. Prado
 *
 */

#include <fstream>
#include <algorithm>
#include <sstream>

#include "CommandAnalysis.h"
#include "CmdScheduler.h"

using namespace Data;
using namespace std;

bool commandSorter(const MemCommand& i, const MemCommand& j)
{
  if (i.getTimeInt64() == j.getTimeInt64()) {
    return i.getType() == MemCommand::PRE && j.getType() != MemCommand::PRE;
  } else {
    return i.getTimeInt64() < j.getTimeInt64();
  }
}

CommandAnalysis::CommandAnalysis(const int64_t nbrofBanks)
{
  // Initializing all counters and variables
  clearStats(0);
  zero = 0;

  bank_state.resize(static_cast<size_t>(nbrofBanks), BANK_PRECHARGED);
  last_bank_state.resize(static_cast<size_t>(nbrofBanks), BANK_PRECHARGED);
  mem_state  = 0;

  cmd_list.clear();
  cached_cmd.clear();
  activation_cycle.resize(static_cast<size_t>(nbrofBanks), 0);
}

// function to clear counters
void CommandAnalysis::clearStats(const int64_t timestamp)
{

  numberofacts        = 0;
  numberofpres        = 0;
  numberofreads       = 0;
  numberofwrites      = 0;
  numberofrefs        = 0;
  f_act_pdns          = 0;
  s_act_pdns          = 0;
  f_pre_pdns          = 0;
  s_pre_pdns          = 0;
  numberofsrefs       = 0;

  actcycles           = 0;
  precycles           = 0;
  f_act_pdcycles      = 0;
  s_act_pdcycles      = 0;
  f_pre_pdcycles      = 0;
  s_pre_pdcycles      = 0;
  pup_act_cycles      = 0;
  pup_pre_cycles      = 0;
  sref_cycles_idd6    = 0;
  spup_cycles         = 0;
  sref_ref_act_cycles = 0;
  sref_ref_pre_cycles = 0;
  spup_ref_act_cycles = 0;
  spup_ref_pre_cycles = 0;
  idlecycles_act      = 0;
  idlecycles_pre      = 0;

  // reset count references to timestamp so that they are moved
  // to start of next stats generation
  first_act_cycle     = timestamp;

  pdn_cycle           = timestamp;
  sref_cycle_window   = timestamp;

  end_act_op          = timestamp;
  end_read_op         = timestamp;
  end_write_op        = timestamp;

  latest_read_cycle   = -1;
  latest_write_cycle  = -1;

  if (timestamp == 0) {
    latest_pre_cycle = -1;
    latest_act_cycle = -1;
    sref_cycle = 0;
    last_pre_cycle = 0;
    sref_ref_act_cycles_window = 0;
    sref_ref_pre_cycles_window = 0;
  } else {
    last_pre_cycle = max(timestamp,last_pre_cycle);

    latest_pre_cycle = max(timestamp, latest_pre_cycle);

    if (latest_act_cycle < timestamp)
        latest_act_cycle = -1;
  }
}

// function to clear all arrays
void CommandAnalysis::clear()
{
  cached_cmd.clear();
  cmd_list.clear();
  last_bank_state.clear();
  bank_state.clear();
}

// Reads through the trace file, identifies the timestamp, command and bank
// If the issued command includes an auto-precharge, adds an explicit
// precharge to a cached command list and computes the precharge offset from the
// issued command timestamp, when the auto-precharge would kick in

void CommandAnalysis::getCommands(const Data::MemorySpecification& memSpec,
                                  std::vector<MemCommand>& list, bool lastupdate, int64_t timestamp)
{
  if (!next_window_cmd_list.empty()) {
    list.insert(list.begin(), next_window_cmd_list.begin(), next_window_cmd_list.end());
    next_window_cmd_list.clear();
  }
  for (size_t i = 0; i < list.size(); ++i) {
    MemCommand& cmd = list[i];
    MemCommand::cmds cmdType = cmd.getType();
    if (cmdType == MemCommand::ACT) {
      activation_cycle[cmd.getBank()] = cmd.getTimeInt64();
    } else if (cmdType == MemCommand::RDA || cmdType == MemCommand::WRA) {
      // Remove auto-precharge flag from command
      cmd.setType(cmd.typeWithoutAutoPrechargeFlag());

      // Add the auto precharge to the list of cached_cmds
      int64_t preTime = max(cmd.getTimeInt64() + cmd.getPrechargeOffset(memSpec, cmdType),
                           activation_cycle[cmd.getBank()] + memSpec.memTimingSpec.RAS);
      list.push_back(MemCommand(MemCommand::PRE, cmd.getBank(), preTime));
    }

    if (!lastupdate && timestamp > 0) {
      if(cmd.getTimeInt64() > timestamp)
      {
          MemCommand nextWindowCmd = list[i];
          next_window_cmd_list.push_back(nextWindowCmd);
          list.erase(find(list.begin(), list.end(), cmd));
      }
    }
  }
  sort(list.begin(), list.end(), commandSorter);

  if (lastupdate && list.empty() == false) {
    // Add cycles at the end of the list
    int64_t t = timeToCompletion(memSpec, list.back().getType()) + list.back().getTimeInt64() - 1;
    list.push_back(MemCommand(MemCommand::NOP, 0, t));
  }  

  evaluate(memSpec, list);
} // CommandAnalysis::getCommands


// To get the time of completion of the issued command
// Derived based on JEDEC specifications

int64_t CommandAnalysis::timeToCompletion(const MemorySpecification&
                                      memSpec, MemCommand::cmds type)
{
  int64_t offset = 0;
  const MemTimingSpec& memTimingSpec     = memSpec.memTimingSpec;
  const MemArchitectureSpec& memArchSpec = memSpec.memArchSpec;

  if (type == MemCommand::RD) {
    offset = memTimingSpec.RL +
                              memTimingSpec.DQSCK + 1 + (memArchSpec.burstLength /
                                                         memArchSpec.dataRate);
  } else if (type == MemCommand::WR) {
    offset = memTimingSpec.WL +
                              (memArchSpec.burstLength / memArchSpec.dataRate) +
                              memTimingSpec.WR;
  } else if (type == MemCommand::ACT) {
    offset = memTimingSpec.RCD;
  } else if ((type == MemCommand::PRE) || (type == MemCommand::PREA)) {
    offset = memTimingSpec.RP;
  }
  return offset;
} // CommandAnalysis::timeToCompletion

// Used to analyse a given list of commands and identify command timings
// and memory state transitions
void CommandAnalysis::evaluate(const MemorySpecification& memSpec,
                               vector<MemCommand>& cmd_list)
{
  // for each command identify timestamp, type and bank
  for (auto cmd : cmd_list) {
    // For command type
    int type = cmd.getType();
    // For command bank
    unsigned bank = cmd.getBank();
    // Command Issue timestamp in clock cycles (cc)
    int64_t timestamp = cmd.getTimeInt64();
    // Get current number of active banks (according to bank_state[])
    unsigned num_active_banks = get_num_active_banks();

    if (type == MemCommand::ACT) {
      printWarningIfPoweredDown("Command issued while in power-down mode.", type, timestamp, bank);
      // If command is ACT - update number of acts, bank state of the
      // target bank, first and latest activation cycle and the memory
      // state. Update the number of precharged/idle-precharged cycles.
      // If the bank is already active ignore the command and generate a
      // warning.
      if (bank_state[bank] == BANK_PRECHARGED) {
        numberofacts++;

        if (num_active_banks == 0) {
          // Here a memory state transition to ACT is happening. Save the
          // number of cycles in precharge state (increment the counter).
          first_act_cycle = timestamp;
          precycles += max(zero, timestamp - last_pre_cycle);
          idle_pre_update(memSpec, timestamp, latest_pre_cycle);
        }

        bank_state[bank] = BANK_ACTIVE;
        latest_act_cycle = timestamp;
      } else {
        printWarning("Bank is already active!", type, timestamp, bank);
      }
    } else if (type == MemCommand::RD) {
      printWarningIfPoweredDown("Command issued while in power-down mode.", type, timestamp, bank);
      // If command is RD - update number of reads and read cycle. Check
      // for active idle cycles (if any).
      if (bank_state[bank] == BANK_PRECHARGED) {
        printWarning("Bank is not active!", type, timestamp, bank);
      }
      numberofreads++;
      idle_act_update(memSpec, latest_read_cycle, latest_write_cycle,
                      latest_act_cycle, timestamp);
      latest_read_cycle = timestamp;
    } else if (type == MemCommand::WR) {
      printWarningIfPoweredDown("Command issued while in power-down mode.", type, timestamp, bank);
      // If command is WR - update number of writes and write cycle. Check
      // for active idle cycles (if any).
      if (bank_state[bank] == BANK_PRECHARGED) {
        printWarning("Bank is not active!", type, timestamp, bank);
      }
      numberofwrites++;
      idle_act_update(memSpec, latest_read_cycle, latest_write_cycle,
                      latest_act_cycle, timestamp);
      latest_write_cycle = timestamp;
    } else if (type == MemCommand::REF) {
      printWarningIfPoweredDown("Command issued while in power-down mode.", type, timestamp, bank);
      // If command is REF - update number of refreshes, set bank state of
      // all banks to ACT, set the last PRE cycles at RFC-RP cycles from
      // timestamp, set the number of active cycles to RFC-RP and check
      // for active and precharged cycles and idle active and idle
      // precharged cycles before refresh. Change memory state to 0.
      printWarningIfActive("One or more banks are active! REF requires all banks to be precharged.", type, timestamp, bank);
      numberofrefs++;
      idle_pre_update(memSpec, timestamp, latest_pre_cycle);
      first_act_cycle  = timestamp;
      precycles       += max(zero, timestamp - last_pre_cycle);
      last_pre_cycle   = timestamp + memSpec.memTimingSpec.RFC -
                         memSpec.memTimingSpec.RP;
      latest_pre_cycle = last_pre_cycle;
      actcycles       += memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP;
      for (auto& bs : bank_state) {
        bs = BANK_PRECHARGED;
      }
    } else if (type == MemCommand::PRE) {
      printWarningIfPoweredDown("Command issued while in power-down mode.", type, timestamp, bank);
      // If command is explicit PRE - update number of precharges, bank
      // state of the target bank and last and latest precharge cycle.
      // Calculate the number of active cycles if the memory was in the
      // active state before, but there is a state transition to PRE now
      // (i.e., this is the last active bank).
      // If the bank is already precharged ignore the command and generate a
      // warning.

      // Precharge only if the target bank is active
      if (bank_state[bank] == BANK_ACTIVE) {
        numberofpres++;
        // Since we got here, at least one bank is active
        assert(num_active_banks != 0);

        if (num_active_banks == 1) {
          // This is the last active bank. Therefore, here a memory state
          // transition to PRE is happening. Let's increment the active cycle
          // counter.
          actcycles += max(zero, timestamp - first_act_cycle);
          last_pre_cycle = timestamp;
          idle_act_update(memSpec, latest_read_cycle, latest_write_cycle,
                          latest_act_cycle, timestamp);
        }

        bank_state[bank] = BANK_PRECHARGED;
        latest_pre_cycle = timestamp;
      } else {
        printWarning("Bank is already precharged!", type, timestamp, bank);
      }
    } else if (type == MemCommand::PREA) {
      printWarningIfPoweredDown("Command issued while in power-down mode.", type, timestamp, bank);
      // If command is explicit PREA (precharge all banks) - update
      // number of precharges by the number of active banks, update the bank
      // state of all banks to PRE and set the precharge cycle (the cycle in
      // which the memory state changes from ACT to PRE, aka last_pre_cycle).
      // Calculate the number of active cycles if the memory was in the
      // active state before, but there is a state transition to PRE now.
      
      if (num_active_banks > 0) {
        // Active banks are being precharged
        numberofpres += num_active_banks;
        // At least one bank was active, therefore the current memory state is
        // ACT. Since all banks are being precharged a memory state transition
        // to PRE is happening. Add to the counter the amount of cycles the
        // memory remained in the ACT state.
	
        actcycles += max(zero, timestamp - first_act_cycle);
        last_pre_cycle = timestamp;
        idle_act_update(memSpec, latest_read_cycle, latest_write_cycle,
                        latest_act_cycle, timestamp);

        latest_pre_cycle = timestamp;
        // Reset the state for all banks to precharged.
        for (auto& bs : bank_state) {
          bs = BANK_PRECHARGED;
        }
      } else {
        printWarning("All banks are already precharged!", type, timestamp, bank);
      }
    } else if (type == MemCommand::PDN_F_ACT) {
      // If command is fast-exit active power-down - update number of
      // power-downs, set the power-down cycle and the memory mode to
      // fast-exit active power-down. Save states of all the banks from
      // the cycle before entering active power-down, to be returned to
      // after powering-up. Update active and active idle cycles.
      printWarningIfNotActive("All banks are precharged! Incorrect use of Active Power-Down.", type, timestamp, bank);
      f_act_pdns++;
      last_bank_state = bank_state;
      pdn_cycle  = timestamp;
      actcycles += max(zero, timestamp - first_act_cycle);
      idle_act_update(memSpec, latest_read_cycle, latest_write_cycle,
                      latest_act_cycle, timestamp);
      mem_state  = CommandAnalysis::MS_PDN_F_ACT;
    } else if (type == MemCommand::PDN_S_ACT) {
      // If command is slow-exit active power-down - update number of
      // power-downs, set the power-down cycle and the memory mode to
      // slow-exit active power-down. Save states of all the banks from
      // the cycle before entering active power-down, to be returned to
      // after powering-up. Update active and active idle cycles.
      printWarningIfNotActive("All banks are precharged! Incorrect use of Active Power-Down.", type, timestamp, bank);
      s_act_pdns++;
      last_bank_state = bank_state;
      pdn_cycle  = timestamp;
      actcycles += max(zero, timestamp - first_act_cycle);
      idle_act_update(memSpec, latest_read_cycle, latest_write_cycle,
                      latest_act_cycle, timestamp);
      mem_state  = CommandAnalysis::MS_PDN_S_ACT;
    } else if (type == MemCommand::PDN_F_PRE) {
      // If command is fast-exit precharged power-down - update number of
      // power-downs, set the power-down cycle and the memory mode to
      // fast-exit precahrged power-down. Update precharged and precharged
      // idle cycles.
      printWarningIfActive("One or more banks are active! Incorrect use of Precharged Power-Down.", type, timestamp, bank);
      f_pre_pdns++;
      pdn_cycle  = timestamp;
      precycles += max(zero, timestamp - last_pre_cycle);
      idle_pre_update(memSpec, timestamp, latest_pre_cycle);
      mem_state  = CommandAnalysis::MS_PDN_F_PRE;
    } else if (type == MemCommand::PDN_S_PRE) {
      // If command is slow-exit precharged power-down - update number of
      // power-downs, set the power-down cycle and the memory mode to
      // slow-exit precahrged power-down. Update precharged and precharged
      // idle cycles.
      printWarningIfActive("One or more banks are active! Incorrect use of Precharged Power-Down.", type, timestamp, bank);
      s_pre_pdns++;
      pdn_cycle  = timestamp;
      precycles += max(zero, timestamp - last_pre_cycle);
      idle_pre_update(memSpec, timestamp, latest_pre_cycle);
      mem_state  = CommandAnalysis::MS_PDN_S_PRE;
    } else if (type == MemCommand::PUP_ACT) {
      // If command is power-up in the active mode - check the power-down
      // exit-mode employed (fast or slow), update the number of power-down
      // and power-up cycles and the latest and first act cycle. Also, reset
      // all the individual bank states to the respective saved states
      // before entering power-down.
      if (mem_state == CommandAnalysis::MS_PDN_F_ACT) {
        f_act_pdcycles  += max(zero, timestamp - pdn_cycle);
        pup_act_cycles  += memSpec.memTimingSpec.XP;
        latest_act_cycle = max(timestamp, timestamp +
                               memSpec.memTimingSpec.XP - memSpec.memTimingSpec.RCD);
      } else if (mem_state == CommandAnalysis::MS_PDN_S_ACT) {
        s_act_pdcycles += max(zero, timestamp - pdn_cycle);
        if (memSpec.memArchSpec.dll == false) {
          pup_act_cycles  += memSpec.memTimingSpec.XP;
          latest_act_cycle = max(timestamp, timestamp +
                                 memSpec.memTimingSpec.XP - memSpec.memTimingSpec.RCD);
        } else {
          pup_act_cycles  += memSpec.memTimingSpec.XPDLL -
                             memSpec.memTimingSpec.RCD;
          latest_act_cycle = max(timestamp, timestamp +
                                 memSpec.memTimingSpec.XPDLL -
                                 (2 * memSpec.memTimingSpec.RCD));
        }
      } else if (mem_state != CommandAnalysis::MS_PDN_S_ACT || mem_state != CommandAnalysis::MS_PDN_F_ACT) {
        cerr << "Incorrect use of Active Power-Up!" << endl;
      }
      mem_state = 0;
      bank_state = last_bank_state;
      first_act_cycle = timestamp;
    } else if (type == MemCommand::PUP_PRE) {
      // If command is power-up in the precharged mode - check the power-down
      // exit-mode employed (fast or slow), update the number of power-down
      // and power-up cycles and the latest and last pre cycle.
      if (mem_state == CommandAnalysis::MS_PDN_F_PRE) {
        f_pre_pdcycles  += max(zero, timestamp - pdn_cycle);
        pup_pre_cycles  += memSpec.memTimingSpec.XP;
        latest_pre_cycle = max(timestamp, timestamp +
                               memSpec.memTimingSpec.XP - memSpec.memTimingSpec.RP);
      } else if (mem_state == CommandAnalysis::MS_PDN_S_PRE) {
        s_pre_pdcycles += max(zero, timestamp - pdn_cycle);
        if (memSpec.memArchSpec.dll == false) {
          pup_pre_cycles  += memSpec.memTimingSpec.XP;
          latest_pre_cycle = max(timestamp, timestamp +
                                 memSpec.memTimingSpec.XP - memSpec.memTimingSpec.RP);
        } else {
          pup_pre_cycles  += memSpec.memTimingSpec.XPDLL -
                             memSpec.memTimingSpec.RCD;
          latest_pre_cycle = max(timestamp, timestamp +
                                 memSpec.memTimingSpec.XPDLL - memSpec.memTimingSpec.RCD -
                                 memSpec.memTimingSpec.RP);
        }
      } else if (mem_state != CommandAnalysis::MS_PDN_S_PRE || mem_state != CommandAnalysis::MS_PDN_F_PRE) {
        cerr << "Incorrect use of Precharged Power-Up!" << endl;
      }
      mem_state      = 0;
      last_pre_cycle = timestamp;
    } else if (type == MemCommand::SREN) {
      // If command is self-refresh - update number of self-refreshes,
      // set memory state to SREF, update precharge and idle precharge
      // cycles and set the self-refresh cycle.
      printWarningIfActive("One or more banks are active! SREF requires all banks to be precharged.", type, timestamp, bank);
      numberofsrefs++;
      sref_cycle = timestamp;
      sref_cycle_window = timestamp;
      sref_ref_pre_cycles_window = 0;
      sref_ref_act_cycles_window = 0;      
      precycles += max(zero, timestamp - last_pre_cycle);
      idle_pre_update(memSpec, timestamp, latest_pre_cycle);
      mem_state  = CommandAnalysis::MS_SREF;
    } else if (type == MemCommand::SREX) {
      // If command is self-refresh exit - update the number of self-refresh
      // clock cycles, number of active and precharged auto-refresh clock
      // cycles during self-refresh and self-refresh exit based on the number
      // of cycles in the self-refresh mode and auto-refresh duration (RFC).
      // Set the last and latest precharge cycle accordingly and set the
      // memory state to 0.
      if (mem_state != CommandAnalysis::MS_SREF) {
        cerr << "Incorrect use of Self-Refresh Power-Up!" << endl;
      }
      // The total duration of self-refresh is given by the difference between
      // the current clock cycle and the clock cycle of entering self-refresh.
      int64_t sref_duration = timestamp - sref_cycle;

      // Negative or zero duration should never happen.
      if (sref_duration <= 0) {
        printWarning("Invalid Self-Refresh duration!", type, timestamp, bank);
        sref_duration = 0;
      }

      // The minimum time that the DRAM must remain in Self-Refresh is CKESR.
      if (sref_duration < memSpec.memTimingSpec.CKESR) {
        printWarning("Self-Refresh duration < CKESR!", type, timestamp, bank);
      }

      if (sref_duration >= memSpec.memTimingSpec.RFC) {
        /*
         * Self-refresh Exit Context 1 (tSREF >= tRFC):
         * The memory remained in self-refresh for a certain number of clock
         * cycles greater than a refresh cycle time (RFC). Consequently, the
         * initial auto-refresh accomplished.
         *
         *
         *  SREN                                #              SREX
         *  |                                   #                ^
         *  |                                   #                |
         *  |<------------------------- tSREF ----------...----->|
         *  |                                   #                |
         *  |      Initial Auto-Refresh         #                |
         *  v                                   #                |
         *  ------------------------------------#-------...-----------------> t
         *                                      #
         *   <------------- tRFC -------------->#
         *   <---- (tRFC - tRP) ----><-- tRP -->#
         *               |                |
         *               v                v
         *     sref_ref_act_cycles     sref_ref_pre_cycles
         *
         *
         * Summary:
         * sref_cycles_idd6 += tSREF – tRFC
         * sref_ref_act_cycles += tRFC - tRP
         * sref_ref_pre_cycles += tRP
         * spup_ref_act_cycles += 0
         * spup_ref_pre_cycles += 0
         *
         */

        // The initial auto-refresh consumes (IDD5 − IDD3N) over one refresh
        // period (RFC) from the start of the self-refresh.
        sref_ref_act_cycles += memSpec.memTimingSpec.RFC -
                               memSpec.memTimingSpec.RP - sref_ref_act_cycles_window;
        sref_ref_pre_cycles += memSpec.memTimingSpec.RP - sref_ref_pre_cycles_window;
        last_pre_cycle       = timestamp;

        // The IDD6 current is consumed for the time period spent in the
        // self-refresh mode, which excludes the time spent in finishing the
        // initial auto-refresh.
        if (sref_cycle_window > sref_cycle + memSpec.memTimingSpec.RFC) {
            sref_cycles_idd6         += max(zero, timestamp - sref_cycle_window);
        } else {
            sref_cycles_idd6         += max(zero, timestamp - sref_cycle
                                   - memSpec.memTimingSpec.RFC);
        }

        // IDD2N current is consumed when exiting the self-refresh state.
        if (memSpec.memArchSpec.dll == false) {
          spup_cycles     += memSpec.memTimingSpec.XS;
          latest_pre_cycle = max(timestamp, timestamp +
                                 memSpec.memTimingSpec.XS - memSpec.memTimingSpec.RP);
        } else {
          spup_cycles     += memSpec.memTimingSpec.XSDLL -
                             memSpec.memTimingSpec.RCD;
          latest_pre_cycle = max(timestamp, timestamp +
                                 memSpec.memTimingSpec.XSDLL - memSpec.memTimingSpec.RCD
                                 - memSpec.memTimingSpec.RP);
        }

      } else {
        // Self-refresh Exit Context 2 (tSREF < tRFC):
        // Exit self-refresh before the completion of the initial
        // auto-refresh.

        // Number of active cycles needed by an auto-refresh.
        int64_t ref_act_cycles = memSpec.memTimingSpec.RFC -
                                 memSpec.memTimingSpec.RP;

        if (sref_duration >= ref_act_cycles) {
          /*
           * Self-refresh Exit Context 2A (tSREF < tRFC && tSREF >= tRFC - tRP):
           * The duration of self-refresh is equal or greater than the number
           * of active cycles needed by the initial auto-refresh.
           *
           *
           *  SREN                                           SREX
           *  |                                                ^         #
           *  |                                                |         #
           *  |<------------------ tSREF --------------------->|         #
           *  |                                                |         #
           *  |                                  Initial Auto-Refresh    #
           *  v                                                |         #
           *  -----------------------------------------------------------#--> t
           *                                                             #
           *   <------------------------ tRFC -------------------------->#
           *   <------------- (tRFC - tRP)--------------><----- tRP ---->#
           *           |                                 <-----><------->
           *           v                                  |         |
           *     sref_ref_act_cycles                      v         v
           *                             sref_ref_pre_cycles spup_ref_pre_cycles
           *
           *
           * Summary:
           * sref_cycles_idd6 += 0
           * sref_ref_act_cycles += tRFC - tRP
           * sref_ref_pre_cycles += tSREF – (tRFC – tRP)
           * spup_ref_act_cycles += 0
           * spup_ref_pre_cycles += tRP – sref_ref_pre_cycles
           *
           */

          // Number of precharged cycles (zero <= pre_cycles < RP)
          int64_t pre_cycles = sref_duration - ref_act_cycles - sref_ref_pre_cycles_window;

          sref_ref_act_cycles += ref_act_cycles - sref_ref_act_cycles_window;
          sref_ref_pre_cycles += pre_cycles;

          // Number of precharged cycles during the self-refresh power-up. It
          // is at maximum tRP (if pre_cycles is zero).
          int64_t spup_pre = memSpec.memTimingSpec.RP - pre_cycles;

          spup_ref_pre_cycles += spup_pre;

          last_pre_cycle       = timestamp + spup_pre;

          if (memSpec.memArchSpec.dll == false) {
            spup_cycles     += memSpec.memTimingSpec.XS - spup_pre;
            latest_pre_cycle = max(timestamp, timestamp +
                                   memSpec.memTimingSpec.XS - spup_pre -
                                   memSpec.memTimingSpec.RP);
          } else {
            spup_cycles     += memSpec.memTimingSpec.XSDLL -
                               memSpec.memTimingSpec.RCD - spup_pre;
            latest_pre_cycle = max(timestamp, timestamp +
                                   memSpec.memTimingSpec.XSDLL - memSpec.memTimingSpec.RCD -
                                   spup_pre - memSpec.memTimingSpec.RP);
          }
        } else {
          /*
           * Self-refresh Exit Context 2B (tSREF < tRFC - tRP):
           * self-refresh duration is shorter than the number of active cycles
           * needed by the initial auto-refresh.
           *
           *
           *  SREN                             SREX
           *  |                                  ^                        #
           *  |                                  |                        #
           *  |<-------------- tSREF ----------->|                        #
           *  |                                  |                        #
           *  |                       Initial Auto-Refresh                #
           *  v                                  |                        #
           *  ------------------------------------------------------------#--> t
           *                                                              #
           *   <------------------------ tRFC --------------------------->#
           *   <-------------- (tRFC - tRP)-------------><------ tRP ---->#
           *   <--------------------------------><------><--------------->
           *               |                        |             |
           *               v                        v             v
           *     sref_ref_act_cycles    spup_ref_act_cycles spup_ref_pre_cycles
           *
           *
           * Summary:
           * sref_cycles_idd6 += 0
           * sref_ref_act_cycles += tSREF
           * sref_ref_pre_cycles += 0
           * spup_ref_act_cycles += (tRFC – tRP) - tSREF
           * spup_ref_pre_cycles += tRP
           *
           */

          sref_ref_act_cycles += sref_duration - sref_ref_act_cycles_window;

          int64_t spup_act = (memSpec.memTimingSpec.RFC -
                              memSpec.memTimingSpec.RP) - sref_duration;

          spup_ref_act_cycles += spup_act;
          spup_ref_pre_cycles += memSpec.memTimingSpec.RP;

          last_pre_cycle       = timestamp + spup_act + memSpec.memTimingSpec.RP;
          if (memSpec.memArchSpec.dll == false) {
            spup_cycles     += memSpec.memTimingSpec.XS - spup_act -
                               memSpec.memTimingSpec.RP;
            latest_pre_cycle = max(timestamp, timestamp +
                                   memSpec.memTimingSpec.XS - spup_act -
                                   (2 * memSpec.memTimingSpec.RP));
          } else {
            spup_cycles     += memSpec.memTimingSpec.XSDLL -
                               memSpec.memTimingSpec.RCD - spup_act -
                               memSpec.memTimingSpec.RP;
            latest_pre_cycle = max(timestamp, timestamp +
                                   memSpec.memTimingSpec.XSDLL - memSpec.memTimingSpec.RCD -
                                   spup_act - (2 * memSpec.memTimingSpec.RP));
          }
        }
      }
      mem_state = 0;
    } else if (type == MemCommand::END || type == MemCommand::NOP) {
      // May be optionally used at the end of memory trace for better accuracy
      // Update all counters based on completion of operations.
      if (num_active_banks > 0 && mem_state == 0) {
        actcycles += max(zero, timestamp - first_act_cycle);
        idle_act_update(memSpec, latest_read_cycle, latest_write_cycle,
                        latest_act_cycle, timestamp);
      } else if (num_active_banks == 0 && mem_state == 0) {
        precycles += max(zero, timestamp - last_pre_cycle);
        idle_pre_update(memSpec, timestamp, latest_pre_cycle);
      } else if (mem_state == CommandAnalysis::MS_PDN_F_ACT) {
        f_act_pdcycles += max(zero, timestamp - pdn_cycle);
      } else if (mem_state == CommandAnalysis::MS_PDN_S_ACT) {
        s_act_pdcycles += max(zero, timestamp - pdn_cycle);
      } else if (mem_state == CommandAnalysis::MS_PDN_F_PRE) {
        f_pre_pdcycles += max(zero, timestamp - pdn_cycle);
      } else if (mem_state == CommandAnalysis::MS_PDN_S_PRE) {
        s_pre_pdcycles += max(zero, timestamp - pdn_cycle);
      } else if (mem_state == CommandAnalysis::MS_SREF) {
        if (timestamp > sref_cycle + memSpec.memTimingSpec.RFC) {
            if (sref_cycle_window <= sref_cycle + (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP)) {
                sref_ref_act_cycles += (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP) - sref_ref_act_cycles_window;
                sref_ref_act_cycles_window = (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP);
                sref_cycle_window = sref_cycle + (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP);
            }
            if (sref_cycle_window <= sref_cycle + memSpec.memTimingSpec.RFC) {
                sref_ref_pre_cycles += memSpec.memTimingSpec.RP - sref_ref_pre_cycles_window;
                sref_ref_pre_cycles_window = memSpec.memTimingSpec.RP;
                sref_cycle_window = sref_cycle + memSpec.memTimingSpec.RFC;
            }
            sref_cycles_idd6 += max(zero, timestamp - sref_cycle_window);
        } else if (timestamp > sref_cycle + (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP)) {
            if(sref_cycle_window <= sref_cycle + (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP)) {
                sref_ref_act_cycles += (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP) - sref_ref_act_cycles_window;
                sref_ref_act_cycles_window = (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP);
                sref_cycle_window = sref_cycle + (memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP);
            }

            sref_ref_pre_cycles_window += timestamp - sref_cycle_window;
            sref_ref_pre_cycles += timestamp - sref_cycle_window;
        } else {
            sref_ref_act_cycles_window += timestamp - sref_cycle_window;
            sref_ref_act_cycles += timestamp - sref_cycle_window;
        }
      }
    } else {
      printWarning("Unknown command given, exiting.", type, timestamp, bank);
      exit(-1);
    }
  }
} // CommandAnalysis::evaluate

// To update idle period information whenever active cycles may be idle
void CommandAnalysis::idle_act_update(const MemorySpecification& memSpec,
                                      int64_t latest_read_cycle, int64_t latest_write_cycle,
                                      int64_t latest_act_cycle, int64_t timestamp)
{
  if (latest_read_cycle >= 0) {
    end_read_op = latest_read_cycle + timeToCompletion(memSpec,
                                                       MemCommand::RD) - 1;
  }

  if (latest_write_cycle >= 0) {
    end_write_op = latest_write_cycle + timeToCompletion(memSpec,
                                                         MemCommand::WR) - 1;
  }

  if (latest_act_cycle >= 0) {
    end_act_op = latest_act_cycle + timeToCompletion(memSpec,
                                                     MemCommand::ACT) - 1;
  }

  idlecycles_act += max(zero, timestamp - max(max(end_read_op, end_write_op),
                                              end_act_op));
} // CommandAnalysis::idle_act_update

// To update idle period information whenever precharged cycles may be idle
void CommandAnalysis::idle_pre_update(const MemorySpecification& memSpec,
                                      int64_t timestamp, int64_t latest_pre_cycle)
{
  if (latest_pre_cycle > 0) {
    idlecycles_pre += max(zero, timestamp - latest_pre_cycle -
                          memSpec.memTimingSpec.RP);
  } else if (latest_pre_cycle == 0) {
    idlecycles_pre += max(zero, timestamp - latest_pre_cycle);
  }
}

// Returns the number of active banks based on the bank_state vector.
unsigned CommandAnalysis::get_num_active_banks(void)
{
  return (unsigned)std::count(bank_state.begin(), bank_state.end(), BANK_ACTIVE);
}

void CommandAnalysis::printWarningIfActive(const string& warning, int type, int64_t timestamp, unsigned bank)
{
  if (get_num_active_banks() != 0) {
    printWarning(warning, type, timestamp, bank);
  }
}

void CommandAnalysis::printWarningIfNotActive(const string& warning, int type, int64_t timestamp, unsigned bank)
{
  if (get_num_active_banks() == 0) {
    printWarning(warning, type, timestamp, bank);
  }
}

void CommandAnalysis::printWarningIfPoweredDown(const string& warning, int type, int64_t timestamp, unsigned bank)
{
  if (mem_state != 0) {
    printWarning(warning, type, timestamp, bank);
  }
}

void CommandAnalysis::printWarning(const string& warning, int type, int64_t timestamp, unsigned bank)
{
  cerr << "WARNING: " << warning << endl;
  cerr << "Command: " << type << ", Timestamp: " << timestamp <<
    ", Bank: " << bank << endl;
}
