#pragma once
#include "HypedFuzz.h"
#include <array>
#include <cmath>

namespace hyped {

// Same nonlinear circuit equations as Circuit, partitioned at ideal op-amp outputs.
// No heap allocations, coefficient design, or system calls in process().
class Realtime {
public:
    explicit Realtime(double sampleRate=48000., int oversample=2)
        : os_(oversample),
          front_(sampleRate*oversample,1,Segment::Front),
          fuzz_(sampleRate*oversample,1,Segment::Fuzz),
          output_(sampleRate*oversample,1,Segment::Output) {
        if(os_!=1 && os_!=2 && os_!=4 && os_!=8)
            throw std::invalid_argument("Realtime oversampling must be 1,2,4,or 8");
        prepareDecimator();
        setControls({});
        reset();
    }
    void setLinearSolver(LinearSolver x) {
        front_.setLinearSolver(x);fuzz_.setLinearSolver(x);output_.setLinearSolver(x);
    }
    LinearSolver linearSolver() const {return front_.linearSolver();}
    void setFastNonlinearMath(bool enable) {
        front_.setFastNonlinearMath(enable);
        fuzz_.setFastNonlinearMath(enable);
        output_.setFastNonlinearMath(enable);
    }
    bool fastNonlinearMath() const {return front_.fastNonlinearMath();}
    std::array<int,3> dimensions() const {return {front_.nodeCount(),fuzz_.nodeCount(),output_.nodeCount()};}
    std::array<int,3> reducedDimensions() const {return {front_.reducedDimension(),fuzz_.reducedDimension(),output_.reducedDimension()};}
    unsigned long long compactFallbacks() const {
        return front_.compactFallbacks()+fuzz_.compactFallbacks()+output_.compactFallbacks();
    }
    unsigned long long reducedFallbacks() const {
        return front_.reducedFallbacks()+fuzz_.reducedFallbacks()+output_.reducedFallbacks();
    }
    unsigned long long schurRebuilds() const {
        return front_.schurRebuilds()+fuzz_.schurRebuilds()+output_.schurRebuilds();
    }
    unsigned long long totalNewtonIterations() const {
        return front_.totalNewtonIterations()+fuzz_.totalNewtonIterations()+output_.totalNewtonIterations();
    }
    unsigned long long convergedSteps() const {
        return front_.convergedSteps()+fuzz_.convergedSteps()+output_.convergedSteps();
    }
    int peakNewtonIterations() const {
        return std::max({front_.peakNewtonIterations(),fuzz_.peakNewtonIterations(),output_.peakNewtonIterations()});
    }
    void setControls(const Controls &c) {
        front_.setControls(c);fuzz_.setControls(c);output_.setControls(c);
    }
    bool setSolverIterationLimit(int n) {
        if(n<16 || n>320)return false;
        return front_.setSolverIterationLimit(n) && fuzz_.setSolverIterationLimit(n)
               && output_.setSolverIterationLimit(n);
    }
    void processBlock(const float* input,float* output,std::size_t frames) {
        for(std::size_t i=0;i<frames;++i)output[i]=process(input[i]);
    }
    void setInputVoltsPerUnit(double x) { inputVoltsPerUnit_=std::clamp(x,.001,5.); }
    // Name-based extended controls. Call outside process(), ideally at control rate.
    bool setComponentScale(const std::string& id,double scale) {
        bool a=front_.setComponentScale(id,scale);
        bool b=fuzz_.setComponentScale(id,scale);
        bool c=output_.setComponentScale(id,scale);
        return a||b||c;
    }
    bool setResistorOhms(const std::string& id,double ohms) {
        bool a=front_.setResistorOhms(id,ohms);
        bool b=fuzz_.setResistorOhms(id,ohms);
        bool c=output_.setResistorOhms(id,ohms);
        return a||b||c;
    }
    bool setCapacitanceFarads(const std::string& id,double farads) {
        bool a=front_.setCapacitanceFarads(id,farads);
        bool b=fuzz_.setCapacitanceFarads(id,farads);
        bool c=output_.setCapacitanceFarads(id,farads);
        return a||b||c;
    }
    bool setComponentJitter(const std::string& id,double amount,double refreshHz=5.,
                            double smoothingSeconds=.02,uint32_t seed=12345) {
        bool a=front_.setComponentJitter(id,amount,refreshHz,smoothingSeconds,seed);
        bool b=fuzz_.setComponentJitter(id,amount,refreshHz,smoothingSeconds,seed);
        bool c=output_.setComponentJitter(id,amount,refreshHz,smoothingSeconds,seed);
        return a||b||c;
    }
    void randomizeTolerances(double rFraction,double cFraction,uint32_t seed=0xBADC0DEu) {
        front_.randomizeTolerances(rFraction,cFraction,seed);
        fuzz_.randomizeTolerances(rFraction,cFraction,seed);
        output_.randomizeTolerances(rFraction,cFraction,seed);
    }
    bool setOpAmpParameters(const std::string& id,OpAmpParameters p) {
        bool a=front_.setOpAmpParameters(id,p);
        bool b=fuzz_.setOpAmpParameters(id,p);
        bool c=output_.setOpAmpParameters(id,p);
        return a||b||c;
    }
    bool setOpAmpJitter(const std::string& id,double slewAmount,double offsetJitterVolts,
                        double refreshHz=4.,double smoothingSeconds=.05,uint32_t seed=12345) {
        bool a=front_.setOpAmpJitter(id,slewAmount,offsetJitterVolts,refreshHz,smoothingSeconds,seed);
        bool b=fuzz_.setOpAmpJitter(id,slewAmount,offsetJitterVolts,refreshHz,smoothingSeconds,seed);
        bool c=output_.setOpAmpJitter(id,slewAmount,offsetJitterVolts,refreshHz,smoothingSeconds,seed);
        return a||b||c;
    }
    bool setTransistorParameters(const std::string& id,TransistorParameters p) {
        bool a=front_.setTransistorParameters(id,p);
        bool b=fuzz_.setTransistorParameters(id,p);
        bool c=output_.setTransistorParameters(id,p);
        return a||b||c;
    }
    bool setDiodeParameters(const std::string& id,DiodeParameters p) {
        bool a=front_.setDiodeParameters(id,p);
        bool b=fuzz_.setDiodeParameters(id,p);
        bool c=output_.setDiodeParameters(id,p);
        return a||b||c;
    }
    bool setTransistorJitter(const std::string& id,double betaFraction,double satFraction,
                             double hz=4.,double seconds=.05,uint32_t seed=12345) {
        bool a=fuzz_.setTransistorJitter(id,betaFraction,satFraction,hz,seconds,seed);
        bool b=output_.setTransistorJitter(id,betaFraction,satFraction,hz,seconds,seed);
        return a||b;
    }
    bool setDiodeJitter(const std::string& id,double saturationFraction,double emissionFraction,
                        double hz=4.,double seconds=.05,uint32_t seed=12345) {
        return fuzz_.setDiodeJitter(id,saturationFraction,emissionFraction,hz,seconds,seed);
    }
    bool setOpAmpRecovery(const std::string& id,double milliseconds,double depth) {
        bool a=front_.setOpAmpRecovery(id,milliseconds,depth);
        bool b=fuzz_.setOpAmpRecovery(id,milliseconds,depth);
        bool c=output_.setOpAmpRecovery(id,milliseconds,depth);
        return a||b||c;
    }
    bool setOpAmpSlewRate(const std::string& id,double vPerUs) {
        bool a=front_.setOpAmpSlewRate(id,vPerUs);
        bool b=fuzz_.setOpAmpSlewRate(id,vPerUs);
        bool c=output_.setOpAmpSlewRate(id,vPerUs);
        return a||b||c;
    }
    bool setOpAmpBandwidth(const std::string& id,double hz) {
        bool a=front_.setOpAmpBandwidth(id,hz);
        bool b=fuzz_.setOpAmpBandwidth(id,hz);
        bool c=output_.setOpAmpBandwidth(id,hz);
        return a||b||c;
    }
    bool setOpAmpOutputSwing(const std::string& id,double volts) {
        bool a=front_.setOpAmpOutputSwing(id,volts);
        bool b=fuzz_.setOpAmpOutputSwing(id,volts);
        bool c=output_.setOpAmpOutputSwing(id,volts);
        return a||b||c;
    }
    void setTemperatureCelsius(double degrees) {
        front_.setTemperatureCelsius(degrees);fuzz_.setTemperatureCelsius(degrees);
        output_.setTemperatureCelsius(degrees);
    }
    void setSupplyVoltage(double volts) {
        front_.setSupplyVoltage(volts);fuzz_.setSupplyVoltage(volts);output_.setSupplyVoltage(volts);
    }
    void setReferenceRatio(double ratio) {
        front_.setReferenceRatio(ratio);fuzz_.setReferenceRatio(ratio);output_.setReferenceRatio(ratio);
    }
    void setReferenceVoltageFixed(double volts) {
        front_.setReferenceVoltageFixed(volts);fuzz_.setReferenceVoltageFixed(volts);
        output_.setReferenceVoltageFixed(volts);
    }
    std::vector<std::string> componentNames() const {
        auto names=front_.componentNames();
        auto add=fuzz_.componentNames();names.insert(names.end(),add.begin(),add.end());
        add=output_.componentNames();names.insert(names.end(),add.begin(),add.end());
        return names;
    }
    std::vector<std::string> opAmpNames() const {
        auto names=front_.opAmpNames();
        auto add=fuzz_.opAmpNames();names.insert(names.end(),add.begin(),add.end());
        add=output_.opAmpNames();names.insert(names.end(),add.begin(),add.end());
        return names;
    }
    std::vector<std::string> transistorNames() const {
        auto names=fuzz_.transistorNames();
        auto add=output_.transistorNames();names.insert(names.end(),add.begin(),add.end());
        return names;
    }
    std::vector<std::string> diodeNames() const { return fuzz_.diodeNames(); }

