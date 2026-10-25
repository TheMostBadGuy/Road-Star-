// Behavioral checks on the local numerical model, not validation of Tesla hardware.
#define main existing_diagnostic_main
#include "../PhysicsHarness.cpp"
#undef main
#include <cmath>

static int failures = 0;
static void Check(const char* name, bool ok, double value) {
    std::printf("%s | %s | %.12g\n", ok ? "PASS" : "FAIL", name, value);
    if (!ok) ++failures;
}
static bool Finite(const Vehicle& v) {
    return std::isfinite(v.X.x) && std::isfinite(v.X.y) && std::isfinite(v.X.z)
        && std::isfinite(v.V.Len()) && std::isfinite(v.W.Len()) && std::isfinite(v.T.FanDownforceN);
}

// Analytic interior cylinder, axis X, bottom at z=0. Unlike an infinite plane,
// it reproduces rays starting beyond a curved floor at diagonal car headings.
static TraceFn Cylinder(double radius) {
    return [radius](const V3& s,const V3& d,double len,double r)->Hit {
        const V3 c=s-V3(0,0,radius); const double RR=radius-r;
        const double a=d.y*d.y+d.z*d.z,b=2*(c.y*d.y+c.z*d.z),cc=c.y*c.y+c.z*c.z-RR*RR;
        if(cc>1e-8 || a<1e-12)return Hit();
        const double disc=b*b-4*a*cc;if(disc<0)return Hit();
        const double t=(-b+std::sqrt(disc))/(2*a);if(t<0 || t>len)return Hit();
        const V3 center=s+d*t,n=V3(0,-center.y,radius-center.z).Norm();
        Hit h;h.bHit=true;h.Point=center-n*r;h.Normal=n;h.CenterDist=t;h.Dist=(h.Point-s).Dot(d);return h;
    };
}

