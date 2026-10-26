#include "../../Unreal/RoadsterSim/Source/RoadsterSim/Public/ColdGasSystem.h"
#include <chrono>
#include <cstdio>
using namespace Roadster::ColdGas;
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main() {
    for(GasSpecies gas:{GasSpecies::Nitrogen,GasSpecies::Helium,GasSpecies::Argon}) {
        Config cfg=Config::Rocket();cfg.Propellant=gas;
        double start=Now();System system(cfg);const double build=Now()-start;
        start=Now();for(int k=0;k<1000;++k)system.Step(.001);const double idle=Now()-start;
        system.SetArmed(true);system.Fill(379,kAmbientK);start=Now();
        for(int k=0;k<1000;++k){for(int i=0;i<4;++i)system.Command(i,1);system.Step(.001);}
        const double burst=Now()-start;start=Now();system.ConfigureExperiment(gas,690);
        const double refill=Now()-start;
        std::printf("%s | first table/system build %.6f s | 1 simulated second idle %.6f wall s | 1 simulated second boost %.6f wall s | cached reconfigure %.6f s\n",Properties(gas).Name,build,idle,burst,refill);
    }
}
