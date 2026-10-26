// Behavior and reference-EOS checks. These do not validate proprietary Tesla hardware.
#define Roadster LegacyRoadster
#include "ColdGasSystem.before.h"
#undef Roadster
#ifdef ROADSTER_LIVE_KERNEL
#include "../../Unreal/RoadsterSim/Source/RoadsterSim/Public/ColdGasSystem.h"
#else
#include "ColdGasSystem.candidate.h"
#endif
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace Roadster::ColdGas;
static int failures=0, checks=0;
static void Check(const char* name,bool ok,double value=0) { ++checks;if(!ok)++failures;std::printf("%s | %s | %.12g\n",ok?"PASS":"FAIL",name,value); }
static bool Near(double a,double b,double rel=1e-8) { return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=rel*std::max(1.0,std::fabs(b)); }

static void Reference(GasSpecies species,const char* file)
{
    Thermodynamics eos(species);std::ifstream input(file);Check(file,input.good());
    std::string row;std::getline(input,row);double rhoErr=0,cpErr=0,cvErr=0,wErr=0,jtErr=0,hDeltaErr=0,h0=0,refh0=0;int count=0;
    while(std::getline(input,row)) {
        std::istringstream line(row);double T,p,rho,v,u,h,s,cv,cp,w,jt;
        if(!(line>>T>>p>>rho>>v>>u>>h>>s>>cv>>cp>>w>>jt))continue;
        const auto q=eos.EvaluatePT(p*1e5,T);if(count==0){h0=q.h;refh0=h*1000;}
        rhoErr=std::max(rhoErr,std::fabs(q.rho/rho-1));cpErr=std::max(cpErr,std::fabs(q.cp/(cp*1000)-1));
        cvErr=std::max(cvErr,std::fabs(q.cv/(cv*1000)-1));wErr=std::max(wErr,std::fabs(q.w/w-1));
        jtErr=std::max(jtErr,std::fabs(q.muJT*1e5-jt));hDeltaErr=std::max(hDeltaErr,std::fabs(q.h-h0-(h*1000-refh0)));++count;
    }
    std::printf("NIST %s %s rows=%d errors: rho=%.6g%% cp=%.6g%% cv=%.6g%% sound=%.6g%% JT=%.6g K/bar dh=%.6g J/kg\n",eos.Data().Name,file,count,rhoErr*100,cpErr*100,cvErr*100,wErr*100,jtErr,hDeltaErr);
    Check("reference rows present",count>=8,count);
    // WebBook helium may use a prior reference EOS; distinguish its differences from the exact IR 8474 checks below.
    Check("density agrees with NIST within 10 ppm",rhoErr<1e-5,rhoErr);
    Check("heat capacities agree with NIST within 10 ppm",cpErr<1e-5&&cvErr<1e-5,std::max(cpErr,cvErr));
    Check("sound agrees with NIST within 10 ppm",wErr<1e-5,wErr);
    Check("JT agrees with NIST within 0.00001 K per bar",jtErr<1e-5,jtErr);
    Check("pressure enthalpy differences agree within 2 J per kg",hDeltaErr<2,hDeltaErr);
}

