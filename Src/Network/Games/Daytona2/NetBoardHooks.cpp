/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2020 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

// Daytona-specific presentation hooks; transport stays in SimNetBoard.
#include "Supermodel.h"
#include "Model3/SimNetBoard.h"
#include "TimedSegment.h"

void CSimNetBoard::ApplyDaytonaPrediction()
{
	const auto& profile = *Dayto2pe::GetProfile(m_gameInfo.name);
	// Predict remote slots after forwarding; never modify the local echo.
	for (size_t slot = 0; slot < m_gameNet.daytona.m_daytoPredictors.size(); ++slot)
	{
		const size_t offset = slot * m_segmentSize;
		const bool validTiming = slot < m_asyncLatestAgeUs.size() &&
			slot < m_asyncLatestUncertaintyUs.size() &&
			m_asyncLatestUncertaintyUs[slot] < Dayto2pe::MaxTimingUncertaintyUs;
		double sampleAgeFrames = validTiming ?
			double(m_asyncLatestAgeUs[slot]) / FrameDurationUs() : double(m_gameNet.m_asyncSnapshotAge);
		double timingUncertaintyFrames = slot < m_asyncLatestUncertaintyUs.size() ?
			double(m_asyncLatestUncertaintyUs[slot]) / FrameDurationUs() : 0.0;
		std::vector<uint8_t> udpStorage;
		const uint8_t* presentation=PresentationSource(slot,udpStorage,sampleAgeFrames,timingUncertaintyFrames);
		const bool human = m_gameNet.daytona.m_daytoPredictors[slot].Apply(presentation, m_segmentSize,
			CommRAM + 0x100 + m_segmentSize + offset, m_gameNet.m_predictionFrame,
			sampleAgeFrames, timingUncertaintyFrames, profile);
		if (human && !m_gameNet.daytona.m_humanPredictionLogged) {
			m_gameNet.daytona.m_humanPredictionLogged = true;
			InfoLog("%s human prediction active: remote slot=%u", m_gameInfo.name.c_str(), unsigned(slot));
		}
		// ROM cabinet IDs are one-based; HLE indices are zero-based.
		const unsigned owner = 1 + (m_asyncIndex[0] + m_numMachines - 1 - unsigned(slot)) % m_numMachines;
		Dayto2pe::Sample diagnostic;
		if (Dayto2pe::Decode(m_asyncLatest.data() + offset, m_segmentSize, diagnostic, profile) && owner < Dayto2pe::CpuRouting::MaxOwners)
		{
			const auto& map = m_gameNet.daytona.m_cpuRouting.owners[owner];
			const unsigned id = map.ids[0];
			// Log distinct routing snapshots so unchanged routing is distinguishable from disabled prediction.
			const uint64_t key = uint64_t(m_gameNet.daytona.m_cpuRouting.local) |
				(uint64_t(m_gameNet.daytona.m_cpuRouting.machines) << 8) |
				(uint64_t(owner) << 16) | (uint64_t(map.format) << 24) |
				(uint64_t(map.count) << 32) | (uint64_t(id) << 40);
			static uint64_t routedKey = ~uint64_t(0);
			if (key != routedKey)
			{
				routedKey = key;
				m_gameNet.daytona.m_cpuRoutingLogged = true;
				InfoLog("%s CPU routing: native=%u/%u HLE=%u/%u owner=%u format=%u count=%u firstcar=%u type=%u authority=%u",
					m_gameInfo.name.c_str(), unsigned(m_gameNet.daytona.m_cpuRouting.local), unsigned(m_gameNet.daytona.m_cpuRouting.machines), unsigned(m_asyncIndex[0]), unsigned(m_numMachines),
					owner, unsigned(map.format), unsigned(map.count), id, id < 40 ? m_gameNet.daytona.m_cpuRouting.types[id] : 255, id < 40 ? m_gameNet.daytona.m_cpuRouting.authorities[id] : 255);
			}
		}
		const bool routingMatches = m_gameNet.daytona.m_cpuRouting.local == m_asyncIndex[0] + 1 && m_gameNet.daytona.m_cpuRouting.machines == m_numMachines;
		const unsigned modified = m_gameNet.daytona.m_cpuPredictors[slot].Apply(presentation, m_segmentSize,
			CommRAM + 0x100 + m_segmentSize + offset, m_gameNet.m_predictionFrame,
			sampleAgeFrames, timingUncertaintyFrames, m_gameNet.daytona.m_cpuRouting,
			routingMatches ? owner : Dayto2pe::CpuRouting::MaxOwners, profile);
		if (modified && !m_gameNet.daytona.m_cpuPredictionLogged)
		{
			m_gameNet.daytona.m_cpuPredictionLogged = true;
			InfoLog("%s CPU prediction active: %u verified remote CPU records", m_gameInfo.name.c_str(), modified);
		}
	}
}
