#include "../../Unreal/RoadsterSim/Source/RoadsterSim/Public/PlanetEnvironment.h"
#include <cstdio>
#include <algorithm>
#include <array>
#include <string>

using Earth::Vec3;
static int Checks=0, Failures=0;
static void Check(const char* name, bool ok, double value=0)
{
    ++Checks; if (!ok) ++Failures;
    std::printf("%s | %s | %.15g\n",ok?"PASS":"FAIL",name,value);
}
static bool Near(double actual, double reference, double relative=1e-4, double absolute=0)
{ return std::abs(actual-reference)<=absolute+relative*std::abs(reference); }

struct CoastState { Vec3 Position,Velocity; };
static CoastState Add(const CoastState& a,const CoastState& b,double scale)
{ return {a.Position+b.Position*scale,a.Velocity+b.Velocity*scale}; }
static CoastState Derivative(const CoastState& s) { return {s.Velocity,Earth::Gravity(s.Position)}; }
static CoastState RK4(const CoastState& s,double dt)
{
    const auto k1=Derivative(s),k2=Derivative(Add(s,k1,dt/2));
    const auto k3=Derivative(Add(s,k2,dt/2)),k4=Derivative(Add(s,k3,dt));
    return {s.Position+(k1.Position+2*k2.Position+2*k3.Position+k4.Position)*(dt/6),
            s.Velocity+(k1.Velocity+2*k2.Velocity+2*k3.Velocity+k4.Velocity)*(dt/6)};
}
struct CoastResult { double EnergyDrift=0,AngularMomentumDrift=0,PositionClosureM=0,VelocityClosureMps=0; };
static CoastResult CircularCoast(int steps,FILE* csv=nullptr)
{
    const double r=Earth::MeanRadiusM+400000;
    const double speed=std::sqrt(Earth::MuM3PerS2/r);
    const double period=2*3.14159265358979323846*std::sqrt(r*r*r/Earth::MuM3PerS2);
    const CoastState start{{0,0,400000},{speed,0,0}};
    CoastState state=start;
    const auto initial=Earth::OrbitalElements(start.Position,start.Velocity);
    CoastResult out;
    if (csv) std::fprintf(csv,"time_s,x_m,y_m,z_m,vx_mps,vy_mps,vz_mps,specific_energy_j_per_kg,specific_angular_momentum_m2_per_s\n");
    for (int i=0;i<=steps;++i)
    {
        const auto orbit=Earth::OrbitalElements(state.Position,state.Velocity);
        out.EnergyDrift=std::max(out.EnergyDrift,std::abs(orbit.SpecificEnergyJPerKg/initial.SpecificEnergyJPerKg-1));
        out.AngularMomentumDrift=std::max(out.AngularMomentumDrift,std::abs(orbit.SpecificAngularMomentumM2PerS/initial.SpecificAngularMomentumM2PerS-1));
        if (csv && (i%32==0 || i==steps))
            std::fprintf(csv,"%.12g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g\n",i*period/steps,
                state.Position.X,state.Position.Y,state.Position.Z,state.Velocity.X,state.Velocity.Y,state.Velocity.Z,
                orbit.SpecificEnergyJPerKg,orbit.SpecificAngularMomentumM2PerS);
        if (i<steps) state=RK4(state,period/steps);
    }
    out.PositionClosureM=Earth::Length(state.Position-start.Position);
    out.VelocityClosureMps=Earth::Length(state.Velocity-start.Velocity);
    return out;
}