    void reset() {
        lastInput_=0;
        front_.reset();fuzz_.reset();output_.reset();
        history_.fill(0.);pos_=0;
    }
    float process(float in) {
        const double input = std::isfinite(in)?std::clamp(double(in),-8.,8.):0.;
        double last=0;
        for(int j=0;j<os_;++j) {
            const double f=double(j+1)/os_;
            const double volts=inputVoltsPerUnit_*(lastInput_*(1.-f)+input*f);
            const double boost=front_.stepVolts(volts);
            const double full=fuzz_.stepVolts(boost);
            const double scooped=fuzz_.scoopedOutputVolts();
            last=output_.stepVolts(full,scooped,boost);
            history_[pos_] = last;
            pos_=(pos_+1)%kTaps;
        }
        lastInput_=input;
        double result=0;
        if (os_==1) result=last;
        else for(int k=0;k<kTaps;++k)
            result += coefficients_[k]*history_[(pos_+kTaps-1-k)%kTaps];
        return float(std::clamp(result,-10.,10.));
    }
    unsigned long long failedSolves() const {
        return front_.failedSolves()+fuzz_.failedSolves()+output_.failedSolves();
    }
    std::array<unsigned long long,3> failedByStage() const {return {front_.failedSolves(),fuzz_.failedSolves(),output_.failedSolves()};}
    // The stage voltages are available for debugging / calibration.
    double inspectBoost() const {return front_.inspectVoltage("boost_out");}
    double inspectFull() const {return fuzz_.inspectVoltage("full_out");}
    double inspectScooped() const {return fuzz_.scoopedOutputVolts();}
private:
    static constexpr int kTaps=63;
    int os_;
    Circuit front_,fuzz_,output_;
    double lastInput_=0.,inputVoltsPerUnit_=0.25;
    std::array<double,kTaps> history_{},coefficients_{};
    int pos_=0;
    void prepareDecimator() {
        constexpr double pi=3.141592653589793;
        if(os_==1) { coefficients_[0]=1.;return; }
        const double fc=0.45/double(os_);
        double sum=0;
        for(int k=0;k<kTaps;++k) {
            double x=k-(kTaps-1)*.5;
            double sinc=std::abs(x)<1e-10?2*fc:std::sin(2*pi*fc*x)/(pi*x);
            double win=.42-.5*std::cos(2*pi*k/(kTaps-1))+.08*std::cos(4*pi*k/(kTaps-1));
            coefficients_[k]=sinc*win;
            sum+=coefficients_[k];
        }
        for(auto &coef:coefficients_) coef/=sum;
    }
};
} // namespace hyped