struct Burst { double mass,isp,impulse,endMass,minT; };
static Burst Run(GasSpecies species,double pressure,double volume=200)
{
    Config cfg=Config::ForTankVolumeL(volume);cfg.Propellant=species;System s(cfg);
    Check("configure bounded pressure",s.ConfigureExperiment(species,pressure));
    const double m=s.GasMassKg(),isp=s.CurrentIspS(); s.SetArmed(true);
    for(int i=0;i<500;++i)s.Step(.001); // charge feed before opening rear bank
    double minT=1e9,integralMass=0,integralImpulse=0,maxF=0,endF=0;bool finite=true,positive=true;
    for(int k=0;k<5000;++k) {
        for(int i=0;i<4;++i)s.Command(i,1);s.Step(.001);
        const auto& t=s.Tel();const double f=t.TotalThrustN;
        integralMass+=.001*t.TotalMassFlowKgS;integralImpulse+=.001*f;maxF=std::max(maxF,f);endF=f;
        finite &= std::isfinite(t.DisplayBar)&&std::isfinite(t.ManifoldK)&&std::isfinite(f)&&std::isfinite(t.GasMassKg);
        positive &= t.DisplayBar>=0&&t.GasMassKg>0&&t.ManifoldK>0;
        for(int i=0;i<t.NumTanks;++i)minT=std::min(minT,t.Tank[i].TempK);
    }
    const auto& t=s.Tel();std::printf("BURST %s p=%.0f V=%.0fL fill=%.9f kg initial-Isp=%.6f s impulse=%.6f Ns residual=%.9f kg minT=%.6f K peak=%.4f end=%.4f N\n",Properties(species).Name,pressure,volume,m,isp,t.TotalImpulseNs,t.GasMassKg,minT,maxF,endF);
    Check("finite positive blowdown state",finite&&positive,minT);
    Check("mass conserved below 1 microgram",std::fabs(s.MassBalanceErrorKg())<1e-9,s.MassBalanceErrorKg());
    Check("energy conserved below 0.01 joule",std::fabs(s.EnergyBalanceErrorJ())<.01,s.EnergyBalanceErrorJ());
    Check("frame mass equals expelled mass",Near(integralMass,t.MassExpelledKg,1e-8),integralMass-t.MassExpelledKg);
    Check("frame impulse equals total impulse",Near(integralImpulse,t.TotalImpulseNs,1e-8),integralImpulse-t.TotalImpulseNs);
    Check("finite supply depletes below one percent peak",endF<.01*maxF&&s.GasMassKg()<m,endF);
    Check("tank residual isolates supply",t.Tank[0].bCutoff||t.Tank[0].PressureBar<=10.2,t.Tank[0].PressureBar);
    const double before=s.GasMassKg();s.TopUp();Check("topup restores original inventory",Near(s.GasMassKg(),m),s.GasMassKg()-m);
    Check("topup conserved mass and energy",std::fabs(s.MassBalanceErrorKg())<1e-9&&std::fabs(s.EnergyBalanceErrorJ())<.01,s.EnergyBalanceErrorJ());
    return {m,isp,t.TotalImpulseNs,before,minT};
}