int main()
{
    // Independent rounded geometric-altitude entries visually checked in Table I,
    // not geopotential-altitude rows. Source location recorded in README.md.
    struct Ref { double Z,T,P,Rho; };
    const Ref lower[]={{0,288.15,101325,1.2250},
        {11000,216.774,22699,.36480},{20000,216.650,5529.3,.088910},
        {50000,270.650,79.779,.0010269},{86000,186.8673,.37338,6.958e-6}};
    for (const auto& r:lower)
    {
        const auto a=Earth::Sample({0,0,r.Z});
        const std::string label=std::to_string(static_cast<int>(r.Z))+"m geometric ";
        Check((label+"temperature reference").c_str(),Near(a.TemperatureK,r.T,0,.001),a.TemperatureK);
        Check((label+"pressure reference").c_str(),Near(a.PressurePa,r.P),a.PressurePa);
        Check((label+"density reference").c_str(),Near(a.DensityKgM3,r.Rho),a.DensityKgM3);
    }
    const Ref upper[]={{100000,195.08,.032011,5.604e-7},{120000,360,.0025382,2.222e-8},
        {200000,854.56,.000084736,2.541e-10},{400000,995.83,1.4518e-6,2.803e-12},
        {1000000,1000,7.5138e-9,3.561e-15}};
    for (const auto& r:upper)
    {
        const auto a=Earth::SampleAltitude(r.Z);
        const std::string label=std::to_string(static_cast<int>(r.Z))+"m upper table ";
        Check((label+"temperature reference").c_str(),Near(a.TemperatureK,r.T,0,.01),a.TemperatureK);
        Check((label+"pressure knot").c_str(),Near(a.PressurePa,r.P,1e-12),a.PressurePa);
        Check((label+"density knot").c_str(),Near(a.DensityKgM3,r.Rho,1e-12),a.DensityKgM3);
    }
    // Held-out rows quantify the sparse interpolation's practical approximation.
    const Ref heldOut[]={{240000,929.73,3.1059e-5,7.858e-11},
                         {380000,994.10,2.0384e-6,4.013e-12}};
    double maxHeldOutError=0;
    for (const auto& r:heldOut)
    {
        const auto a=Earth::SampleAltitude(r.Z);
        const double error=std::max(std::abs(a.PressurePa/r.P-1),std::abs(a.DensityKgM3/r.Rho-1));
        maxHeldOutError=std::max(error,maxHeldOutError);
        Check("held-out upper interpolation within documented 6 percent spot-check tolerance",error<.06,error);
    }
    bool monotonic=true,positive=true;
    auto previous=Earth::SampleAltitude(0);
    FILE* samples=nullptr; fopen_s(&samples,"environment_samples.csv","w");
    if (samples) std::fprintf(samples,"geometric_altitude_m,temperature_k,pressure_pa,density_kg_m3,gravity_mps2\n");
    for (int z=0;z<=1500000;z+=1000)
    {
        const auto a=Earth::SampleAltitude(z);
        if (z>0) monotonic=monotonic && a.PressurePa<previous.PressurePa && a.DensityKgM3<previous.DensityKgM3;
        positive=positive && a.TemperatureK>0 && a.PressurePa>0 && a.DensityKgM3>0
                           && std::isfinite(a.PressurePa) && std::isfinite(a.DensityKgM3);
        if (samples) std::fprintf(samples,"%d,%.12g,%.12g,%.12g,%.12g\n",z,a.TemperatureK,a.PressurePa,a.DensityKgM3,Earth::Length(Earth::Gravity({0,0,double(z)})));
        previous=a;
    }
    if (samples) std::fclose(samples);
    Check("pressure and density decrease every kilometre through extrapolated 1500km",monotonic);
    Check("atmosphere values positive and finite through1500km",positive);
    bool boundaries=true;
    const double boundariesM[]={11000,20000,32000,47000,51000,71000};
    for (double h:boundariesM)
    {
        const double z=6356766.0*h/(6356766.0-h);
        const auto a=Earth::SampleAltitude(z-.001),b=Earth::SampleAltitude(z+.001);
        boundaries=boundaries && b.PressurePa<a.PressurePa && b.DensityKgM3<a.DensityKgM3
                   && Near(a.PressurePa,b.PressurePa,1e-6) && Near(a.DensityKgM3,b.DensityKgM3,1e-6);
    }
    const auto below=Earth::SampleAltitude(86000-.001),above=Earth::SampleAltitude(86000+.001);
    boundaries=boundaries && above.PressurePa<below.PressurePa && above.DensityKgM3<below.DensityKgM3
               && Near(below.PressurePa,above.PressurePa,1e-6);
    Check("hydrostatic layers and86km join continuous without pressure-density jumps",boundaries);

    const double seaG=Earth::Length(Earth::Gravity({}));
    const double orbitG=Earth::Length(Earth::Gravity({0,0,400000}));
    Check("surface gravity uses requested mu and mean radius",Near(seaG,9.82025048706393,1e-12),seaG);
    Check("radial gravity decreases exactly with inverse square",Near(orbitG/seaG,std::pow(Earth::MeanRadiusM/(Earth::MeanRadiusM+400000),2),1e-14),orbitG);
    const Vec3 sidePoint{Earth::MeanRadiusM+400000,0,-Earth::MeanRadiusM};
    const auto sideAir=Earth::Sample(sidePoint);
    const auto sideGravity=Earth::Gravity(sidePoint);
    Check("radial altitude works away from launchsite Z axis",Near(sideAir.AltitudeM,400000,0,1e-9),sideAir.AltitudeM);
    Check("rotated equal-radius atmosphere unchanged",Near(sideAir.PressurePa,Earth::Sample({0,0,400000}).PressurePa,1e-14));
    Check("gravity points toward translated Earth centre",Near(sideGravity.X,-orbitG,1e-14)&&sideGravity.Y==0&&sideGravity.Z==0,sideGravity.X);
    Check("central singularity explicitly guarded",Earth::Length(Earth::Gravity({0,0,-Earth::MeanRadiusM}))==0);
    Check("central orbit state invalid",!Earth::OrbitalElements({0,0,-Earth::MeanRadiusM},{}).Valid);

    const double r=Earth::MeanRadiusM+400000,vc=std::sqrt(Earth::MuM3PerS2/r);
    const auto circular=Earth::OrbitalElements({0,0,400000},{vc,0,0});
    Check("400km circular speed7672.6mps",Near(circular.CircularSpeedMps,7672.59864838501,1e-12),vc);
    Check("circular orbit bound and misses Earth",circular.Valid&&circular.Bound&&!circular.Impacting);
    Check("circular eccentricity near zero",circular.Eccentricity<1e-12,circular.Eccentricity);
    Check("circular periapsis at400km",Near(circular.PeriapsisAltitudeM,400000,0,1e-6),circular.PeriapsisAltitudeM);
    Check("circular apoapsis at400km",Near(circular.ApoapsisAltitudeM,400000,0,1e-6),circular.ApoapsisAltitudeM);
    Check("escape speed is sqrt2 times circular speed",Near(circular.EscapeSpeedMps,std::sqrt(2.0)*vc,1e-14),circular.EscapeSpeedMps);
    const double rp=r,ra=Earth::MeanRadiusM+1400000,a=(rp+ra)/2;
    const double vp=std::sqrt(Earth::MuM3PerS2*(2/rp-1/a));
    const auto ellipse=Earth::OrbitalElements({0,0,400000},{vp,0,0});
    Check("400x1400km ellipse bound and nonimpacting",ellipse.Valid&&ellipse.Bound&&!ellipse.Impacting);
    Check("elliptic periapsis400km",Near(ellipse.PeriapsisAltitudeM,400000,0,1e-6),ellipse.PeriapsisAltitudeM);
    Check("elliptic apoapsis1400km",Near(ellipse.ApoapsisAltitudeM,1400000,0,1e-6),ellipse.ApoapsisAltitudeM);
    const auto escape=Earth::OrbitalElements({0,0,400000},{1.01*circular.EscapeSpeedMps,0,0});
    Check("super-escape hyperbola unbound positiveenergy",escape.Valid&&!escape.Bound&&escape.SpecificEnergyJPerKg>0&&escape.Eccentricity>1);
    Check("unbound apoapsis is infinity",std::isinf(escape.ApoapsisAltitudeM));
    const auto parabolic=Earth::OrbitalElements({0,0,400000},{circular.EscapeSpeedMps,0,0});
    Check("exact local escape speed gives parabolic nonbound trajectory",!parabolic.Bound&&std::abs(parabolic.SpecificEnergyJPerKg)<1e-7,parabolic.SpecificEnergyJPerKg);
    const auto falling=Earth::OrbitalElements({0,0,400000},{1000,0,0});
    Check("suborbital400km trajectory intersects Earth",falling.Valid&&falling.Bound&&falling.Impacting,falling.PeriapsisAltitudeM);
    const auto radial=Earth::OrbitalElements({0,0,400000},{0,0,1000});
    Check("radial degenerate bound orbit impacting and finite",radial.Valid&&radial.Bound&&radial.Impacting&&std::isfinite(radial.ApoapsisAltitudeM));

    FILE* coast=nullptr; fopen_s(&coast,"coast_orbit.csv","w");
    const auto fine=CircularCoast(2048,coast); if (coast) std::fclose(coast);
    Check("RK4coast one orbit max relative energy drift below2e-10",fine.EnergyDrift<2e-10,fine.EnergyDrift);
    Check("RK4coast one orbit max relative angular-momentum drift below1e-10",fine.AngularMomentumDrift<1e-10,fine.AngularMomentumDrift);
    Check("RK4coast one orbit position closure below1cm",fine.PositionClosureM<.01,fine.PositionClosureM);
    Check("RK4coast one orbit velocity closure below1e-5mps",fine.VelocityClosureMps<1e-5,fine.VelocityClosureMps);
    const auto coarse=CircularCoast(128),medium=CircularCoast(256);
    Check("RK4 step refinement demonstrates fourth-order convergence",coarse.PositionClosureM>10*medium.PositionClosureM,coarse.PositionClosureM/medium.PositionClosureM);
    FILE* result=nullptr; fopen_s(&result,"test_results.json","w");
    if (result)
    {
        std::fprintf(result,"{\n  \"checks\":%d,\n  \"failures\":%d,\n  \"pass\":%s,\n  \"circular_speed_400km_mps\":%.15g,\n  \"coast_max_relative_energy_drift\":%.15g,\n  \"coast_max_relative_angular_momentum_drift\":%.15g,\n  \"coast_position_closure_m\":%.15g,\n  \"coast_velocity_closure_mps\":%.15g,\n  \"coarse_refinement_position_error_ratio\":%.15g,\n  \"max_heldout_atmosphere_relative_error\":%.15g\n}\n",
            Checks,Failures,Failures?"false":"true",vc,fine.EnergyDrift,fine.AngularMomentumDrift,fine.PositionClosureM,
            fine.VelocityClosureMps,coarse.PositionClosureM/medium.PositionClosureM,maxHeldOutError);
        std::fclose(result);
    }
    std::printf("RESULT %d/%d checks passed\n",Checks-Failures,Checks);
    return Failures?1:0;
}