int main() {
    // Gravity alone is enough to lose ceiling contact; suction must support the car.
    const auto ceiling = MakeTrace({ { V3(0,0,6), V3(0,0,-1) } });
    for (FanMode fm : { FanMode::Off, FanMode::MaxGrip }) {
        Vehicle v; v.Init(Trim::Rocket); v.Set.Aero=AeroPreset::Custom; v.Set.Fan=fm;
        v.Place(V3(0,0,6), Quat::AxisAngle(V3(1,0,0), kPi));
        v.FanRPM=fm==FanMode::MaxGrip ? v.S.FanMaxRPM : 0; v.SkirtDeploy=1;
        Inputs in; for(int k=0;k<5000;++k) v.Step(.001,in,ceiling);
        Check(fm==FanMode::Off ? "ceiling fan off loses contact" : "ceiling fan on supports weight",
              Finite(v) && (fm==FanMode::Off ? v.BodyOriginWorld().z<0 && v.T.WheelsInContact==0
                : std::fabs(v.BodyOriginWorld().z-6)<.02 && v.T.WheelsInContact==4 && v.T.FanDownforceN>v.TotalMass()*kG),
              v.BodyOriginWorld().z);
    }
    {
        Vehicle v=MakeCar(Trim::Rocket); v.Set.Aero=AeroPreset::Custom; v.Set.Fan=FanMode::MaxGrip;
        const TraceFn empty=[](const V3&,const V3&,double,double){return Hit();};
        Inputs in; for(int k=0;k<200;++k) v.Step(.001,in,empty);
        Check("no nearby surface means no fan suction",v.T.FanDownforceN==0,v.T.FanDownforceN);
    }
    {
        // The center ray still hits both partial surfaces. The corner probes must
        // detect loss of floor coverage and leak the common plenum.
        Vehicle base=MakeCar(Trim::Performance); base.Set.Aero=AeroPreset::Custom; base.Set.Fan=FanMode::MaxGrip;
        Inputs in; in.Brake=.2;
        const TraceFn narrow=[](const V3& s,const V3& d,double len,double r) {
            Hit h=Flat(s,d,len,r);
            if(h.bHit && (std::fabs(h.Point.x+.2)>.25 || std::fabs(h.Point.y)>.25))return Hit();
            return h;
        };
        const TraceFn edge=[](const V3& s,const V3& d,double len,double r) {
            Hit h=Flat(s,d,len,r); if(h.bHit && h.Point.x>.5)return Hit(); return h;
        };
        const TraceFn backface=[](const V3& s,const V3& d,double len,double r) {
            Hit h=Flat(s,d,len,r); h.Normal=-h.Normal; return h;
        };
        const TraceFn remote=[](const V3& s,const V3& d,double len,double r) {
            Hit h=Flat(s,d,len,r); if(h.bHit)h.Dist=len+1; return h;
        };
        Vehicle full=base,strip=base,partial=base,wrong=base,far=base;
        full.Step(.001,in,Flat);strip.Step(.001,in,narrow);partial.Step(.001,in,edge);
        wrong.Step(.001,in,backface);far.Step(.001,in,remote);
        const double narrowRatio=strip.T.FanDownforceN/full.T.FanDownforceN;
        const double edgeRatio=partial.T.FanDownforceN/full.T.FanDownforceN;
        Check("full floor supplies usable fan suction",full.T.FanDownforceN>full.TotalMass()*kG,full.T.FanDownforceN);
        Check("center-only narrow surface leaks away most suction",narrowRatio>=0 && narrowRatio<.1,narrowRatio);
        Check("missing front floor reduces shared-plenum suction",edgeRatio>narrowRatio && edgeRatio<.35,edgeRatio);
        Check("back-facing surface cannot seal underfloor",wrong.T.FanDownforceN==0,wrong.T.FanDownforceN);
        Check("out-of-reach hit cannot seal underfloor",far.T.FanDownforceN==0,far.T.FanDownforceN);
    }
    {
        const auto cylinder=Cylinder(8);
        double minimumSeal=1;
        bool finite=true;
        for(bool lowered:{false,true})for(double roll:{0.,90.,180.})for(double yaw:{0.,30.,45.}) {
            Vehicle v;v.Init(Trim::Rocket);v.Set.Aero=AeroPreset::Custom;v.Set.Fan=FanMode::MaxGrip;v.Set.bLowSuspension=lowered;
            const double phi=roll*kPi/180;
            const Quat q=Quat::AxisAngle(V3(1,0,0),phi)*Quat::AxisAngle(V3(0,0,1),yaw*kPi/180);
            v.Place(V3(0,8*std::sin(phi),8-8*std::cos(phi)),q);v.FanRPM=v.S.FanMaxRPM;v.SkirtDeploy=1;
            Inputs in;v.Step(.001,in,cylinder);finite &= Finite(v);minimumSeal=std::min(minimumSeal,v.T.FanSeal);
        }
        Check("curved cylinder retains seal at diagonal headings upright wall and ceiling",finite&&minimumSeal>.85,minimumSeal);
    }
    {
        Vehicle v=MakeCar(Trim::Performance); Inputs in; in.Boost=in.Lift=1;
        for(int k=0;k<500;++k) v.Step(.001,in,Flat);
        Check("Performance trim cannot produce gas thrust",v.T.ThrustN==0 && v.T.TankKg==0,v.T.ThrustN);
    }
    {
        Vehicle v=MakeCar(Trim::Rocket); v.Set.bThrustersArmed=false; Inputs in; in.Boost=in.Lift=1;
        for(int k=0;k<1000;++k) v.Step(.001,in,Flat);
        Check("disarmed thrusters decay below 0.01 N",v.T.ThrustN<.01,v.T.ThrustN);
    }
    {
        ColdGas::System s(ColdGas::Config::Rocket()); s.SetArmed(true); s.Fill(379,293.15);
        for(int k=0;k<1000;++k) s.Step(.001);
        const double m=s.GasMassKg()+s.ManifoldMassKg();
        for(int k=0;k<500;++k) {for(int j=0;j<4;++j)s.Command(j,1);s.Step(.001);}
        const double after=s.GasMassKg()+s.ManifoldMassKg();
        Check("boost consumes gas",after<m,after-m);
        Check("burst conserves mass",std::fabs(s.MassBalanceErrorKg())<1e-8,s.MassBalanceErrorKg());
        const double rel=std::fabs(s.EnergyBalanceErrorJ())/std::max(1.,s.EnergyThroughputJ());
        Check("burst conserves energy",rel<1e-9,rel);
        s.Fill(379,293.15);
        Check("refill restores pressure",std::fabs(s.Tel().DisplayBar-379)<.1,s.Tel().DisplayBar);
    }
    {
        Vehicle a=MakeCar(Trim::Performance),b=a; Inputs in; in.Throttle=.7;
        for(int k=0;k<3000;++k)a.Step(.001,in,Flat);
        for(int k=0;k<6000;++k)b.Step(.0005,in,Flat);
        const double dv=std::fabs(a.T.Speed-b.T.Speed);
        Check("half timestep acceleration speed agrees within 1 percent",Finite(a)&&Finite(b)&&dv<std::max(0.2,.01*b.T.Speed),dv);
    }
    {
        Vehicle v=MakeCar(Trim::Performance); v.Set.Aero=AeroPreset::Custom; v.Set.Fan=FanMode::MaxGrip;
        v.EnergyKWh=0; Inputs in; in.Brake=.2;
        for(int k=0;k<2000;++k) v.Step(.001,in,Flat);
        Check("empty battery stops electric fan",v.T.FanPowerKW<.01 && v.T.FanDownforceN<1,v.T.FanDownforceN);
    }
    {
        Vehicle v=MakeCar(Trim::Performance); v.Set.Aero=AeroPreset::Custom; v.Set.Fan=FanMode::MaxGrip;
        v.EnergyKWh=0; v.Set.bInfiniteBattery=true; Inputs in; in.Brake=.2;
        for(int k=0;k<2000;++k) v.Step(.001,in,Flat);
        Check("unlimited battery powers fan after empty finite state",v.T.FanDownforceN>v.TotalMass()*kG && v.SOC()>.99,v.T.FanDownforceN);
    }
    {
        Vehicle v=MakeCar(Trim::Performance); v.Set.Aero=AeroPreset::Custom; v.Set.Fan=FanMode::Off;
        v.EnergyKWh=0; Inputs in; in.Throttle=1;
        double peakTorque=0;
        for(int k=0;k<2000;++k) { v.Step(.001,in,Flat); peakTorque=std::max(peakTorque,std::fabs(v.T.RearTorque)); }
        Check("empty battery never requests drive torque including at standstill",peakTorque<.01,peakTorque);
    }
    {
        Vehicle v=MakeCar(Trim::Performance); v.Set.Aero=AeroPreset::Custom; v.Set.Fan=FanMode::Off;
        v.EnergyKWh=0; v.Set.bInfiniteBattery=true; Inputs in; in.Throttle=1;
        double peakTorque=0;
        for(int k=0;k<1000;++k) { v.Step(.001,in,Flat); peakTorque=std::max(peakTorque,std::fabs(v.T.RearTorque)); }
        Check("unlimited battery still enables drive torque",peakTorque>1,peakTorque);
    }
    std::printf("RESULT: %d failure(s)\n",failures);
    return failures ? 1:0;
}