int main()
{
    for(GasSpecies species:{GasSpecies::Helium,GasSpecies::Argon}) {
        const char* gas=species==GasSpecies::Helium?"helium":"argon";
        for(int T:{200,300,350}) { const std::string file=std::string(gas)+"-"+std::to_string(T)+"K-NIST.tsv";Reference(species,file.c_str()); }
    }
    Thermodynamics he(GasSpecies::Helium);
    for(const auto& row:std::array<std::array<double,5>,2>{{ {{300,25,85.769640,13.176055,1349.3067}}, {{300,1,2.524130,12.496256,1030.3609}} }}) {
        const auto p=he.Evaluate(row[0],1/(row[1]*1000*.004002602));
        // Keep the R prescribed in Table 1; Table 3 has a roughly 1.2 ppm convention discrepancy.
        Check("He NIST IR 8474 Table 3 pressure within 2 ppm",Near(p.p/1e6,row[2],2e-6),p.p/1e6-row[2]);
        Check("He NIST IR 8474 Table 3 cv within 2 ppm",Near(p.cv*.004002602,row[3],2e-6),p.cv*.004002602-row[3]);
        Check("He NIST IR 8474 Table 3 sound within 2 ppm",Near(p.w,row[4],2e-6),p.w-row[4]);
    }
    for(GasSpecies species:{GasSpecies::Nitrogen,GasSpecies::Helium,GasSpecies::Argon}) {
        Thermodynamics eos(species);for(double p:{1e5,150e5,379e5,690e5}) {
            const auto q=eos.EvaluatePT(p,293.15);const double dv=q.v*1e-5,dT=.001;
            const double cv=(eos.U(q.T+dT,q.v)-eos.U(q.T-dT,q.v))/(2*dT);
            const double dudv=(eos.U(q.T,q.v+dv)-eos.U(q.T,q.v-dv))/(2*dv);
            Check("EOS inverse returns pressure",Near(q.p,p,1e-9),q.p/p-1);
            Check("enthalpy equals u plus pv",Near(q.h,q.u+p*q.v,1e-9),q.h-q.u-p*q.v);
            Check("cv equals temperature derivative of u",Near(q.cv,cv,1e-7),q.cv-cv);
            Check("Helmholtz Maxwell identity",Near(dudv,q.T*q.dpdT-q.p,1e-5),dudv-q.T*q.dpdT+q.p);
            Check("stable heat capacity and sound",q.cp>q.cv&&q.cv>0&&q.w>0,q.cp/q.cv);
        }
        Config c=Config::Rocket();c.Propellant=species;System system(c);
        const double mass=system.GasMassKg();
        Check("mass input roundtrips fill pressure",system.FillByMassKg(mass)&&Near(system.FillPressureBar(),379,1e-9),system.FillPressureBar());
        Check("half mass derives lower pressure",system.FillByMassKg(mass*.5)&&system.FillPressureBar()<379,system.FillPressureBar());
        const double before=system.GasMassKg();
        Check("overrating rejected without mutation",!system.ConfigureExperiment(species,691)&&system.GasMassKg()==before);
        Check("invalid temperature rejected",!system.ConfigureExperiment(species,379,0));
        Check("nonfinite and invalid gas rejected",!system.ConfigureExperiment(species,NAN)&&!system.ConfigureExperiment(static_cast<GasSpecies>(99),379));
        Check("invalid amounts rejected",!system.FillByMassKg(-1)&&!system.FillByMassKg(INFINITY)&&!system.FillByMassKg(0));
        for(double temperature:{200.,350.}) {
            Check("temperature envelope fill accepted",system.ConfigureExperiment(species,690,temperature));
            const double maxMass=system.GasMassKg();
            Check("mass input roundtrips maximum rating",system.FillByMassKg(maxMass,temperature)&&Near(system.FillPressureBar(),690,1e-9),system.FillPressureBar());
            const auto exhaust=system.Evaluate(0,1,150e5,temperature,0);
            Check("vacuum expansion remains finite",std::isfinite(exhaust.ThrustN)&&exhaust.ThrustN>0&&std::isfinite(exhaust.ExitMach)&&exhaust.ExitMach>0,exhaust.ThrustN);
            system.SetArmed(true);system.ConfigureExperiment(species,690,temperature);bool envelopeFinite=true;
            for(int k=0;k<2000;++k) {
                for(int i=0;i<4;++i)system.Command(i,1);system.Step(.001,0,kAmbientK);
                const auto& t=system.Tel();envelopeFinite &= std::isfinite(t.TotalThrustN)&&std::isfinite(t.Tank[0].TempK)&&t.Tank[0].TempK>0&&t.GasMassKg>0;
            }
            Check("vacuum burst at fill temperature limit is finite",envelopeFinite,system.Tel().Tank[0].TempK);
            Check("vacuum burst conserves energy",std::fabs(system.EnergyBalanceErrorJ())<.01,system.EnergyBalanceErrorJ());
            Check("vacuum burst conserves mass",std::fabs(system.MassBalanceErrorKg())<1e-9,system.MassBalanceErrorKg());
        }
    }
    const Burst n=Run(GasSpecies::Nitrogen,379),h=Run(GasSpecies::Helium,379),ar=Run(GasSpecies::Argon,379);
    Check("equal pressure fill mass order Ar > N2 > He",ar.mass>n.mass&&n.mass>h.mass,h.mass);
    Check("exhaust Isp order He > N2 > Ar",h.isp>n.isp&&n.isp>ar.isp,h.isp);
    for(GasSpecies species:{GasSpecies::Nitrogen,GasSpecies::Helium,GasSpecies::Argon})Run(species,690,600);

    // Compare the untouched pre-edit model with the new default using the same valve schedule.
    LegacyRoadster::ColdGas::System old;System current;old.SetArmed(true);current.SetArmed(true);
    double worst=0;for(int k=0;k<2500;++k) {
        for(int i=0;i<4;++i){const double cmd=k>499 ? .75:0;old.Command(i,cmd);current.Command(i,cmd);}
        old.Step(.001);current.Step(.001);
        const auto& a=old.Tel();const auto& b=current.Tel();worst=std::max(worst,std::fabs(a.TotalThrustN-b.TotalThrustN));
    }
    Check("nitrogen default regression thrust within 1 mN",worst<.001,worst);
    Check("nitrogen default regression mass",Near(old.GasMassKg(),current.GasMassKg(),1e-8),old.GasMassKg()-current.GasMassKg());
    Check("nitrogen default regression impulse",Near(old.Tel().TotalImpulseNs,current.Tel().TotalImpulseNs,1e-8),old.Tel().TotalImpulseNs-current.Tel().TotalImpulseNs);
    std::printf("RESULT %d/%d passed\n",checks-failures,checks);return failures?1:0;
}
