#pragma once
// PedalPCB Hyped Fuzz: component-netlist reference simulator.
// Standalone, allocation-free in the audio loop; C++17; MIT license applies
// to this newly written simulator, not to the referenced schematic.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyped {

// All controls normalized [0,1]. Mode follows the three schematic switch routes.
enum class Mode { Full, Scooped, Boost };
// The partitioned model uses exactly the same component topology at each
// op-amp output, with loads isolated by the zero-output-impedance macromodel.
enum class Segment { Complete, Front, Fuzz, Output };
// Reduced solves eliminate nodes belonging only to the linear RC network.
// Dense remains available for testing or as a numerical fallback.
enum class LinearSolver { Dense, Reduced, Auto, SparseReduced, AffineReduced, SparseAffineReduced, CompactNewton };
struct Controls {
    float gain = 0.50f;
    float bass = 0.50f;
    float treble = 0.50f;
    float level = 0.50f;
    Mode mode = Mode::Full;
};

// Actual component parameters are exposed as engineering units, not normalized knobs.
// The op-amp macromodel remains a first-order, rail-limited approximation.
struct OpAmpParameters {
    double openLoopGain = 5000.0;
    double gainBandwidthHz = 0.0; // zero disables bandwidth limitation (legacy reference)
    double slewRateVPerUs = 0.0;   // zero disables slew limiting (legacy reference)
    double outputSwingVolts = 4.0; // positive and negative about VREF
    // Optional macromodel behavior: accumulated rail overdrive slows the
    // amplifier's release (experimental, not a measured TL072/4558 fit).
    double overloadRecoveryMs = 0.0; // zero=disabled
    double overloadRecoveryDepth = 0.0; // 0..1; max reduces release speed ~10x
    double offsetVolts = 0.0;
    double inputBiasAmps = 0.0;    // per input into reference rail
    double inputOffsetAmps = 0.0;  // imbalance added at noninverting input
    static OpAmpParameters TL072() {
        OpAmpParameters p; p.slewRateVPerUs=13.0; p.gainBandwidthHz=3e6; return p;
    }
    static OpAmpParameters JRC4558() {
        OpAmpParameters p; p.slewRateVPerUs=1.0; p.gainBandwidthHz=3e6; return p;
    }
};
struct TransistorParameters {
    double beta=350.0, saturationAmps=1e-14, reverseAlpha=0.5;
};
struct DiodeParameters {
    double saturationAmps=2.52e-9, emission=1.75;
};
// A seeded, smoothed random sample-and-hold signal with values in [-1,+1].
// This is a sonic experiment, not evidence of actual physical component noise.
struct Jitter {
    double depth=0.0, refreshHz=5.0, smoothingSeconds=0.02;
    double phase=1.0, current=0.0, target=0.0, coefficient=1.0;
    uint32_t seed=0x1234567u, randomState=0x1234567u;
    void configure(double d,double hz,double seconds, uint32_t s, double fs) {
        depth=std::clamp(d,0.0,0.95);
        refreshHz=std::clamp(hz,0.0,fs*0.49);
        smoothingSeconds=std::clamp(seconds,0.0,10.0);
        seed = s?s:0x1234567u;
        coefficient=smoothingSeconds>0.0 ? -std::expm1(-1.0/(fs*smoothingSeconds)) : 1.0;
        reset();
    }
    void reset() {randomState=seed;phase=1.0;current=target=0.0;}
    uint32_t random32() {
        auto x=randomState; x^=x<<13; x^=x>>17; x^=x<<5;
        return randomState=x;
    }
    void advance(double fs) {
        if(depth==0) return;
        if(phase>=1.0) {
            target=2.0*(double(random32())/4294967295.0)-1.0;
            phase-=1.0;
        }
        phase+=refreshHz/fs;
        current+=coefficient*(target-current);
    }
    double multiplier() const {return 1.0+depth*current;}
};

class Circuit {
public:
    explicit Circuit(double sampleRate = 48000.0, int oversample = 2, Segment segment = Segment::Complete)
        : sampleRate_(sampleRate), oversample_(oversample), segment_(segment) {
        if (sampleRate < 8000 || sampleRate > 384000 || oversample < 1 || oversample > 8)
            throw std::invalid_argument("Unsupported sample rate/oversampling");
        constructNetlist();
        const auto scoop=lookup_.find("scoop_out");
        if(scoop!=lookup_.end()) scoopedNode_=scoop->second;
        allocateSolver();
        configureDecimator();
        reset();
    }

    // Full-scale normalized audio = inputVoltsPerUnit peak (default 0.25 Vpeak).
    // The pedal's analog voltages cannot be recovered from digital full-scale
    // without calibration at the soundcard/interface.
    void setInputVoltsPerUnit(double x) { inputVoltsPerUnit_ = std::clamp(x, 0.001, 5.0); }
    // Safe to call between audio blocks. They change existing solver coefficients,
    // without rebuilding topology. Invalid names return false; no silent fallback.
    bool setComponentScale(const std::string& id,double scale) {
        if(!std::isfinite(scale) || scale < 0.01 || scale>100.0) return false;
        for(auto &r:resistors_) if(r.label==id) {r.scale=scale; refresh(r);return true;}
        for(auto &c:caps_) if(c.label==id) {c.scale=scale;refresh(c);return true;}
        return false;
    }
    bool setResistorOhms(const std::string& id,double ohms) {
        if(!std::isfinite(ohms) || ohms<0.01 || ohms>1e13) return false;
        for(auto &r:resistors_) if(r.label==id) {r.nominal=ohms;refresh(r);return true;}
        return false;
    }
    bool setCapacitanceFarads(const std::string& id,double farads) {
        if(!std::isfinite(farads) || farads<1e-15 || farads>1.0) return false;
        for(auto &c:caps_) if(c.label==id) {c.nominal=farads;refresh(c);return true;}
        return false;
    }
    bool setComponentJitter(const std::string& id,double amount,double refreshHz=5.0,
                            double smoothingSeconds=0.02,uint32_t seed=12345) {
        if(!std::isfinite(amount)||!std::isfinite(refreshHz)||!std::isfinite(smoothingSeconds)) return false;
        for(auto &r:resistors_) if(r.label==id) {
            r.jitter.configure(amount,refreshHz,smoothingSeconds,hashLabel(seed,id),internalRate());
            refresh(r);recomputeJitterFlag();return true;
        }
        for(auto &c:caps_) if(c.label==id) {
            c.jitter.configure(amount,refreshHz,smoothingSeconds,hashLabel(seed,id),internalRate());
            refresh(c);recomputeJitterFlag();return true;
        }
        return false;
    }
    // Component tolerance is static, reproducible, and name-stable across
    // reference/partitioned solvers. Potentiometer segments also vary.
    void randomizeTolerances(double resistorFraction, double capacitorFraction,
                             uint32_t seed=0xBADC0DEu) {
        resistorFraction=std::clamp(resistorFraction,0.0,0.95);
        capacitorFraction=std::clamp(capacitorFraction,0.0,0.95);
        for(auto &r:resistors_) {
            r.tolerance=1.0+resistorFraction*randomUnit(hashLabel(seed,r.label));refresh(r);
        }
        for(auto &c:caps_) {
            c.tolerance=1.0+capacitorFraction*randomUnit(hashLabel(seed,c.label));refresh(c);
        }
    }
    bool setOpAmpParameters(const std::string &id,OpAmpParameters p) {
        if(!std::isfinite(p.openLoopGain) || p.openLoopGain<1 || p.openLoopGain>1e7 ||
           !std::isfinite(p.gainBandwidthHz) || p.gainBandwidthHz<0 || p.gainBandwidthHz>1e9 ||
           !std::isfinite(p.slewRateVPerUs) || p.slewRateVPerUs<0 || p.slewRateVPerUs>1e5 ||
           !std::isfinite(p.outputSwingVolts) || p.outputSwingVolts<=0.01 || p.outputSwingVolts>4.49 ||
           !std::isfinite(p.overloadRecoveryMs) || p.overloadRecoveryMs<0 || p.overloadRecoveryMs>1000 ||
           !std::isfinite(p.overloadRecoveryDepth) || p.overloadRecoveryDepth<0 || p.overloadRecoveryDepth>1 ||
           !std::isfinite(p.offsetVolts) || std::abs(p.offsetVolts)>0.2 ||
           !std::isfinite(p.inputBiasAmps) || std::abs(p.inputBiasAmps)>1e-3 ||
           !std::isfinite(p.inputOffsetAmps) || std::abs(p.inputOffsetAmps)>1e-3) return false;
        for(auto &a:amps_) if(a.label==id) {a.parameters=p;return true;}
        return false;
    }
    bool setOpAmpJitter(const std::string &id,double slewAmount,double offsetJitterVolts,
                        double refreshHz=4.0,double smoothingSeconds=0.05,uint32_t seed=12345) {
        if(!std::isfinite(offsetJitterVolts)||std::abs(offsetJitterVolts)>0.2||
           !std::isfinite(slewAmount)||!std::isfinite(refreshHz)||!std::isfinite(smoothingSeconds))return false;
        for(auto &a:amps_) if(a.label==id) {
            a.slewJitter.configure(slewAmount,refreshHz,smoothingSeconds,
                                   hashLabel(seed,id+"/slew"),internalRate());
            a.offsetJitter.configure(0.95,refreshHz,smoothingSeconds,
                                   hashLabel(seed,id+"/offset"),internalRate());
            a.offsetJitterVolts=offsetJitterVolts;
            recomputeJitterFlag();return true;
        }
        return false;
    }
    bool setTransistorParameters(const std::string& id,TransistorParameters p) {
        if(!std::isfinite(p.beta)||p.beta<1||p.beta>2000 ||
           !std::isfinite(p.saturationAmps)||p.saturationAmps<1e-20||p.saturationAmps>1e-8 ||
           !std::isfinite(p.reverseAlpha)||p.reverseAlpha<=0||p.reverseAlpha>=0.99) return false;
        for(auto &q:npns_) if(q.label==id) {
            q.nominalIs=p.saturationAmps;q.nominalBeta=p.beta;q.alphaR=p.reverseAlpha;
            refresh(q);return true;
        }
        return false;
    }
    bool setDiodeParameters(const std::string& id,DiodeParameters p) {
        if(!std::isfinite(p.saturationAmps)||p.saturationAmps<1e-20||p.saturationAmps>1e-3||
           !std::isfinite(p.emission)||p.emission<1.0||p.emission>4.0)return false;
        for(auto &d:diodes_) if(d.label==id) {
            d.nominalIs=p.saturationAmps;d.nominalEmission=p.emission;refresh(d);return true;
        }
        return false;
    }
    bool setTransistorJitter(const std::string &id,double betaFraction,double satFraction,
                             double hz=4.0,double smoothSeconds=0.05,uint32_t seed=12345) {
        if(!std::isfinite(betaFraction)||!std::isfinite(satFraction)||
           !std::isfinite(hz)||!std::isfinite(smoothSeconds))return false;
        for(auto &q:npns_)if(q.label==id) {
            q.betaJitter.configure(betaFraction,hz,smoothSeconds,hashLabel(seed,id+"/beta"),internalRate());
            q.saturationJitter.configure(satFraction,hz,smoothSeconds,
                                          hashLabel(seed,id+"/is"),internalRate());
            refresh(q);recomputeJitterFlag();return true;
        }
        return false;
    }
    bool setDiodeJitter(const std::string &id,double saturationFraction,double emissionFraction,
                        double hz=4.,double smoothSeconds=0.05,uint32_t seed=12345) {
        if(!std::isfinite(saturationFraction)||!std::isfinite(emissionFraction)||
           !std::isfinite(hz)||!std::isfinite(smoothSeconds))return false;
        for(auto &d:diodes_)if(d.label==id) {
            d.saturationJitter.configure(saturationFraction,hz,smoothSeconds,
                                         hashLabel(seed,id+"/is"),internalRate());
            d.emissionJitter.configure(emissionFraction,hz,smoothSeconds,
                                       hashLabel(seed,id+"/n"),internalRate());
            refresh(d);recomputeJitterFlag();return true;
        }
        return false;
    }
    bool setOpAmpRecovery(const std::string& id,double milliseconds,double depth) {
        if(!std::isfinite(milliseconds)||milliseconds<0||milliseconds>1000 ||
           !std::isfinite(depth)||depth<0||depth>1) return false;
        for(auto &a:amps_) if(a.label==id) {
            a.parameters.overloadRecoveryMs=milliseconds;
            a.parameters.overloadRecoveryDepth=depth;
            if(milliseconds==0 || depth==0) a.overloadMemory=0.;
            return true;
        }
        return false;
    }
    bool setOpAmpSlewRate(const std::string& id,double vPerUs) {
        if(!std::isfinite(vPerUs)||vPerUs<0||vPerUs>1e5)return false;
        for(auto &a:amps_)if(a.label==id){a.parameters.slewRateVPerUs=vPerUs;return true;}
        return false;
    }
    bool setOpAmpBandwidth(const std::string& id,double hz) {
        if(!std::isfinite(hz)||hz<0||hz>1e9)return false;
        for(auto &a:amps_)if(a.label==id){a.parameters.gainBandwidthHz=hz;return true;}
        return false;
    }
    bool setOpAmpOutputSwing(const std::string& id,double volts) {
        if(!std::isfinite(volts)||volts<0.01||volts>4.49)return false;
        for(auto &a:amps_)if(a.label==id){a.parameters.outputSwingVolts=volts;return true;}
        return false;
    }
    // Thermal voltage changes; saturation-current temperature dependence isn't
    // calibrated, so this is an exploratory temperature parameter.
    void setTemperatureCelsius(double degrees) {
        if(!std::isfinite(degrees))return;
        temperatureCelsius_=std::clamp(degrees,-20.,125.);
        for(auto &d:diodes_)d.thermal=thermalVoltage();
    }
    void setSupplyVoltage(double volts) {if(std::isfinite(volts)) supplyVoltage_=std::clamp(volts,4.0,15.0);}
    void setReferenceVoltage(double volts) {
        if(std::isfinite(volts)) {
            referenceVoltage_=std::clamp(volts,0.5,supplyVoltage_-0.5);
            referenceFollowsRail_=false;
        }
    }
    // Handy for a dynamically moving reference bias or sag: 0.5 follows the rail.
    void setReferenceRatio(double ratio) {
        if(std::isfinite(ratio)) { referenceRatio_=std::clamp(ratio,0.1,0.9);referenceFollowsRail_=true; }
    }
    void setReferenceVoltageFixed(double volts) {setReferenceVoltage(volts);}
    std::vector<std::string> componentNames() const {
        std::vector<std::string> result;result.reserve(resistors_.size()+caps_.size());
        for(auto &r:resistors_)result.push_back(r.label);
        for(auto &c:caps_)result.push_back(c.label);
        return result;
    }
    std::vector<std::string> opAmpNames() const {
        std::vector<std::string> result;result.reserve(amps_.size());
        for(auto &a:amps_)result.push_back(a.label);
        return result;
    }
    std::vector<std::string> transistorNames() const {
        std::vector<std::string> result;result.reserve(npns_.size());
        for(auto &q:npns_)result.push_back(q.label);
        return result;
    }
    std::vector<std::string> diodeNames() const {
        std::vector<std::string> result;result.reserve(diodes_.size());
        for(auto &d:diodes_)result.push_back(d.label);
        return result;
    }
    void setControls(Controls c) {
        c.gain   = std::clamp(c.gain,   0.0f, 1.0f);
        c.bass   = std::clamp(c.bass,   0.0f, 1.0f);
        c.treble = std::clamp(c.treble, 0.0f, 1.0f);
        c.level  = std::clamp(c.level,  0.0f, 1.0f);
        controls_ = c;
        // The B50K EQ controls are linear pots. Their direction follows pin 1->3.
        if (highLeft_>=0) setSplitPot(highLeft_, highRight_, 50000., controls_.treble);
        if (lowLeft_>=0) setSplitPot(lowLeft_, lowRight_, 50000., controls_.bass);
        // Audio-taper approximations. Actual fitted pots can be calibrated later.
        const auto aTaper = [](double x) { return x*x; };
        if (gainPot_>=0) setResistorNominal(gainPot_,1.0 + 50000.0 * aTaper(controls_.gain));
        if (levelTop_>=0) setSplitPot(levelTop_, levelBottom_, 50000., 1.0-aTaper(controls_.level));
        constexpr double on = 0.1, off = 1.0e12;
        if (switchFull_>=0) setResistorNominal(switchFull_,controls_.mode == Mode::Full ? on : off);
        if (switchScoop_>=0) setResistorNominal(switchScoop_,controls_.mode == Mode::Scooped ? on : off);
        if (switchBoost_>=0) setResistorNominal(switchBoost_,controls_.mode == Mode::Boost ? on : off);
    }

