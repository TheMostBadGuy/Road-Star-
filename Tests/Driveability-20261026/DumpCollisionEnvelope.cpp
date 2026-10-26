#include "../../Unreal/RoadsterSim/Source/RoadsterSim/Public/RoadsterPhysics.h"
#include <cstdio>
int main(){Roadster::Vehicle v;v.Init(Roadster::Trim::Rocket);std::printf("{\"count\":%d,\"probes\":[",v.NumProbes);for(int k=0;k<v.NumProbes;++k){const auto&p=v.Probes[k];std::printf("%s{\"p\":[%.17g,%.17g,%.17g],\"r\":%.17g}",k?",":"",p.P.x,p.P.y,p.P.z,p.R);}std::printf("]}\n");}
