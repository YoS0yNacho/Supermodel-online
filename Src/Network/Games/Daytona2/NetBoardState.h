// Daytona-only presentation state. GPL-3.0-or-later.
#ifndef INCLUDED_DAYTONA2_NETBOARD_STATE_H
#define INCLUDED_DAYTONA2_NETBOARD_STATE_H
#include "Daytona2CpuPrediction.h"
#include "Policy.h"
#include <vector>
namespace Dayto2pe { struct NetBoardState {
	// Optional dayto2pe-only presentation copies; never sent around the ring.
	bool m_daytoPrediction = false;
	unsigned m_tickRateHz = 60;
	std::vector<Dayto2pe::Predictor> m_daytoPredictors;
	std::vector<Dayto2pe::CpuPredictor> m_cpuPredictors;
	Dayto2pe::CpuRouting m_cpuRouting;
	bool m_cpuPredictionLogged = false;
	bool m_humanPredictionLogged = false;
	bool m_cpuRoutingLogged = false;
	void SetTickRate(unsigned hz) {
		m_tickRateHz = std::clamp(hz, 5u, 60u);
		for (auto& p : m_daytoPredictors) p.SetTickRate(m_tickRateHz);
		for (auto& p : m_cpuPredictors) p.SetTickRate(m_tickRateHz);
	}
	void ResetPresentation() { m_daytoPredictors.clear(); m_cpuPredictors.clear(); m_cpuRouting={}; m_humanPredictionLogged=m_cpuPredictionLogged=m_cpuRoutingLogged=false; }
}; }
#endif