    Controls controls() const { return controls_; }
    int oversampling() const { return oversample_; }
    // Voltage-level single internal step for the partitioned real-time model.
    double stepVolts(double input, double source2=4.5, double source3=4.5) {
        fixedInput_=input; fixedSource2_=source2; fixedSource3_=source3;
        advanceJitter();
        solve(1.0/(sampleRate_*oversample_),solverIterationLimit_);
        updateCaps();
        return voltage(outputNode_);
    }
    int nodeCount() const { return static_cast<int>(names_.size()); }
    // A host may explicitly bound expensive pathological Newton cases. A
    // smaller cap can increase convergence failures; default is v2's 320.
    bool setSolverIterationLimit(int n) {
        if(n<16 || n>320) return false;
        solverIterationLimit_=n;
        return true;
    }
    int solverIterationLimit() const {return solverIterationLimit_;}
    // Optional v8 experiment. Exact std::tanh remains the reference default.
    // Call at a host control boundary, not concurrently with process().
    void setFastNonlinearMath(bool enable) {fastNonlinearMath_=enable;}
    bool fastNonlinearMath() const {return fastNonlinearMath_;}
    void setLinearSolver(LinearSolver x) {linearSolver_=x;}
    LinearSolver linearSolver() const {return linearSolver_;}
    unsigned long long compactFallbacks() const {return compactFallbacks_;}
    int reducedDimension() const {return static_cast<int>(boundary_.size());}
    int eliminatedDimension() const {return static_cast<int>(linear_.size());}
    unsigned long long reducedFallbacks() const {return reducedFallbacks_;}
    unsigned long long schurRebuilds() const {return schurRebuilds_;}
    unsigned long long totalNewtonIterations() const {return totalNewtonIterations_;}
    unsigned long long convergedSteps() const {return convergedSteps_;}
    int peakNewtonIterations() const {return peakNewtonIterations_;}
    double scoopedOutputVolts() const {
        return scoopedNode_<0 ? 0.0 : voltage(scoopedNode_);
    }
    int lastIterations() const { return lastIterations_; }
    bool lastConverged() const { return lastConverged_; }
    unsigned long long failedSolves() const { return failedSolves_; }
    double inspectVoltage(const std::string& name) const {
        if (name == "GND") return 0.;
        if (name == "VCC") return supplyVoltage_;
        if (name == "VREF") return vref();
        if (name == "IN") return fixedInput_;
        if (name == "SRC2") return fixedSource2_;
        if (name == "SRC3") return fixedSource3_;
        auto i = lookup_.find(name);
        if (i == lookup_.end()) throw std::invalid_argument("Unknown circuit node: " + name);
        return voltage(i->second);
    }

    void reset() {
        std::fill(unknowns_.begin(), unknowns_.end(), 0.0);
        for (int n = kFirstFree; n < static_cast<int>(names_.size()); ++n) {
            unknowns_[n-kFirstFree] = names_[n] == "post_clip" ? 0.0 : vref();
        }
        for (int op = 0; op < static_cast<int>(amps_.size()); ++op)
            unknowns_[numFreeNodes_+op] = 0.0;
        fixedInput_ = (segment_==Segment::Front || segment_==Segment::Complete) ? 0.0 : 4.5;
        fixedSource2_ = fixedSource3_ = vref();
        for(auto &a:amps_) {a.previousOutput=vref();a.overloadMemory=0.;a.slewJitter.reset();a.offsetJitter.reset();}
        for(auto &q:npns_) {q.saturationJitter.reset();q.betaJitter.reset();refresh(q);}
        for(auto &d:diodes_) {d.saturationJitter.reset();d.emissionJitter.reset();refresh(d);}
        for(auto &r:resistors_) {r.jitter.reset();refresh(r);}
        for(auto &c:caps_) {c.jitter.reset();refresh(c);}
        lastInput_ = 0.0;
        std::fill(decimatorHistory_.begin(),decimatorHistory_.end(),0.0);
        decimatorPos_ = 0;
        // Solve capacitor-open DC network, then charge every capacitor to its DC value.
        // Convergence need not be perfect here: warm-up solves account for remaining bias.
        solve(0.0, 70);
        for (auto &c : caps_) {
            c.previousVoltage=voltage(c.a)-voltage(c.b);
            c.previousCharge=c.farads*c.previousVoltage;
        }
        for (auto &a : amps_) a.previousOutput = voltage(a.out);
        // Bring the nonlinear diode/transistor network to bias at the actual dt.
        const double dt = 1.0/(sampleRate_*oversample_);
        for (int i=0; i<80; ++i) { solve(dt, 20); updateCaps(); }
        lastConverged_ = true;
        failedSolves_ = 0;
        totalNewtonIterations_=convergedSteps_=0;
        peakNewtonIterations_=0;
    }

