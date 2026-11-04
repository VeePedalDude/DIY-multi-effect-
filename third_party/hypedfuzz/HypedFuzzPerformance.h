#pragma once
// User-facing experimental macro controls for the Hyped Fuzz circuit model.
// Controls are defined from measured SIMULATOR response, not verified analog hardware.
// Update from a control thread between audio blocks; do not call from process().
#include "HypedFuzzRealtime.h"
#include <algorithm>
#include <cmath>

namespace hyped {
class PerformanceControls {
public:
 explicit PerformanceControls(Realtime& effect): effect_(effect) {}

 // 0 = 12k, 0.5 = schematic 10k, 1 = 8k.
 // Harmonic effects are signal/mode dependent; no boost-mode effect.
 bool octaveCharacter(float normalized) {
   const double t=unit(normalized);
   return effect_.setComponentScale("R14",1.2-0.4*t);
 }
 // 0 = original Full C11 15nF, 1 = 4.95nF. In Scooped/Boost this is inactive.
 bool fullBite(float normalized) {
   return effect_.setComponentScale("C11",1.-0.67*unit(normalized));
 }
 // 0 = original C13 1nF, 1 = 5nF. Primarily Scooped mode.
 bool scoopEdge(float normalized) {
   return effect_.setComponentScale("C13",1.+4.*unit(normalized));
 }
 // 0 = original C14 4.7nF, 1 = 14.1nF. Primarily Scooped mode.
 bool scoopContour(float normalized) {
   return effect_.setComponentScale("C14",1.+2.*unit(normalized));
 }
 // 0 = beta 350 (default), 1 = beta 100. Experimental timbral variation.
 bool transistorCharacter(float normalized) {
   TransistorParameters p;p.beta=350.-250.*unit(normalized);
   return effect_.setTransistorParameters("Q2 BC549C",p);
 }
 // 0 = 9V nominal; 1 = 7.5V static rail. NOT a supply-enveloped sag model.
 void railVoltage(float normalized) {
   effect_.setSupplyVoltage(9.-1.5*unit(normalized));
 }
 // 0 = nominal D1; 1 = stronger D1 conduction and altered emission.
 // Large differences in RMS, sometimes less distinct in level-matched frequency response.
 bool diodeAsymmetry(float normalized) {
   const double t=unit(normalized);
   DiodeParameters p;
   p.saturationAmps=2.52e-9*std::pow(1e-7/2.52e-9,t);
   p.emission=1.75-0.45*t;
   return effect_.setDiodeParameters("D1 1N914",p);
 }
 // 0=no drift, 1=20% fractional resistor jitter. Set it ONCE when configuration
 // changes; repeatedly setting it re-seeds its random sequence and is NOT modulation.
 bool fuzzDrift(float normalized,double refreshHz=5.0,uint32_t seed=12345) {
   return effect_.setComponentJitter("R14",.20*unit(normalized),refreshHz,.03,seed);
 }
private:
 static double unit(float v){return std::isfinite(v)?std::clamp(double(v),0.,1.):0.;}
 Realtime& effect_;
};
} // namespace hyped
