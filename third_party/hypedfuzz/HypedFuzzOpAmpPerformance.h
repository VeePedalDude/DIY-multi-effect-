#pragma once
// Experimental performance macros for physical op-amp parameters.
// The nominal reference circuit still has finite open-loop gain, but omits
// bandwidth, slew and overload-release limits until explicitly configured.
#include "HypedFuzzRealtime.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hyped {
enum class AmpStage {
    InputBuffer, BoostPreamp, FullGain, ScoopedGain, EqBassBuffer, EqMain
};

inline const char* ampStageName(AmpStage s) {
    switch(s) {
        case AmpStage::InputBuffer: return "IC1.1 TL072";
        case AmpStage::BoostPreamp: return "IC1.2 TL072";
        case AmpStage::FullGain: return "IC2.1 JRC4558";
        case AmpStage::ScoopedGain: return "IC2.2 JRC4558";
        case AmpStage::EqBassBuffer: return "IC3.2 JRC4558";
        case AmpStage::EqMain: return "IC3.1 JRC4558";
    }
    return "";
}

class OpAmpPerformance {
public:
    explicit OpAmpPerformance(Realtime& fx): fx_(fx) {}

    // 0=disabled (reference). 1 means VERY slow 0.003 V/us.
    // Nonzero maps logarithmically from .10 V/us down to .003 V/us.
    // More slow-slew = stronger attack rounding and possible harmonics.
    bool slewCrush(AmpStage stage, float normalized) {
        const double v=unit(normalized);
        return fx_.setOpAmpSlewRate(ampStageName(stage),
            v==0 ? 0.0 : .1*std::pow(.003/.1,v));
    }

    // 0=disabled (reference). 1=50kHz; lower gain-bandwidth can
    // strongly alter closed-loop behavior, not simply apply a tone filter.
    bool bandwidthLimit(AmpStage stage, float normalized) {
        const double v=unit(normalized);
        return fx_.setOpAmpBandwidth(ampStageName(stage),
            v==0 ? 0.0 : 3e6*std::pow(50000./3e6,v));
    }

    // 0=original 4V about VREF; 1=0.65V about VREF. At small values
    // this is intentionally a nonphysical lo-fi headroom experiment.
    bool headroomCrush(AmpStage stage, float normalized) {
        return fx_.setOpAmpOutputSwing(ampStageName(stage),
                                      4.0*std::pow(.65/4.0,unit(normalized)));
    }

    // 0=disabled; above zero recovery 5-100ms with strong (0.9) hold.
    // Measurable primarily when the selected amplifier reaches its rail.
    // This is a synthetic recovery envelope, NOT a calibrated chip property.
    bool recovery(AmpStage stage, float normalized) {
        const double v=unit(normalized);
        return fx_.setOpAmpRecovery(ampStageName(stage),
                                     v==0 ? 0 : 5.0+95.0*v, v==0 ? 0 : .9);
    }

    // Slow seeded changes to slew rate (fraction) and input-offset (mV).
    // Slew jitter is only audible if slewRateVPerUs is nonzero. This
    // resets the seeded jitter phases, so call on control changes only.
    bool imperfection(AmpStage stage,float slewFraction,float offsetMillivolts,
                      double speedHz=3., uint32_t seed=12345) {
        return fx_.setOpAmpJitter(ampStageName(stage),unit(slewFraction)*.40,
                                 unit(offsetMillivolts)*.003,
                                 speedHz,.05,seed);
    }

private:
    static double unit(float v) {return std::isfinite(v)?std::clamp(double(v),0.,1.):0.;}
    Realtime& fx_;
};
} // namespace hyped