    // Mono: no heap allocations, file I/O, or configuration rebuild in callback.
    float process(float sample) {
        // Linear interpolation limits internal discontinuity with oversampling.
        const double current = std::isfinite(sample) ? std::clamp(double(sample), -8.0, 8.0) : 0.0;
        const double dt = 1.0/(sampleRate_*oversample_);
        double accum = 0.0;
        for (int i=0; i<oversample_; ++i) {
            const double t = double(i+1)/oversample_;
            fixedInput_ = inputVoltsPerUnit_*((1.0-t)*lastInput_ + t*current);
            advanceJitter();
            solve(dt, 120);
            updateCaps();
            decimatorHistory_[decimatorPos_] = voltage(outputNode_);
            decimatorPos_ = (decimatorPos_+1)%decimatorHistory_.size();
        }
        lastInput_ = current;
        if (oversample_ == 1) accum = decimatorHistory_[(decimatorPos_ + decimatorHistory_.size()-1) % decimatorHistory_.size()];
        else {
            // Anti-alias windowed-sinc low-pass before decimation, unlike box averaging.
            const std::size_t N=decimatorHistory_.size();
            for (std::size_t k=0; k<N; ++k)
                accum += decimatorKernel_[k] * decimatorHistory_[(decimatorPos_+N-1-k)%N];
        }
        // 1 V audio output per 1.0 float. DC removed by C25/R36.
        return float(std::clamp(accum, -10.0, 10.0));
    }

private:
    static constexpr int kGnd=0, kVcc=1, kVref=2, kInput=3, kSource2=4, kSource3=5, kFirstFree=6;
    struct Resistor {
        int a,b;double ohms, conductance=0.;std::string label;
        double nominal,scale=1.0,tolerance=1.0;Jitter jitter;
    };
    struct Capacitor {
        int a,b;double farads,previousVoltage,conductance=0.;std::string label;
        double previousCharge=0.;
        double nominal,scale=1.0,tolerance=1.0;Jitter jitter;
    };
    struct Diode {
        int anode,cathode;double saturation,emission,thermal;std::string label;
        int ai=-1,ki=-1;
        double nominalIs=2.52e-9,nominalEmission=1.75;
        Jitter saturationJitter,emissionJitter;
    };
    struct NPN {
        int c,b,e;double is,alphaF,alphaR;std::string label;
        int ci=-1,bi=-1,ei=-1;
        double nominalIs=1e-14,nominalBeta=350.;
        Jitter saturationJitter,betaJitter;
    };
    struct OpAmp {
        int plus,minus,out;OpAmpParameters parameters;std::string label;
        double previousOutput=4.5,offsetJitterVolts=0.0,overloadMemory=0.0;
        Jitter slewJitter,offsetJitter;
    };
    double sampleRate_ = 48000.;
    int oversample_ = 2;
    Segment segment_ = Segment::Complete;
    double inputVoltsPerUnit_ = 0.25, fixedInput_ = 0., lastInput_ = 0.;
    double fixedSource2_=4.5, fixedSource3_=4.5;
    double supplyVoltage_=9.0,referenceVoltage_=4.5,referenceRatio_=0.5;
    bool referenceFollowsRail_=true,jitterEnabled_=false,passiveJitterEnabled_=false;
    double temperatureCelsius_=25.0;
    Controls controls_{};
    std::unordered_map<std::string,int> lookup_;
    std::vector<std::string> names_ {"GND", "VCC", "VREF", "IN", "SRC2", "SRC3"};
    std::vector<Resistor> resistors_;
    std::vector<Capacitor> caps_;
    std::vector<Diode> diodes_;
    std::vector<NPN> npns_;
    std::vector<OpAmp> amps_;
    struct AmpEval { double swing=0, gainScale=0, alpha=1, slewDt=0, offset=0, reference=0; };
    std::vector<AmpEval> ampEval_;
    int gainPot_=-1, highLeft_=-1,highRight_=-1,lowLeft_=-1,lowRight_=-1;
    int levelTop_=-1,levelBottom_=-1,switchFull_=-1,switchScoop_=-1,switchBoost_=-1;
    int outputNode_=0, scoopedNode_=-1;
    int solverIterationLimit_=320;
    bool fastNonlinearMath_=false;
    int numFreeNodes_=0, dimension_=0;
    LinearSolver linearSolver_=LinearSolver::Auto;
    unsigned long long reducedFallbacks_=0,compactFallbacks_=0;
    // Two disjoint partitions of the Newton unknowns. Nonlinear device ports
    // and op-amp current variables go into boundary_; internal RC nodes go
    // into linear_. These indices are fixed at construction.
    std::vector<int> boundary_, linear_;
    std::vector<double> linLU_, linToBoundary_, boundaryToLin_, cross_,
                        linRhs_, linAnswer_, linCachedSource_, reduced_, reducedRhs_, reducedDelta_,
                        preparedWork_;
    std::vector<int> linPivots_;
    // v7: a complete nonlinear Newton iteration operates on boundary unknowns
    // only. Passive internal states are reconstructed exactly from cached LU.
    std::uint64_t compactRevision_=0;
    double compactDt_=-1.;
    std::vector<int> compactIndex_;
    std::vector<double> schurBase_,compactMatrix_,compactResidual_,compactWork_,
                        compactRhs_,compactStep_,compactSource_;

    std::vector<std::uint32_t> schurPattern_; // circuit-graph sparsity, cached with RC coefficients
    bool reducedAffineValid_=false, lastLinearUsedReduced_=false;
    // Cached linear MNA part: topology and conductances do not depend on
    // unknown voltages. Charges and fixed voltage sources are prepared anew
    // for each solve, but not for every Newton trial.
    struct LinearEntry {int row,col;double g;};
    std::vector<double> linearJac_,linearSource_;
    std::vector<LinearEntry> linearEntries_;
    int linearEntryCount_=0;
    std::uint64_t cachedLinearRevision_=0;
    double cachedLinearDt_=-1.;
    bool schurReady_=false;
    std::uint64_t linearRevision_=1,preparedRevision_=0;
    double preparedDt_=-1.;
    unsigned long long schurRebuilds_=0;
    std::vector<double> unknowns_, residual_, jac_, delta_, work_, rhs_, old_, compactBackup_;
    std::vector<double> decimatorKernel_, decimatorHistory_;
    std::size_t decimatorPos_=0;
    bool lastConverged_=true;
    unsigned long long failedSolves_=0;
    int lastIterations_=0;
    unsigned long long totalNewtonIterations_=0, convergedSteps_=0;
    int peakNewtonIterations_=0;

    int node(const std::string &name) {
        if (name == "GND") return kGnd;
        if (name == "VCC") return kVcc;
        if (name == "VREF") return kVref;
        if (name == "IN") return kInput;
        if (name == "SRC2") return kSource2;
        if (name == "SRC3") return kSource3;
        auto it = lookup_.find(name);
        if (it != lookup_.end()) return it->second;
        int id = static_cast<int>(names_.size());
        lookup_[name] = id;
        names_.push_back(name);
        return id;
    }
    void R(const char* id, const char* a, const char* b, double ohms) {
        Resistor r{};r.a=node(a);r.b=node(b);r.ohms=ohms;r.nominal=ohms;r.label=id;
        refresh(r);resistors_.push_back(r);
    }
    void C(const char* id, const char* a, const char* b, double farads) {
        Capacitor c;
        c.a=node(a);c.b=node(b);c.farads=farads;c.previousVoltage=0.0;
        c.label=id;c.nominal=farads;
        refresh(c);caps_.push_back(c);
    }
    void D(const char* id, const char* a, const char* k, double Is=2.52e-9, double n=1.75) {
        Diode d{};d.anode=node(a);d.cathode=node(k);d.saturation=Is;
        d.emission=n;d.thermal=thermalVoltage();d.nominalIs=Is;d.nominalEmission=n;d.label=id;
        d.ai=idx(d.anode);d.ki=idx(d.cathode);
        diodes_.push_back(d);
    }
    void Q(const char* id, const char* c, const char* b, const char* e) {
        const int cn=node(c),bn=node(b),en=node(e);
        constexpr double beta=350.0, alphaR=0.5;
        NPN q{};q.c=cn;q.b=bn;q.e=en;q.is=1e-14;
        q.nominalBeta=beta;q.nominalIs=q.is;q.alphaF=beta/(beta+1.0);
        q.alphaR=alphaR;q.label=id;
        q.ci=idx(q.c);q.bi=idx(q.b);q.ei=idx(q.e);
        npns_.push_back(q);
        // BC549C nominal diffusion/depletion capacitances, approximations.
        C((std::string(id)+"_CBE").c_str(), b,e,10e-12);
        C((std::string(id)+"_CBC").c_str(), b,c,3e-12);
    }
    void OP(const char* id,const char* plus,const char* minus,const char* out) {
        // Legacy ideal-bandwidth reference is the safe default. Explicitly
        // opt into the TL072/JRC4558 finite-bandwidth/slew presets.
        OpAmp a{};a.plus=node(plus);a.minus=node(minus);a.out=node(out);
        a.parameters=OpAmpParameters{};a.label=id;
        amps_.push_back(a);
    }
    static uint32_t hashLabel(uint32_t seed,const std::string& s) {
        uint32_t h=2166136261u ^ seed;
        for (unsigned char c:s) {h^=c;h*=16777619u;}
        return h ? h : 0x1234567u;
    }
    static double randomUnit(uint32_t h) {
        h^=h<<13;h^=h>>17;h^=h<<5;
        return 2.0*double(h)/4294967295.0-1.0;
    }
    double internalRate() const {return sampleRate_*oversample_;}
    double thermalVoltage() const {return 0.02585*(temperatureCelsius_+273.15)/298.15;}
    double vref() const {return referenceFollowsRail_ ? supplyVoltage_*referenceRatio_ : referenceVoltage_;}
    void refresh(NPN &q) {
        q.is=q.nominalIs*q.saturationJitter.multiplier();
        const double beta=q.nominalBeta*q.betaJitter.multiplier();
        q.alphaF=beta/(beta+1.0);
    }
    void refresh(Diode &d) {
        d.saturation=d.nominalIs*d.saturationJitter.multiplier();
        d.emission=d.nominalEmission*d.emissionJitter.multiplier();
    }
    void refresh(Resistor &r) {
        r.ohms=std::max(1e-3,r.nominal*r.scale*r.tolerance*r.jitter.multiplier());
        const double g=1./r.ohms;
        if(g!=r.conductance) {r.conductance=g;++linearRevision_;}
    }
    void refresh(Capacitor &c) {
        c.farads=std::max(1e-17,c.nominal*c.scale*c.tolerance*c.jitter.multiplier());
        // Every transient solve uses exactly one internal oversampled period.
        const double g=c.farads*internalRate();
        if(g!=c.conductance) {c.conductance=g;++linearRevision_;}
    }
    void setResistorNominal(int idx,double value) {
        resistors_[idx].nominal=value;refresh(resistors_[idx]);
    }
    void setSplitPot(int first,int second,double nominal,double t) {
        setResistorNominal(first,1.0+nominal*t);
        setResistorNominal(second,1.0+nominal*(1.0-t));
    }
    void recomputeJitterFlag() {
        jitterEnabled_=false;
        passiveJitterEnabled_=false;
        for(const auto &r:resistors_) if(r.jitter.depth>0){jitterEnabled_=true;passiveJitterEnabled_=true;}
        for(const auto &c:caps_) if(c.jitter.depth>0){jitterEnabled_=true;passiveJitterEnabled_=true;}
        for(const auto &a:amps_) if(a.slewJitter.depth>0||a.offsetJitterVolts!=0.)jitterEnabled_=true;
        for(const auto &q:npns_) if(q.betaJitter.depth>0||q.saturationJitter.depth>0)jitterEnabled_=true;
        for(const auto &d:diodes_) if(d.saturationJitter.depth>0||d.emissionJitter.depth>0)jitterEnabled_=true;
    }
    void advanceJitter() {
        if(!jitterEnabled_)return;
        for(auto &r:resistors_) if(r.jitter.depth>0) {r.jitter.advance(internalRate());refresh(r);}
        for(auto &c:caps_) if(c.jitter.depth>0) {c.jitter.advance(internalRate());refresh(c);}
        for(auto &q:npns_)if(q.betaJitter.depth>0||q.saturationJitter.depth>0) {
            q.betaJitter.advance(internalRate());q.saturationJitter.advance(internalRate());refresh(q);
        }
        for(auto &d:diodes_)if(d.saturationJitter.depth>0||d.emissionJitter.depth>0) {
            d.saturationJitter.advance(internalRate());d.emissionJitter.advance(internalRate());refresh(d);
        }
        for(auto &a:amps_) {
            if(a.slewJitter.depth>0)a.slewJitter.advance(internalRate());
            if(a.offsetJitterVolts!=0)a.offsetJitter.advance(internalRate());
        }
    }
    void constructNetlist() {
        if (segment_ != Segment::Complete) { constructPartition();return; }
        // INPUT / IC1.1 (TL072 unity buffer)
        R("R1", "IN", "c1_in", 10000.);
        C("C1", "c1_in", "input_plus", 47e-9);
        R("R2", "input_plus", "VREF", 1e6);
        OP("IC1.1 TL072", "input_plus", "buf", "buf");
        // IC1.2: TL072 boost gain and 47p feedback capacitor.
        C("C3", "buf", "gain_plus", 100e-9);
        R("R41", "gain_plus", "VREF", 1e6);
        R("R3", "gain_minus", "c2_top", 1500.);
        C("C2", "c2_top", "GND", 2.2e-6);
        R("R4", "gain_minus", "gain_pot_start", 2200.);
        gainPot_ = static_cast<int>(resistors_.size());
        R("GAIN A50K, wiper tied to pin 3", "gain_pot_start", "boost_out", 12501.);
        C("C4", "gain_minus", "boost_out", 47e-12);
        OP("IC1.2 TL072", "gain_plus", "gain_minus", "boost_out");
        R("R5", "boost_out", "r5_out", 10000.);
        C("C5", "r5_out", "GND", 33e-9);
        C("C6", "r5_out", "q1_base", 10e-6);
        // Q1: phase-splitting common emitter stage.
        R("R6", "VCC", "q1_base", 220000.);
        R("R7", "q1_base", "GND", 100000.);
        R("R8", "VCC", "q1_col", 4700.);
        R("R9", "q1_emit", "GND", 10000.);
        Q("Q1 BC549C", "q1_col", "q1_base", "q1_emit");
        R("R10", "q1_col", "c7_left", 1000.);
        C("C7", "c7_left", "q2_base", 1e-6);
        R("R11", "q1_emit", "c8_left", 1000.);
        C("C8", "c8_left", "q3_base", 1e-6);
        // Q2/Q3: common-collector load and common emitter network (full-wave octave).
        R("R12", "VCC", "q2_base", 100000.);
        R("R13", "q2_base", "GND", 27000.);
        R("R14", "VCC", "q23_col", 10000.);
        R("R15", "q23_emit", "GND", 1800.);
        C("C9", "q23_emit", "GND", 47e-6);
        R("R16", "VCC", "q3_base", 100000.);
        R("R17", "q3_base", "GND", 27000.);
        Q("Q2 BC549C", "q23_col", "q2_base", "q23_emit");
        Q("Q3 BC549C", "q23_col", "q3_base", "q23_emit");
        // 1N914 antiparallel ground-clipping diodes.
        C("C10", "q23_col", "post_clip", 1e-6);
        D("D1 1N914", "post_clip", "GND");
        D("D2 1N914", "GND", "post_clip");
        // FULL: 10k/15n rolloff, JRC4558 inverting x2.7 gain.
        R("R18", "post_clip", "full_lowpass", 10000.);
        C("C11", "full_lowpass", "GND", 15e-9);
        C("C12", "full_lowpass", "full_input", 1e-6);
        R("R20", "full_input", "full_minus", 10000.);
        R("R23", "full_minus", "full_out", 27000.);
        C("C15", "full_minus", "full_out", 47e-12);
        OP("IC2.1 JRC4558", "VREF", "full_minus", "full_out");
        R("R27", "full_out", "full_sw", 1000.);
        // SCOOPED: twin-frequency-sensitive feed and JRC4558 x12.
        R("R19", "post_clip", "scoop_lp1", 47000.);
        C("C14", "scoop_lp1", "GND", 4.7e-9);
        R("R22", "scoop_lp1", "scoop_lp2", 10000.);
        C("C16", "scoop_lp2", "GND", 4.7e-9);
        C("C13", "post_clip", "scoop_bypass", 1e-9);
        R("R21", "scoop_bypass", "scoop_lp2", 10000.);
        C("C17", "scoop_lp2", "scoop_input", 1e-6);
        R("R24", "scoop_input", "scoop_minus", 10000.);
        R("R25", "scoop_minus", "scoop_out", 120000.);
        C("C18", "scoop_minus", "scoop_out", 10e-12);
        OP("IC2.2 JRC4558", "VREF", "scoop_minus", "scoop_out");
        R("R26", "scoop_out", "scoop_sw", 1000.);
        // BOOST bypasses Q1-Q3, clipping, both fuzz mode filters.
        C("C28", "boost_out", "boost_sw", 10e-6);
        R("R42", "boost_sw", "VREF", 100000.);
        switchFull_ = static_cast<int>(resistors_.size());
        R("SW2 FULL closed", "full_sw", "mode_bus", 0.1);
        switchScoop_ = static_cast<int>(resistors_.size());
        R("SW2 SCOOPED closed", "scoop_sw", "mode_bus", 1e12);
        switchBoost_ = static_cast<int>(resistors_.size());
        R("SW2 BOOST closed", "boost_sw", "mode_bus", 1e12);
        // ACTIVE EQ: circuit topology includes both pot wiper networks and IC3.2.
        R("R28", "mode_bus", "VREF", 100000.);
        R("R29", "mode_bus", "eq_plus", 10000.);
        highLeft_ = static_cast<int>(resistors_.size());
        R("HIGH B50K pin1-wiper", "eq_plus", "high_wiper", 25001.);
        highRight_ = static_cast<int>(resistors_.size());
        R("HIGH B50K wiper-pin3", "high_wiper", "eq_minus", 25001.);
        R("R30", "high_wiper", "high_cap", 3300.);
        C("C19", "high_cap", "GND", 15e-9);
        lowLeft_ = static_cast<int>(resistors_.size());
        R("LOW B50K pin1-wiper", "eq_plus", "low_wiper", 25001.);
        lowRight_ = static_cast<int>(resistors_.size());
        R("LOW B50K wiper-pin3", "low_wiper", "eq_minus", 25001.);
        C("C20", "low_wiper", "low_tap", 150e-9);
        C("C21", "low_tap", "low_buf_plus", 47e-9);
        R("R31", "low_buf_plus", "VREF", 100000.);
        R("R32", "low_tap", "low_buf_out", 3300.);
        OP("IC3.2 JRC4558", "low_buf_plus", "low_buf_out", "low_buf_out");
        R("R33", "eq_minus", "eq_out", 10000.);
        C("C22", "eq_minus", "eq_out", 47e-12);
        OP("IC3.1 JRC4558", "eq_plus", "eq_minus", "eq_out");
        // LEVEL + Q4 emitter follower / physical output load.
        C("C23", "eq_out", "level_top", 10e-6);
        levelTop_ = static_cast<int>(resistors_.size());
        R("LEVEL A50K pin3-wiper", "level_top", "level_wiper", 25001.);
        levelBottom_ = static_cast<int>(resistors_.size());
        R("LEVEL A50K wiper-pin1", "level_wiper", "GND", 25001.);
        C("C24", "level_wiper", "q4_base", 1e-6);
        R("R34", "q4_base", "VREF", 100000.);
        Q("Q4 BC549C", "VCC", "q4_base", "q4_emit");
        R("R35", "q4_emit", "GND", 10000.);
        C("C25", "q4_emit", "post_out", 10e-6);
        R("R36", "post_out", "GND", 100000.);
        R("R37", "post_out", "OUT", 1000.);
        R("output_load_1M", "OUT", "GND", 1e6);
        outputNode_ = node("OUT");
        // Supply: D3 1N5817, C26 100u, R39/R40 10k and C27 47u
        // are represented by fixed 9V VCC and 4.5V VREF references.
        // Their ripple, reverse-battery effects, and supply sag are not simulated.
        setControls(controls_);
    }
    void constructPartition() {
        switch (segment_) {
            case Segment::Front: {
        // INPUT / IC1.1 (TL072 unity buffer)
        R("R1", "IN", "c1_in", 10000.);
        C("C1", "c1_in", "input_plus", 47e-9);
        R("R2", "input_plus", "VREF", 1e6);
        OP("IC1.1 TL072", "input_plus", "buf", "buf");
        // IC1.2: TL072 boost gain and 47p feedback capacitor.
        C("C3", "buf", "gain_plus", 100e-9);
        R("R41", "gain_plus", "VREF", 1e6);
        R("R3", "gain_minus", "c2_top", 1500.);
        C("C2", "c2_top", "GND", 2.2e-6);
        R("R4", "gain_minus", "gain_pot_start", 2200.);
        gainPot_ = static_cast<int>(resistors_.size());
        R("GAIN A50K, wiper tied to pin 3", "gain_pot_start", "boost_out", 12501.);
        C("C4", "gain_minus", "boost_out", 47e-12);
        OP("IC1.2 TL072", "gain_plus", "gain_minus", "boost_out");
        outputNode_ = node("boost_out");
                break;
            }
            case Segment::Fuzz: {
        R("R5", "IN", "r5_out", 10000.);
        C("C5", "r5_out", "GND", 33e-9);
        C("C6", "r5_out", "q1_base", 10e-6);
        // Q1: phase-splitting common emitter stage.
        R("R6", "VCC", "q1_base", 220000.);
        R("R7", "q1_base", "GND", 100000.);
        R("R8", "VCC", "q1_col", 4700.);
        R("R9", "q1_emit", "GND", 10000.);
        Q("Q1 BC549C", "q1_col", "q1_base", "q1_emit");
        R("R10", "q1_col", "c7_left", 1000.);
        C("C7", "c7_left", "q2_base", 1e-6);
        R("R11", "q1_emit", "c8_left", 1000.);
        C("C8", "c8_left", "q3_base", 1e-6);
        // Q2/Q3: common-collector load and common emitter network (full-wave octave).
        R("R12", "VCC", "q2_base", 100000.);
        R("R13", "q2_base", "GND", 27000.);
        R("R14", "VCC", "q23_col", 10000.);
        R("R15", "q23_emit", "GND", 1800.);
        C("C9", "q23_emit", "GND", 47e-6);
        R("R16", "VCC", "q3_base", 100000.);
        R("R17", "q3_base", "GND", 27000.);
        Q("Q2 BC549C", "q23_col", "q2_base", "q23_emit");
        Q("Q3 BC549C", "q23_col", "q3_base", "q23_emit");
        // 1N914 antiparallel ground-clipping diodes.
        C("C10", "q23_col", "post_clip", 1e-6);
        D("D1 1N914", "post_clip", "GND");
        D("D2 1N914", "GND", "post_clip");
        // FULL: 10k/15n rolloff, JRC4558 inverting x2.7 gain.
        R("R18", "post_clip", "full_lowpass", 10000.);
        C("C11", "full_lowpass", "GND", 15e-9);
        C("C12", "full_lowpass", "full_input", 1e-6);
        R("R20", "full_input", "full_minus", 10000.);
        R("R23", "full_minus", "full_out", 27000.);
        C("C15", "full_minus", "full_out", 47e-12);
        OP("IC2.1 JRC4558", "VREF", "full_minus", "full_out");
        // SCOOPED: twin-frequency-sensitive feed and JRC4558 x12.
        R("R19", "post_clip", "scoop_lp1", 47000.);
        C("C14", "scoop_lp1", "GND", 4.7e-9);
        R("R22", "scoop_lp1", "scoop_lp2", 10000.);
        C("C16", "scoop_lp2", "GND", 4.7e-9);
        C("C13", "post_clip", "scoop_bypass", 1e-9);
        R("R21", "scoop_bypass", "scoop_lp2", 10000.);
        C("C17", "scoop_lp2", "scoop_input", 1e-6);
        R("R24", "scoop_input", "scoop_minus", 10000.);
        R("R25", "scoop_minus", "scoop_out", 120000.);
        C("C18", "scoop_minus", "scoop_out", 10e-12);
        OP("IC2.2 JRC4558", "VREF", "scoop_minus", "scoop_out");
        outputNode_ = node("full_out");
                break;
            }
            case Segment::Output: {
        R("R27", "IN", "full_sw", 1000.);
        R("R26", "SRC2", "scoop_sw", 1000.);
        // BOOST bypasses Q1-Q3, clipping, both fuzz mode filters.
        C("C28", "SRC3", "boost_sw", 10e-6);
        R("R42", "boost_sw", "VREF", 100000.);
        switchFull_ = static_cast<int>(resistors_.size());
        R("SW2 FULL closed", "full_sw", "mode_bus", 0.1);
        switchScoop_ = static_cast<int>(resistors_.size());
        R("SW2 SCOOPED closed", "scoop_sw", "mode_bus", 1e12);
        switchBoost_ = static_cast<int>(resistors_.size());
        R("SW2 BOOST closed", "boost_sw", "mode_bus", 1e12);
        // ACTIVE EQ: circuit topology includes both pot wiper networks and IC3.2.
        R("R28", "mode_bus", "VREF", 100000.);
        R("R29", "mode_bus", "eq_plus", 10000.);
        highLeft_ = static_cast<int>(resistors_.size());
        R("HIGH B50K pin1-wiper", "eq_plus", "high_wiper", 25001.);
        highRight_ = static_cast<int>(resistors_.size());
        R("HIGH B50K wiper-pin3", "high_wiper", "eq_minus", 25001.);
        R("R30", "high_wiper", "high_cap", 3300.);
        C("C19", "high_cap", "GND", 15e-9);
        lowLeft_ = static_cast<int>(resistors_.size());
        R("LOW B50K pin1-wiper", "eq_plus", "low_wiper", 25001.);
        lowRight_ = static_cast<int>(resistors_.size());
        R("LOW B50K wiper-pin3", "low_wiper", "eq_minus", 25001.);
        C("C20", "low_wiper", "low_tap", 150e-9);
        C("C21", "low_tap", "low_buf_plus", 47e-9);
        R("R31", "low_buf_plus", "VREF", 100000.);
        R("R32", "low_tap", "low_buf_out", 3300.);
        OP("IC3.2 JRC4558", "low_buf_plus", "low_buf_out", "low_buf_out");
        R("R33", "eq_minus", "eq_out", 10000.);
        C("C22", "eq_minus", "eq_out", 47e-12);
        OP("IC3.1 JRC4558", "eq_plus", "eq_minus", "eq_out");
        // LEVEL + Q4 emitter follower / physical output load.
        C("C23", "eq_out", "level_top", 10e-6);
        levelTop_ = static_cast<int>(resistors_.size());
        R("LEVEL A50K pin3-wiper", "level_top", "level_wiper", 25001.);
        levelBottom_ = static_cast<int>(resistors_.size());
        R("LEVEL A50K wiper-pin1", "level_wiper", "GND", 25001.);
        C("C24", "level_wiper", "q4_base", 1e-6);
        R("R34", "q4_base", "VREF", 100000.);
        Q("Q4 BC549C", "VCC", "q4_base", "q4_emit");
        R("R35", "q4_emit", "GND", 10000.);
        C("C25", "q4_emit", "post_out", 10e-6);
        R("R36", "post_out", "GND", 100000.);
        R("R37", "post_out", "OUT", 1000.);
        R("output_load_1M", "OUT", "GND", 1e6);
        outputNode_ = node("OUT");
                break;
            }
            case Segment::Complete: break;
        }
        setControls(controls_);
    }
    void allocateSolver() {
        numFreeNodes_ = static_cast<int>(names_.size()) - kFirstFree;
        dimension_ = numFreeNodes_ + static_cast<int>(amps_.size());
        unknowns_.assign(dimension_, 0.0);
        residual_.assign(dimension_, 0.0);
        jac_.assign(dimension_*dimension_, 0.0);
        delta_.assign(dimension_, 0.0);
        work_.assign(dimension_*dimension_, 0.0);
        rhs_.assign(dimension_, 0.0);
        old_.assign(dimension_, 0.0);
        compactBackup_.assign(dimension_, 0.0);
        ampEval_.resize(amps_.size());
        // Only genuinely nonlinear ports remain in the reduced Newton system.
        std::vector<unsigned char> boundaryMask(static_cast<std::size_t>(dimension_),0);
        const auto mark=[&](int n) {if(n>=kFirstFree)boundaryMask[n-kFirstFree]=1;};
        for(const auto &d:diodes_) {mark(d.anode);mark(d.cathode);}
        for(const auto &q:npns_) {mark(q.c);mark(q.b);mark(q.e);}
        for(const auto &a:amps_) {mark(a.plus);mark(a.minus);mark(a.out);}
        for(int i=numFreeNodes_;i<dimension_;++i)boundaryMask[i]=1;
        for(int i=0;i<dimension_;++i)
            (boundaryMask[i]?boundary_:linear_).push_back(i);
        const std::size_t l=linear_.size(),b=boundary_.size();
        linearJac_.resize(dimension_*dimension_);
        linearSource_.resize(dimension_);
        linearEntries_.resize(dimension_*dimension_);
        linLU_.resize(l*l);linToBoundary_.resize(l*b);
        boundaryToLin_.resize(b*l);cross_.resize(b*b);
        linRhs_.resize(l);linAnswer_.resize(l);linCachedSource_.resize(l);
        reduced_.resize(b*b);reducedRhs_.resize(b);reducedDelta_.resize(b);
        preparedWork_.resize(l);linPivots_.resize(l);
        schurPattern_.resize(b);
        compactIndex_.assign(dimension_,-1);
        for(int i=0;i<int(b);++i)compactIndex_[boundary_[i]]=i;
        schurBase_.resize(b*b); compactMatrix_.resize(b*b);
        compactResidual_.resize(b); compactWork_.resize(b*b);
        compactRhs_.resize(b); compactStep_.resize(b);compactSource_.resize(b);
    }
    void configureDecimator() {
        const int taps=oversample_==1 ? 1 : 63;
        decimatorKernel_.assign(taps,0.0);
        decimatorHistory_.assign(taps,0.0);
        if (taps==1) {decimatorKernel_[0]=1.; return;}
        constexpr double pi=3.14159265358979323846264;
        const double fc=0.45/double(oversample_); // cycles/oversampled sample
        double sum=0.0;
        for(int i=0;i<taps;++i) {
            const double x=i-(taps-1)*0.5;
            const double sinc=std::abs(x)<1e-10 ? 2*fc : std::sin(2*pi*fc*x)/(pi*x);
            const double win=0.42-0.5*std::cos(2*pi*i/(taps-1))+0.08*std::cos(4*pi*i/(taps-1));
            decimatorKernel_[i]=sinc*win; sum += decimatorKernel_[i];
        }
        for(auto &v: decimatorKernel_) v/=sum;
    }
    int idx(int n) const { return n < kFirstFree ? -1 : n-kFirstFree; }
    double voltage(int n) const {
        if(n==kGnd) return 0.;
        if(n==kVcc) return supplyVoltage_;
        if(n==kVref) return vref();
        if(n==kInput) return fixedInput_;
        if(n==kSource2) return fixedSource2_;
        if(n==kSource3) return fixedSource3_;
        return unknowns_[n-kFirstFree];
    }
    // Exact existing clamped exponential semantics, avoiding a libm call
    // when transistor/diode junctions are hard reverse-biased or at the cap.
    static double boundedExp(double v) {
        if(v<=-40.) return 4.248354255291589e-18;
        if(v>=35.) return 1586013452313430.8;
        return std::exp(v);
    }
    // 7/7 Pade tanh: within |x|<=1.5, maximum error <3.4e-10
    // over a dense analytical sweep; outside, use the exact libm tanh.
    // This is an optional numerical approximation, not device emulation.
    double opAmpTanh(double arg) const {
        if(arg>=20.) return 1.;
        if(arg<=-20.) return -1.;
        if(!fastNonlinearMath_ || std::abs(arg)>1.5) return std::tanh(arg);
        const double a2=arg*arg;
        const double num=135135. + a2*(17325. + a2*(378. + a2));
        const double den=135135. + a2*(62370. + a2*(3150. + 28.*a2));
        return arg * (num/den);
    }
    // Stamp a two-terminal current flowing a->b, derivatives w.r.t a,b.
    void stampBranch(int a,int b,double current,double g) {
        const int ai=idx(a), bi=idx(b);
        if(ai>=0) {
            residual_[ai] += current;
            jac_[ai*dimension_+ai] += g;
            if(bi>=0) jac_[ai*dimension_+bi] -= g;
        }
        if(bi>=0) {
            residual_[bi] -= current;
            jac_[bi*dimension_+bi] += g;
            if(ai>=0) jac_[bi*dimension_+ai] -= g;
        }
    }
    // Cached device-node indices remove repeated node tests and mappings
    // from the hot nonlinear core.  These are Ebers--Moll derivatives,
    // algebraically identical to the v4 six-term nodal formulation.
    void stampDiode(const Diode &d) {
        const double arg=(voltage(d.anode)-voltage(d.cathode))/(d.emission*d.thermal);
        const double e=boundedExp(arg);
        const double current=d.saturation*(e-1.);
        const double g=d.saturation*e/(d.emission*d.thermal);
        const int ai=d.ai,ki=d.ki;
        if(ai>=0) {
            residual_[ai]+=current;
            jac_[ai*dimension_+ai]+=g;
            if(ki>=0) jac_[ai*dimension_+ki]-=g;
        }
        if(ki>=0) {
            residual_[ki]-=current;
            jac_[ki*dimension_+ki]+=g;
            if(ai>=0) jac_[ki*dimension_+ai]-=g;
        }
    }
    // Consolidated BJT node stamps. No calls to npnTerm(), no repeated
    // idx() lookups, and the exponential is evaluated only once per junction.
    void stampTransistor(const NPN &q) {
        const double vt=thermalVoltage();
        const double feb=boundedExp((voltage(q.b)-voltage(q.e))/vt);
        const double fcb=boundedExp((voltage(q.b)-voltage(q.c))/vt);
        const double ibe=q.is*(feb-1.),ibc=q.is*(fcb-1.);
        const double gbe=q.is*feb/vt,gbc=q.is*fcb/vt;
        const double af=q.alphaF,ar=q.alphaR;
        const int ci=q.ci,bi=q.bi,ei=q.ei,dim=dimension_;
        if(ci>=0) {
            residual_[ci]+=af*ibe-ibc;
            const int row=ci*dim;
            if(bi>=0) jac_[row+bi]+=af*gbe-gbc;
            if(ei>=0) jac_[row+ei]-=af*gbe;
            jac_[row+ci]+=gbc;
        }
        if(ei>=0) {
            residual_[ei]+=-ibe+ar*ibc;
            const int row=ei*dim;
            if(bi>=0) jac_[row+bi]+=-gbe+ar*gbc;
            jac_[row+ei]+=gbe;
            if(ci>=0) jac_[row+ci]-=ar*gbc;
        }
        if(bi>=0) {
            const double bf=1.-af,br=1.-ar;
            residual_[bi]+=bf*ibe+br*ibc;
            const int row=bi*dim;
            jac_[row+bi]+=bf*gbe+br*gbc;
            if(ei>=0) jac_[row+ei]-=bf*gbe;
            if(ci>=0) jac_[row+ci]-=br*gbc;
        }
    }
    // Stamp the Jacobian of the passive network only after a coefficient or
    // DC/transient mode changes. No nonlinear stamp touches this cache.
    void prepareLinearMatrix(double dt) {
        if(cachedLinearRevision_==linearRevision_ && cachedLinearDt_==dt)return;
        std::fill(linearJac_.begin(),linearJac_.end(),0.);
        const auto add=[&](int a,int b,double g) {
            const int ai=idx(a),bi=idx(b);
            if(ai>=0){linearJac_[ai*dimension_+ai]+=g;
                if(bi>=0)linearJac_[ai*dimension_+bi]-=g;}
            if(bi>=0){linearJac_[bi*dimension_+bi]+=g;
                if(ai>=0)linearJac_[bi*dimension_+ai]-=g;}
        };
        for(const auto&r:resistors_)add(r.a,r.b,r.conductance);
        if(dt>0.)for(const auto&c:caps_)add(c.a,c.b,c.conductance);
        for(int i=0;i<numFreeNodes_;++i)linearJac_[i*dimension_+i]+=1e-12;
        linearEntryCount_=0;
        for(int i=0;i<dimension_;++i)
            for(int j=0;j<dimension_;++j) {
                const double x=linearJac_[i*dimension_+j];
                if(x!=0.)linearEntries_[linearEntryCount_++]={i,j,x};
            }
        cachedLinearRevision_=linearRevision_;
        cachedLinearDt_=dt;
    }
    // Account for capacitor history and all fixed-node voltages once per
    // sample. They change the RHS, but not the passive Jacobian.
    void prepareLinearSource(double dt) {
        std::fill(linearSource_.begin(),linearSource_.end(),0.);
        const auto current=[&](int a,int b,double g,double offset) {
            const int ai=idx(a),bi=idx(b);
            if(ai>=0){linearSource_[ai]+=offset;
                if(bi<0)linearSource_[ai]-=g*voltage(b);}
            if(bi>=0){linearSource_[bi]-=offset;
                if(ai<0)linearSource_[bi]-=g*voltage(a);}
        };
        for(const auto &r:resistors_)current(r.a,r.b,r.conductance,0.);
        if(dt>0.)for(const auto &c:caps_)
            current(c.a,c.b,c.conductance,-c.previousCharge/dt);
        for(int i=0;i<numFreeNodes_;++i)linearSource_[i]-=1e-12*vref();
    }
    // The op-amp constants and jitter state do not change within a Newton
    // solve. Compute them per internal audio sample instead of per trial.
    void prepareOpAmpEval(double dt) {
        const double reference=vref();
        for(std::size_t i=0;i<amps_.size();++i) {
            const auto &a=amps_[i];const auto &p=a.parameters;
            auto &e=ampEval_[i];
            e.reference=reference;
            e.swing=std::min(p.outputSwingVolts,
                std::max(0.01,std::min(reference,supplyVoltage_-reference)-0.01));
            e.gainScale=p.openLoopGain/e.swing;
            e.offset=p.offsetVolts+a.offsetJitterVolts*a.offsetJitter.current;
            const double poleRad=p.gainBandwidthHz>0 ?
                2.0*3.141592653589793*p.gainBandwidthHz/p.openLoopGain : 0.;
            e.alpha=(dt>0. && poleRad>0.) ? dt*poleRad/(1.+dt*poleRad) : 1.0;
            e.slewDt=p.slewRateVPerUs*a.slewJitter.multiplier()*1e6*dt;
        }
    }
    void evaluate(double dt) {
        // Called on every Newton/line-search trial. The linear part is a
        // sparse matrix-vector multiply and one contiguous Jacobian copy.
        std::copy(linearJac_.begin(),linearJac_.end(),jac_.begin());
        std::copy(linearSource_.begin(),linearSource_.end(),residual_.begin());
        for(int k=0;k<linearEntryCount_;++k) {
            const auto&e=linearEntries_[k];
            residual_[e.row]+=e.g*unknowns_[e.col];
        }
        for(const auto &d:diodes_) stampDiode(d);
        for(const auto &q:npns_) stampTransistor(q);
        for (int i=0;i<static_cast<int>(amps_.size());++i) {
            const auto &a=amps_[i];
            const int extra=numFreeNodes_+i, out=idx(a.out);
            // Finite open-loop gain, output swing, dominant pole and slew.
            // Backward Euler integrated at oversampled rate; op-amp source
            // remains ideal-Zout so segment partition remains valid.
            const auto &p=a.parameters;
            const auto &e=ampEval_[i];
            const double swing=e.swing;
            const double vdiff=voltage(a.plus)-voltage(a.minus)+e.offset;
            const double arg=e.gainScale*vdiff;
            // tanh(x) rounds to +/-1 in IEEE binary64 for |x| >= 20.
            // Avoid a libm call at those rails, without approximating the
            // op-amp transfer curve where it has a measurable slope.
            const double transfer = opAmpTanh(arg);
            const double gainSlope = p.openLoopGain*(1.-transfer*transfer);
            const double unconstrained=e.reference + swing*transfer;
            const double dcDerivative=gainSlope;
            double target=unconstrained,deriv=dcDerivative;
            if(dt>0) {
                double alpha=e.alpha;
                // Bias-dependent, causal overload release: only slows movement
                // away from a rail while saturation memory is decaying.
                if(p.overloadRecoveryMs>0 && p.overloadRecoveryDepth>0 && a.overloadMemory>0) {
                    const double prev=a.previousOutput-e.reference;
                    const double goal=unconstrained-e.reference;
                    if(prev*goal>=0 && std::abs(goal)<std::abs(prev))
                        alpha/=1.+9.*p.overloadRecoveryDepth*a.overloadMemory;
                    else if(prev*goal<0)
                        alpha/=1.+9.*p.overloadRecoveryDepth*a.overloadMemory;
                }
                const double wanted=alpha*(unconstrained-a.previousOutput);
                const double slew=e.slewDt;
                if(slew>0) {
                    const double q=1.+std::abs(wanted)/slew;
                    target=a.previousOutput+wanted/q;
                    deriv=alpha*dcDerivative/(q*q);
                } else {
                    target=a.previousOutput+wanted;
                    deriv=alpha*dcDerivative;
                }
            }
            residual_[extra]=voltage(a.out)-target;
            jac_[extra*dimension_+out] += 1.;
            if(idx(a.plus)>=0) jac_[extra*dimension_+idx(a.plus)] -= deriv;
            if(idx(a.minus)>=0) jac_[extra*dimension_+idx(a.minus)] += deriv;
            // Input-bias current drawn from both pins into the reference rail.
            stampBranch(a.plus,kVref,p.inputBiasAmps+p.inputOffsetAmps,0.);
            stampBranch(a.minus,kVref,p.inputBiasAmps,0.);
            residual_[out] += unknowns_[extra];
            jac_[out*dimension_+extra] += 1.;
        }
    }
    // Dense reference and fallback solver: unchanged v3 equations.
    bool solveDense() {
        std::copy(jac_.begin(),jac_.end(),work_.begin());
        for(int i=0;i<dimension_;++i) rhs_[i]=-residual_[i];
        for(int k=0;k<dimension_;++k) {
            int pivot=k;
            double pv=std::abs(work_[k*dimension_+k]);
            for(int row=k+1;row<dimension_;++row) {
                double trial=std::abs(work_[row*dimension_+k]);
                if(trial>pv) {pv=trial;pivot=row;}
            }
            if(pv<1e-22 || !std::isfinite(pv)) return false;
            if(pivot!=k) {
                for(int j=k;j<dimension_;++j)
                    std::swap(work_[pivot*dimension_+j],work_[k*dimension_+j]);
                std::swap(rhs_[pivot],rhs_[k]);
            }
            const double inverse=1.0/work_[k*dimension_+k];
            for(int row=k+1;row<dimension_;++row) {
                const double factor=work_[row*dimension_+k]*inverse;
                if(std::abs(factor)<1e-28) continue;
                work_[row*dimension_+k]=0.;
                for(int j=k+1;j<dimension_;++j)
                    work_[row*dimension_+j] -= factor*work_[k*dimension_+j];
                rhs_[row] -= factor*rhs_[k];
            }
        }
        for(int i=dimension_-1;i>=0;--i) {
            double b=rhs_[i];
            for(int j=i+1;j<dimension_;++j) b-=work_[i*dimension_+j]*delta_[j];
            delta_[i]=b/work_[i*dimension_+i];
        }
        return true;
    }

    // The resistor/capacitor-only block J_LL of the Jacobian is constant
    // throughout a Newton solve, even while transistor and op-amp slopes
    // change at every trial. Factorize that block only once per time step.
    bool prepareReduced(double dt) {
        // Rebuilding the Schur factorization at audio rate is slower than
        // ordinary dense LU on these small networks. Auto switches the
        // affected circuit segment back to dense if an R/C is jittering.
        if(linearSolver_==LinearSolver::Dense ||
           (linearSolver_==LinearSolver::Auto && passiveJitterEnabled_) ||
           linear_.empty() || boundary_.empty()) {
            schurReady_=false;
            return false;
        }
        // The Jacobian's linear block and its border change only when an RC
        // coefficient changes or the DC/transient discretization changes.
        if(schurReady_ && preparedRevision_==linearRevision_ && preparedDt_==dt)return true;
        schurReady_=false;
        const int l=static_cast<int>(linear_.size());
        const int b=static_cast<int>(boundary_.size());
        for(int i=0;i<l;++i) {
            for(int j=0;j<l;++j)linLU_[i*l+j]=jac_[linear_[i]*dimension_+linear_[j]];
        }
        // Store permutation steps, rather than a dense inverse of J_LL.
        for(int k=0;k<l;++k) {
            int pivot=k;
            double pv=std::abs(linLU_[k*l+k]);
            for(int i=k+1;i<l;++i) {
                double t=std::abs(linLU_[i*l+k]);
                if(t>pv) {pv=t;pivot=i;}
            }
            if(pv<1e-22 || !std::isfinite(pv))return false;
            linPivots_[k]=pivot;
            if(pivot!=k)for(int j=0;j<l;++j)std::swap(linLU_[pivot*l+j],linLU_[k*l+j]);
            for(int i=k+1;i<l;++i) {
                linLU_[i*l+k]/=linLU_[k*l+k];
                double a=linLU_[i*l+k];
                for(int j=k+1;j<l;++j)linLU_[i*l+j]-=a*linLU_[k*l+j];
            }
        }
        // K = J_LL^-1 J_LB, constant throughout this time step.
        for(int j=0;j<b;++j) {
            for(int i=0;i<l;++i)
                linRhs_[i]=jac_[linear_[i]*dimension_+boundary_[j]];
            solvePreparedLinear(linRhs_,preparedWork_);
            for(int i=0;i<l;++i)linToBoundary_[i*b+j]=preparedWork_[i];
        }
        // The cross-product J_BL*K is fixed over all Newton iterations.
        for(int i=0;i<b;++i) {
            for(int j=0;j<l;++j)
                boundaryToLin_[i*l+j]=jac_[boundary_[i]*dimension_+linear_[j]];
            for(int j=0;j<b;++j) {
                double v=0.;
                for(int k=0;k<l;++k)v+=boundaryToLin_[i*l+k]*linToBoundary_[k*b+j];
                cross_[i*b+j]=v;
            }
        }
        // Structural pattern of the Schur complement, including all
        // possible semiconductor/op-amp Jacobian entries even if the
        // current operating point has zero slope due to saturation.
        for(int i=0;i<b;++i) {
            std::uint32_t mask=0;
            const int row=boundary_[i];
            for(int j=0;j<b;++j) {
                if(jac_[row*dimension_+boundary_[j]]-cross_[i*b+j]!=0.)
                    mask|=(std::uint32_t(1)<<j);
            }
            schurPattern_[i]=mask;
        }
        const auto mark=[&](int r,int c) {
            if(r<0||c<0)return;
            for(int i=0;i<b;++i)if(boundary_[i]==r) {
                for(int j=0;j<b;++j)if(boundary_[j]==c)
                    schurPattern_[i]|=(std::uint32_t(1)<<j);
                break;
            }
        };
        for(const auto& d:diodes_) {
            const int a=idx(d.anode),k=idx(d.cathode);
            mark(a,a);mark(a,k);mark(k,a);mark(k,k);
        }
        for(const auto& q:npns_) {
            const int c=idx(q.c),e=idx(q.e),base=idx(q.b);
            for(int r:{c,e,base})for(int col:{c,e,base})mark(r,col);
        }
        for(int k=0;k<int(amps_.size());++k) {
            const auto& a=amps_[k];const int x=numFreeNodes_+k;
            mark(x,idx(a.plus));mark(x,idx(a.minus));mark(x,idx(a.out));
            mark(idx(a.out),x);
        }
        schurReady_=true;
        preparedRevision_=linearRevision_;preparedDt_=dt;
        ++schurRebuilds_;
        return true;
    }
    // Pivoted forward/back substitution. in and out may not alias.
    void solvePreparedLinear(const std::vector<double>& in,std::vector<double>& out) const {
        const int l=static_cast<int>(linear_.size());
        for(int i=0;i<l;++i)out[i]=in[i];
        for(int i=0;i<l;++i) {
            if(linPivots_[i]!=i) std::swap(out[i],out[linPivots_[i]]);
            for(int j=i+1;j<l;++j)out[j]-=linLU_[j*l+i]*out[i];
        }
        for(int i=l-1;i>=0;--i) {
            for(int j=i+1;j<l;++j)out[i]-=linLU_[i*l+j]*out[j];
            out[i]/=linLU_[i*l+i];
        }
    }
    // Passive internal nodes have no nonlinear stamps. Their exact solution
    // contribution from capacitor charge, supplies, and input voltage is
    // constant over all Newton iterations of the current audio step.
    // Compute J_LL^-1 * source_L only once per sample, not on every iteration.
    void prepareReducedSource() {
        if(!schurReady_)return;
        const int l=static_cast<int>(linear_.size());
        for(int i=0;i<l;++i)linRhs_[i]=linearSource_[linear_[i]];
        solvePreparedLinear(linRhs_,linCachedSource_);
    }
    bool solveReduced(bool sparse=false) {
        if(!schurReady_)return false;
        const int l=static_cast<int>(linear_.size());
        const int b=static_cast<int>(boundary_.size());
        // -J_LL^-1 R_L = -u_L - J_LL^-1 J_LB u_B - J_LL^-1 source_L.
        // No factorization or triangular substitution in this Newton loop.
        if(!reducedAffineValid_) {
            for(int i=0;i<l;++i) {
                double rhs=-unknowns_[linear_[i]]-linCachedSource_[i];
                for(int j=0;j<b;++j)rhs-=linToBoundary_[i*b+j]*unknowns_[boundary_[j]];
                linAnswer_[i]=rhs;
            }
        }
        for(int i=0;i<b;++i) {
            double val=-residual_[boundary_[i]];
            for(int k=0;k<l;++k)val-=boundaryToLin_[i*l+k]*linAnswer_[k];
            reducedRhs_[i]=val;
            for(int j=0;j<b;++j)
                reduced_[i*b+j]=jac_[boundary_[i]*dimension_+boundary_[j]]-cross_[i*b+j];
        }
        // Eliminate the nonlinear-port / op-amp-current system.
        // Its sparsity is fixed by the circuit graph, apart from numerical
        // cancellations and pivot-dependent fill. Track active columns in
        // a 32-bit row mask; exact partial pivoting is preserved.
        // Dense remains an independent numerical reference path.
        if(sparse && b<=32) {
            std::array<std::uint32_t,32> masks{};
            for(int row=0;row<b;++row)masks[row]=schurPattern_[row];
            for(int k=0;k<b;++k) {
                int pivot=k;
                double pv=std::abs(reduced_[k*b+k]);
                for(int i=k+1;i<b;++i) {
                    if(!(masks[i]&(std::uint32_t(1)<<k)))continue;
                    const double q=std::abs(reduced_[i*b+k]);
                    if(q>pv){pv=q;pivot=i;}
                }
                if(pv<1e-22 || !std::isfinite(pv))return false;
                if(pivot!=k) {
                    for(int j=k;j<b;++j)std::swap(reduced_[pivot*b+j],reduced_[k*b+j]);
                    std::swap(reducedRhs_[pivot],reducedRhs_[k]);
                    std::swap(masks[pivot],masks[k]);
                }
                const double inv=1./reduced_[k*b+k];
                const auto rowMask=masks[k];
                for(int i=k+1;i<b;++i) {
                    if(!(masks[i]&(std::uint32_t(1)<<k)))continue;
                    const double factor=reduced_[i*b+k]*inv;
                    if(std::abs(factor)<1e-28)continue;
                    reduced_[i*b+k]=0.;
                    masks[i]&=~(std::uint32_t(1)<<k);
                    // Only structurally present entries in the pivot row
                    // can update this trailing row. Fill is added to mask.
                    std::uint32_t remaining=rowMask&(~std::uint32_t(0)<<(k+1));
                    while(remaining) {
#if defined(__GNUC__) || defined(__clang__)
                        const int j=__builtin_ctz(remaining);
#else
                        int j=k+1;
                        while(!(remaining&(std::uint32_t(1)<<j)))++j;
#endif
                        remaining&=remaining-1;
                        reduced_[i*b+j]-=factor*reduced_[k*b+j];
                    }
                    masks[i]|=(rowMask&(~std::uint32_t(0)<<(k+1)));
                    reducedRhs_[i]-=factor*reducedRhs_[k];
                }
            }
        } else {
            for(int k=0;k<b;++k) {
                int pivot=k;
                double pv=std::abs(reduced_[k*b+k]);
                for(int i=k+1;i<b;++i) {
                    const double q=std::abs(reduced_[i*b+k]);
                    if(q>pv) {pv=q;pivot=i;}
                }
                if(pv<1e-22 || !std::isfinite(pv))return false;
                if(pivot!=k) {
                    for(int j=k;j<b;++j)std::swap(reduced_[pivot*b+j],reduced_[k*b+j]);
                    std::swap(reducedRhs_[pivot],reducedRhs_[k]);
                }
                const double inv=1./reduced_[k*b+k];
                for(int i=k+1;i<b;++i) {
                    const double factor=reduced_[i*b+k]*inv;
                    if(std::abs(factor)<1e-28)continue;
                    reduced_[i*b+k]=0.;
                    for(int j=k+1;j<b;++j)reduced_[i*b+j]-=factor*reduced_[k*b+j];
                    reducedRhs_[i]-=factor*reducedRhs_[k];
                }
            }
        }
        for(int i=b-1;i>=0;--i) {
            double v=reducedRhs_[i];
            for(int j=i+1;j<b;++j)v-=reduced_[i*b+j]*reducedDelta_[j];
            reducedDelta_[i]=v/reduced_[i*b+i];
            if(!std::isfinite(reducedDelta_[i]))return false;
        }
        for(int i=0;i<b;++i)delta_[boundary_[i]]=reducedDelta_[i];
        for(int i=0;i<l;++i) {
            double v=linAnswer_[i];
            for(int j=0;j<b;++j)v-=linToBoundary_[i*b+j]*reducedDelta_[j];
            if(!std::isfinite(v))return false;
            delta_[linear_[i]]=v;
        }
        return true;
    }
    bool solveLinear() {
        if(linearSolver_!=LinearSolver::Dense && schurReady_) {
            const bool sparse=linearSolver_==LinearSolver::SparseReduced ||
                              linearSolver_==LinearSolver::SparseAffineReduced;
            if(solveReduced(sparse)) {
                lastLinearUsedReduced_=true;
                return true;
            }
            ++reducedFallbacks_;
        }
        lastLinearUsedReduced_=false;
        reducedAffineValid_=false;
        return solveDense();
    }
    double merit() const {
        double m=0.0;
        for(int i=0;i<numFreeNodes_;++i)
            m=std::max(m,std::abs(residual_[i])*20000.);
        for(int i=numFreeNodes_;i<dimension_;++i)
            m=std::max(m,std::abs(residual_[i]));
        return m;
    }
    double portDelta(int a, int b) const {
        return (idx(a)<0 ? 0. : delta_[idx(a)]) - (idx(b)<0 ? 0. : delta_[idx(b)]);
    }
    // Eliminate every passive-only node before the nonlinear iteration.
    // The Schur complement is constant until component coefficients change.
    // A fixed circuit matrix is still used: no topology approximation.
    bool prepareCompact(double dt) {
        prepareLinearMatrix(dt);
        prepareLinearSource(dt);
        prepareOpAmpEval(dt);
        // Schur setup consumes jac_ only on topology/RC changes.
        if(!schurReady_ || preparedRevision_!=linearRevision_ || preparedDt_!=dt)
            std::copy(linearJac_.begin(),linearJac_.end(),jac_.begin());
        if(!prepareReduced(dt))return false;
        prepareReducedSource();
        const int b=int(boundary_.size()),l=int(linear_.size());
        if(compactRevision_!=linearRevision_ || compactDt_!=dt) {
            for(int i=0;i<b;++i) {
                const int row=boundary_[i];
                for(int j=0;j<b;++j)
                    schurBase_[i*b+j]=linearJac_[row*dimension_+boundary_[j]]-cross_[i*b+j];
            }
            compactRevision_=linearRevision_;compactDt_=dt;
        }
        for(int i=0;i<b;++i) {
            const int row=boundary_[i];
            double src=linearSource_[row];
            for(int j=0;j<l;++j)src-=boundaryToLin_[i*l+j]*linCachedSource_[j];
            compactSource_[i]=src;
        }
        return true;
    }
    void restorePassive() {
        const int b=int(boundary_.size());
        for(int i=0;i<int(linear_.size());++i) {
            double v=-linCachedSource_[i];
            for(int j=0;j<b;++j)v-=linToBoundary_[i*b+j]*unknowns_[boundary_[j]];
            unknowns_[linear_[i]]=v;
        }
    }
    // The same Ebers-Moll, diode and op-amp equations as evaluate(), stamped
    // directly into a small Schur complement. All nonlinear nodes/rows were
    // classified boundary_ at construct time.
    void evaluateCompact(double dt) {
        const int b=int(boundary_.size());
        std::copy(schurBase_.begin(),schurBase_.end(),compactMatrix_.begin());
        for(int i=0;i<b;++i) {
            double v=compactSource_[i];
            for(int j=0;j<b;++j)v+=schurBase_[i*b+j]*unknowns_[boundary_[j]];
            compactResidual_[i]=v;
        }
        const auto add=[&](int row,int col,double g){
            if(row>=0 && col>=0)compactMatrix_[row*b+col]+=g;
        };
        const auto addR=[&](int row,double x){if(row>=0)compactResidual_[row]+=x;};
        for(const auto &d:diodes_) {
            const double arg=(voltage(d.anode)-voltage(d.cathode))/(d.emission*d.thermal);
            const double ex=boundedExp(arg);
            const double current=d.saturation*(ex-1.);
            const double g=d.saturation*ex/(d.emission*d.thermal);
            const int a=d.ai<0?-1:compactIndex_[d.ai];
            const int k=d.ki<0?-1:compactIndex_[d.ki];
            addR(a,current);addR(k,-current);
            add(a,a,g);add(a,k,-g);add(k,k,g);add(k,a,-g);
        }
        const double vt=thermalVoltage();
        for(const auto&q:npns_) {
            const double feb=boundedExp((voltage(q.b)-voltage(q.e))/vt);
            const double fcb=boundedExp((voltage(q.b)-voltage(q.c))/vt);
            const double ibe=q.is*(feb-1.),ibc=q.is*(fcb-1.);
            const double gbe=q.is*feb/vt,gbc=q.is*fcb/vt;
            const double af=q.alphaF,ar=q.alphaR,bf=1.-af,br=1.-ar;
            const int c=q.ci<0?-1:compactIndex_[q.ci];
            const int bb=q.bi<0?-1:compactIndex_[q.bi];
            const int e=q.ei<0?-1:compactIndex_[q.ei];
            addR(c,af*ibe-ibc);add(c,bb,af*gbe-gbc);add(c,e,-af*gbe);add(c,c,gbc);
            addR(e,-ibe+ar*ibc);add(e,bb,-gbe+ar*gbc);add(e,e,gbe);add(e,c,-ar*gbc);
            addR(bb,bf*ibe+br*ibc);add(bb,bb,bf*gbe+br*gbc);
            add(bb,e,-bf*gbe);add(bb,c,-br*gbc);
        }
        for(int i=0;i<int(amps_.size());++i) {
            const auto&a=amps_[i]; const auto&p=a.parameters;const auto&e=ampEval_[i];
            const int extra=compactIndex_[numFreeNodes_+i];
            const int out=compactIndex_[idx(a.out)];
            const int plus=idx(a.plus)<0?-1:compactIndex_[idx(a.plus)];
            const int minus=idx(a.minus)<0?-1:compactIndex_[idx(a.minus)];
            const double vdiff=voltage(a.plus)-voltage(a.minus)+e.offset;
            const double arg=e.gainScale*vdiff;
            const double tr=opAmpTanh(arg);
            const double unconstrained=e.reference+e.swing*tr;
            const double gainSlope=p.openLoopGain*(1.-tr*tr);
            double target=unconstrained,deriv=gainSlope;
            if(dt>0) {
                double alpha=e.alpha;
                if(p.overloadRecoveryMs>0 && p.overloadRecoveryDepth>0 && a.overloadMemory>0) {
                    const double prev=a.previousOutput-e.reference;
                    const double goal=unconstrained-e.reference;
                    if(prev*goal>=0 && std::abs(goal)<std::abs(prev))
                        alpha/=1.+9.*p.overloadRecoveryDepth*a.overloadMemory;
                    else if(prev*goal<0)
                        alpha/=1.+9.*p.overloadRecoveryDepth*a.overloadMemory;
                }
                const double wanted=alpha*(unconstrained-a.previousOutput);
                const double slew=e.slewDt;
                if(slew>0) {
                    const double q=1.+std::abs(wanted)/slew;
                    target=a.previousOutput+wanted/q;
                    deriv=alpha*gainSlope/(q*q);
                } else {target=a.previousOutput+wanted;deriv=alpha*gainSlope;}
            }
            compactResidual_[extra]=voltage(a.out)-target;
            add(extra,out,1.);add(extra,plus,-deriv);add(extra,minus,deriv);
            addR(plus,p.inputBiasAmps+p.inputOffsetAmps);
            addR(minus,p.inputBiasAmps);
            addR(out,unknowns_[numFreeNodes_+i]);add(out,extra,1.);
        }
    }
    bool compactLinearStep() {
        const int b=int(boundary_.size());
        std::copy(compactMatrix_.begin(),compactMatrix_.end(),compactWork_.begin());
        for(int i=0;i<b;++i)compactRhs_[i]=-compactResidual_[i];
        for(int k=0;k<b;++k) {
            int pivot=k;double pv=std::abs(compactWork_[k*b+k]);
            for(int i=k+1;i<b;++i) {
                const double q=std::abs(compactWork_[i*b+k]);
                if(q>pv){pv=q;pivot=i;}
            }
            if(pv<1e-22 || !std::isfinite(pv))return false;
            if(pivot!=k) {
                for(int j=k;j<b;++j)std::swap(compactWork_[pivot*b+j],compactWork_[k*b+j]);
                std::swap(compactRhs_[pivot],compactRhs_[k]);
            }
            const double inverse=1./compactWork_[k*b+k];
            for(int i=k+1;i<b;++i) {
                const double factor=compactWork_[i*b+k]*inverse;
                if(std::abs(factor)<1e-28)continue;
                compactWork_[i*b+k]=0.;
                for(int j=k+1;j<b;++j)compactWork_[i*b+j]-=factor*compactWork_[k*b+j];
                compactRhs_[i]-=factor*compactRhs_[k];
            }
        }
        for(int i=b-1;i>=0;--i) {
            double v=compactRhs_[i];
            for(int j=i+1;j<b;++j)v-=compactWork_[i*b+j]*compactStep_[j];
            compactStep_[i]=v/compactWork_[i*b+i];
            if(!std::isfinite(compactStep_[i]))return false;
        }
        for(int i=0;i<b;++i)delta_[boundary_[i]]=compactStep_[i];
        for(int i=0;i<int(linear_.size());++i) {
            double v=0.;
            for(int j=0;j<b;++j)v-=linToBoundary_[i*b+j]*compactStep_[j];
            delta_[linear_[i]]=v;
        }
        return true;
    }
    double compactMerit() const {
        double merit=0.;
        for(int i=0;i<int(boundary_.size());++i) {
            const double scale=boundary_[i]<numFreeNodes_?20000.:1.;
            merit=std::max(merit,std::abs(compactResidual_[i])*scale);
        }
        return merit;
    }
    bool solveCompact(double dt,int maxIter) {
        if(!prepareCompact(dt))return false;
        evaluateCompact(dt);
        lastConverged_=false;
        for(int iter=0;iter<maxIter;++iter) {
            double maxF=0.,maxOP=0.;
            for(int i=0;i<int(boundary_.size());++i) {
                const double v=std::abs(compactResidual_[i]);
                if(boundary_[i]<numFreeNodes_)maxF=std::max(maxF,v);
                else maxOP=std::max(maxOP,v);
            }
            if(maxF<3e-9 && maxOP<3e-6) {
                lastConverged_=true;lastIterations_=iter+1;
                totalNewtonIterations_+=iter+1;++convergedSteps_;
                peakNewtonIterations_=std::max(peakNewtonIterations_,iter+1);
                // Nonlinear devices use only boundary potentials. Reconstruct
                // passive nodes once after convergence, not for each trial.
                restorePassive();
                return true;
            }
            const double initialMerit=compactMerit();
            if(!compactLinearStep())return false;
            double limit=1.;
            for(int i=0;i<numFreeNodes_;++i)
                if(std::abs(delta_[i])>0.35)limit=std::min(limit,.35/std::abs(delta_[i]));
            for(const auto&q:npns_)
                for(double dv:{portDelta(q.b,q.e),portDelta(q.b,q.c)})
                    if(std::abs(dv)>.18)limit=std::min(limit,.18/std::abs(dv));
            for(const auto&d:diodes_) {
                const double dv=portDelta(d.anode,d.cathode);
                if(std::abs(dv)>.18)limit=std::min(limit,.18/std::abs(dv));
            }
            for(const auto&a:amps_)if(a.parameters.gainBandwidthHz>0 || a.parameters.slewRateVPerUs>0) {
                const double dv=portDelta(a.plus,a.minus);
                const double vin=voltage(a.plus)-voltage(a.minus);
                if(std::abs(vin)<.02 && std::abs(dv)>.004)
                    limit=std::min(limit,.004/std::abs(dv));
                const double knee=std::max(1e-6,8.*a.parameters.outputSwingVolts/a.parameters.openLoopGain);
                if(std::abs(vin)>knee && vin*(vin+dv)<0.)
                    limit=std::min(limit,(std::abs(vin)-.5*knee)/std::abs(dv));
            }
            for(int i=0;i<int(boundary_.size());++i)old_[boundary_[i]]=unknowns_[boundary_[i]];
            double trial=limit;
            for(int attempt=0;attempt<15;++attempt) {
                for(int i=0;i<int(boundary_.size());++i) {
                    const int at=boundary_[i];unknowns_[at]=old_[at]+trial*delta_[at];
                }
                evaluateCompact(dt);
                if(compactMerit()<=initialMerit*1.000001 || attempt==14)break;
                trial*=.5;
            }
        }
        return false;
    }
    bool solve(double dt,int maxIter) {
        if(linearSolver_==LinearSolver::CompactNewton && dt>0. && passiveJitterEnabled_) {
            const auto saved=linearSolver_;
            linearSolver_=LinearSolver::Auto;
            const bool ok=solve(dt,maxIter);
            linearSolver_=saved;
            return ok;
        }
        if(linearSolver_==LinearSolver::CompactNewton && dt>0.) {
            // Preserve previous state for a deterministic fallback if an
            // experimental reduced iteration cannot converge.
            // Preallocated backup; no allocation in process().
            std::copy(unknowns_.begin(),unknowns_.end(),compactBackup_.begin());
            if(solveCompact(dt,maxIter))return true;
            ++compactFallbacks_;
            std::copy(compactBackup_.begin(),compactBackup_.end(),unknowns_.begin());
            const auto saved=linearSolver_;
            linearSolver_=LinearSolver::Auto;
            const bool ok=solve(dt,maxIter);
            linearSolver_=saved;
            return ok;
        }
        lastConverged_=false;
        // A backtracking line search has already evaluated its accepted
        // state. The next iteration must not evaluate the same state twice.
        reducedAffineValid_=false;
        prepareLinearMatrix(dt);
        prepareLinearSource(dt);
        prepareOpAmpEval(dt);
        evaluate(dt);
        prepareReduced(dt);
        prepareReducedSource();
        for(int iter=0;iter<maxIter;++iter) {
            double maxF=0.;
            for(int i=0;i<numFreeNodes_;++i) maxF=std::max(maxF,std::abs(residual_[i]));
            double maxOP=0.;
            for(int i=numFreeNodes_;i<dimension_;++i) maxOP=std::max(maxOP,std::abs(residual_[i]));
            if(maxF<3e-9 && maxOP<3e-6) {
                lastConverged_=true;lastIterations_=iter+1;
                totalNewtonIterations_+=iter+1;
                ++convergedSteps_;
                peakNewtonIterations_=std::max(peakNewtonIterations_,iter+1);
                return true;
            }
            const double initialMerit=merit();
            if(!solveLinear()) break;
            double limit=1.0;
            for(int i=0;i<numFreeNodes_;++i)
                if(std::abs(delta_[i])>0.35) limit=std::min(limit,0.35/std::abs(delta_[i]));
            // SPICE-style nonlinear port voltage limiting (junctions <= 180mV / step).
            for(const auto &q:npns_) {
                for(double dv:{portDelta(q.b,q.e),portDelta(q.b,q.c)})
                    if(std::abs(dv)>0.18) limit=std::min(limit,0.18/std::abs(dv));
            }
            for(const auto &d:diodes_) {
                double dv=portDelta(d.anode,d.cathode);
                if(std::abs(dv)>0.18) limit=std::min(limit,0.18/std::abs(dv));
            }
            // Resolve nonlinear op-amp feedback transitions without jumping across
            // the high-gain knee in a single Newton update.
            for(const auto &a:amps_) if(a.parameters.gainBandwidthHz>0 || a.parameters.slewRateVPerUs>0) {
                const double dv=portDelta(a.plus,a.minus);
                const double vin=voltage(a.plus)-voltage(a.minus);
                // When deeply saturated, the differential input can swing by
                // hundreds of mV; constrain only around the high-gain knee.
                if(std::abs(vin)<0.02 && std::abs(dv)>0.004)
                    limit=std::min(limit,0.004/std::abs(dv));
                // When the op-amp sigmoid is saturated, Newton's local
                // derivative is essentially zero. Do not jump completely
                // across its sub-millivolt transition in a single iteration;
                // approach the switching surface before following the stiff
                // local derivative.  This affects only nonideal op-amps.
                const double knee=std::max(1e-6,
                    8.0*a.parameters.outputSwingVolts/a.parameters.openLoopGain);
                if(std::abs(vin)>knee && vin*(vin+dv)<0.) {
                    limit=std::min(limit,(std::abs(vin)-0.5*knee)/std::abs(dv));
                }
            }
            std::copy(unknowns_.begin(),unknowns_.end(),old_.begin());
            // Backtracking line search: tolerate tiny nonmonotonic rounding effects
            // but do not accept a Newton step that increases the residual dramatically.
            double trial=limit;
            for(int attempt=0;attempt<15;++attempt) {
                for(int i=0;i<dimension_;++i) unknowns_[i]=old_[i]+trial*delta_[i];
                evaluate(dt);
                if(merit()<=initialMerit*1.000001 || attempt==14) break;
                trial *= 0.5;
            }
            // For fixed J_LL and J_LB the passive Newton RHS is affine:
            // h(u) = -u_L - J_LL^-1*s_L - K*u_B.
            // The reduced solve enforces delta_L = h - K*delta_B.
            // Therefore h(u+trial*delta) = (1-trial)*h(u), identically.
            // Reuse that small vector rather than multiply K*u_B on every
            // Newton iteration. Full Newton steps set the term to zero.
            if(lastLinearUsedReduced_ &&
               (linearSolver_==LinearSolver::AffineReduced ||
                linearSolver_==LinearSolver::SparseAffineReduced)) {
                const double left=1.0-trial;
                for(double& x:linAnswer_)x*=left;
                reducedAffineValid_=true;
            } else reducedAffineValid_=false;
        }
        lastIterations_=maxIter;
        totalNewtonIterations_+=maxIter;
        peakNewtonIterations_=std::max(peakNewtonIterations_,maxIter);
#ifdef HYPED_DEBUG_SOLVE
        if(failedSolves_<8){evaluate(dt); double ff=0,oo=0;for(int i=0;i<numFreeNodes_;++i)ff=std::max(ff,std::abs(residual_[i]));for(int i=numFreeNodes_;i<dimension_;++i)oo=std::max(oo,std::abs(residual_[i]));fprintf(stderr,"FAIL segment=%d dt=%g f=%.9g op=%.9g merit=%g\n",int(segment_),dt,ff,oo,merit()); for(size_t aidx=0;aidx<amps_.size();++aidx){auto&a=amps_[aidx];fprintf(stderr," amp %s plus %.8g minus %.8g out %.8g prev %.8g r %.8g\n",a.label.c_str(),voltage(a.plus),voltage(a.minus),voltage(a.out),a.previousOutput,residual_[numFreeNodes_+aidx]);}}
#endif
        ++failedSolves_;
        return false;
    }
    void updateCaps() {
        for(auto &c:caps_) {
            c.previousVoltage=voltage(c.a)-voltage(c.b);
            c.previousCharge=c.farads*c.previousVoltage;
        }
        for(auto &a:amps_) {
            const auto &p=a.parameters;
            if(p.overloadRecoveryMs>0 && p.overloadRecoveryDepth>0) {
                const double rail=std::min(p.outputSwingVolts,
                     std::max(0.01,std::min(vref(),supplyVoltage_-vref())-0.01));
                const double frac=std::abs(voltage(a.out)-vref())/rail;
                const double dt=1.0/(sampleRate_*oversample_);
                const double tau=frac>.9 ? .0005 : p.overloadRecoveryMs*.001;
                const double alpha=-std::expm1(-dt/std::max(1e-6,tau));
                a.overloadMemory+=alpha*((frac>.9?1.:0.)-a.overloadMemory);
                a.overloadMemory=std::clamp(a.overloadMemory,0.,1.);
            } else a.overloadMemory=0.;
            a.previousOutput=voltage(a.out);
        }
    }
};
} // namespace hyped
