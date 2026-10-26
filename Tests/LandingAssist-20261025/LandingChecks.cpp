#include "../../Unreal/RoadsterSim/Source/RoadsterSim/Public/LandingSupervisor.h"
#include <cstdio>
#include <limits>
using namespace RoadsterLanding;
static int failures=0,checks=0;
static void Check(const char* name,bool pass,double value=0) { ++checks;if(!pass)++failures;std::printf("%s | %s | %.12g\n",pass?"PASS":"FAIL",name,value); }
static bool Near(double x,double y,double tolerance=1e-6) { return std::isfinite(x)&&std::fabs(x-y)<tolerance; }
static bool FiniteOutput(const Result& r) {
    return std::isfinite(r.TimeToContact)&&std::isfinite(r.RequiredRecoveryTime)&&std::isfinite(r.AlignmentErrorRad)
        &&std::isfinite(r.RequiredAngularImpulseNms)&&std::isfinite(r.ImpactNormalSpeedMps)&&std::isfinite(r.AuthorityScale)
        &&Finite(r.TorqueRequestBodyNm)&&Finite(r.DesiredLandingNormal)&&Finite(r.ContactCenter);
}
static Vec3 Rotate(const Vec3& v,const Vec3& axis,double angle) {
    const Vec3 n=Unit(axis);return v*std::cos(angle)+Earth::Cross(n,v)*std::sin(angle)+n*(Earth::Dot(n,v)*(1-std::cos(angle)));
}
static void Attitude(Inputs& in,const Vec3& axis,double angle) {
    in.Forward=Rotate({1,0,0},axis,angle);in.Right=Rotate({0,1,0},axis,angle);in.Up=Rotate({0,0,1},axis,angle);
}
static Inputs Start(double contactTime=1) {
    Inputs in;in.Acceleration={0,0,-10};in.Position={0,0,.65+5*contactTime*contactTime};
    in.PositiveTorqueLimitNm=in.NegativeTorqueLimitNm={12000,18000,0};in.AvailableAngularImpulseNms=100000;
    return in;
}
static TraceFn Plane(Vec3 normal={0,0,1},double offset=0) {
    const Vec3 n=Unit(normal);
    return [=](const Vec3& start,const Vec3& direction,double length,double radius)->TraceHit {
        const double distance=Earth::Dot(start,n)-offset-radius,speed=Earth::Dot(direction,n);
        if(speed>=-1e-12)return {};
        const double t=distance<=0?0:-distance/speed;
        return t<=length+1e-9?TraceHit{true,t,n}:TraceHit{};
    };
}
int main() {
    const auto floor=Plane();
    {
        const AxisPlan rest=MinimumRotationPlan(Pi,0,520,25,25);
        Check("rest-to-rest minimum is 2 sqrt angle over acceleration",rest.Valid&&Near(rest.TimeS,2*std::sqrt(Pi/25)),rest.TimeS);
        const AxisPlan away=MinimumRotationPlan(Pi,-3,520,25,25),toward=MinimumRotationPlan(Pi,3,520,25,25);
        Check("opposing spin requires additional recovery time",away.TimeS>rest.TimeS&&away.TimeS>toward.TimeS,away.TimeS);
        Check("asymmetric acceleration formula",Near(MinimumRotationPlan(1,0,1,2,8).TimeS,std::sqrt(1.25)),MinimumRotationPlan(1,0,1,2,8).TimeS);
        Check("zero authority cannot invent a rotation plan",!MinimumRotationPlan(1,0,1,0,25).Valid);
        Check("already settled needs zero authority",MinimumRotationPlan(0,0,1,0,0).Valid);
        Check("angular impulse accounts for accelerate plus brake",Near(rest.AngularImpulseNms,2*520*std::sqrt(Pi*25)),rest.AngularImpulseNms);
    }
    {
        Supervisor s;Inputs in=Start();const auto out=s.Update(.01,in,floor);
        Check("ballistic plane contact matches analytic one second",out.SensorValid&&out.ContactPredicted&&Near(out.TimeToContact,1,1e-7),out.TimeToContact);
        Check("reported impact normal speed matches ballistic speed",Near(out.ImpactNormalSpeedMps,10,1e-6),out.ImpactNormalSpeedMps);
        Check("safe upright airborne landing needs no intervention",out.AlreadyAligned&&out.Recoverable&&!out.Intervening&&Earth::Length(out.TorqueRequestBodyNm)==0);
        in.AngularVelocityBody.Z=5;const auto yaw=s.Update(.01,in,floor);
        Check("free yaw does not masquerade as tilt recovery",yaw.AlreadyAligned&&!yaw.Intervening&&yaw.TorqueRequestBodyNm.Z==0);
    }
    {
        Supervisor s;Inputs in=Start();int calls=0;bool validQueries=true;
        const auto nohit=[&](const Vec3& p,const Vec3& d,double length,double radius) {
            ++calls;validQueries &= Finite(p)&&Finite(d)&&Near(Earth::Length(d),1)&&length>0&&radius>=0;return TraceHit{};
        };
        const auto out=s.Update(.01,in,nohit);
        Check("no-hit flight never auto-levels",out.SensorValid&&!out.ContactPredicted&&!out.Intervening&&Earth::Length(out.TorqueRequestBodyNm)==0);
        Check("trace horizon has bounded finite sphere queries",validQueries&&calls<=51&&calls>0,calls);
        Check("no-hit telemetry stays finite",FiniteOutput(out));
        in.Position={7000000,0,7000000};in.Velocity={0,7800,0};in.Acceleration=Earth::Gravity(in.Position);
        Check("orbital no-hit flight has no invented landing normal correction",!s.Update(.01,in,nohit).Intervening);
    }
    {
        Supervisor s;Inputs in=Start(.2);Attitude(in,{1,0,0},Pi);
        const auto out=s.Update(.01,in,floor);
        Check("exact inversion produces a real rotation request",out.Intervening&&std::fabs(out.TorqueRequestBodyNm.X)>0,out.TorqueRequestBodyNm.X);
        Check("extremely late inverted landing is not guaranteed recoverable",!out.Recoverable&&out.RequiredRecoveryTime>out.TimeToContact,out.RequiredRecoveryTime);
        Check("late recovery still respects actual torque limits",std::fabs(out.TorqueRequestBodyNm.X)<=12000&&std::fabs(out.TorqueRequestBodyNm.Y)<=18000);
        const double direction=Sign(out.TorqueRequestBodyNm.X);Attitude(in,{1,0,0},Pi+1e-8);
        Check("antipodal perturbation does not reverse recovery branch",Sign(s.Update(.01,in,floor).TorqueRequestBodyNm.X)==direction);
    }
    {
        Supervisor s;Inputs in=Start(1.1);in.PilotStuntActive=true;Attitude(in,{1,0,0},Pi);
        const auto out=s.Update(.01,in,floor);
        Check("recoverable imminent inversion triggers before physical deadline",out.Intervening&&out.Recoverable&&out.RequiredRecoveryTime<out.TimeToContact,out.RequiredRecoveryTime);
    }
    {
        Supervisor s;Inputs in=Start(1.8);in.PilotStuntActive=true;Attitude(in,{1,0,0},1.5);in.AngularVelocityBody.X=2;
        const auto out=s.Update(.01,in,floor);
        Check("manual stunt remains free with sufficient recovery time",out.Recoverable&&!out.Intervening&&Earth::Length(out.TorqueRequestBodyNm)==0,out.RequiredRecoveryTime);
        in=Start(.25);in.PilotStuntActive=true;const auto completed=s.Update(.01,in,floor);
        Check("completed stunt before the floor needs no recovery",completed.AlreadyAligned&&!completed.Intervening&&completed.Recoverable);
    }
    {
        Supervisor s;Inputs in=Start(.1);Attitude(in,{1,0,0},Pi);int calls=0;
        const auto trace=[&](const Vec3&,const Vec3&,double,double) { ++calls;return TraceHit{}; };
        in.WheelsInContact=4;const auto attached=s.Update(.01,in,trace);
        Check("four-wheel inverted ceiling driving is never auto-levelled",!attached.Intervening&&calls==0&&Earth::Length(attached.TorqueRequestBodyNm)==0);
        in.WheelsInContact=0;in.FanSealed=true;
        Check("sealed inverted fan attachment also suppresses recovery",!s.Update(.01,in,trace).Intervening&&calls==0);
    }
    {
        Supervisor s;Inputs in=Start();in.Position={0,0,7};in.Velocity={0,0,12};
        const auto ceiling=Plane({0,0,-1},-10);const auto out=s.Update(.01,in,ceiling);
        const double expected=(12-std::sqrt(144-47))/10;
        Check("ascending ceiling contact predicted on the actual surface",out.ContactPredicted&&Near(out.TimeToContact,expected,1e-6),out.TimeToContact);
        Check("airborne ceiling landing targets wheels toward ceiling",out.DesiredLandingNormal.Z<-.99&&out.Intervening&&std::fabs(out.TorqueRequestBodyNm.X)>0);
        Attitude(in,{1,0,0},Pi);const auto ready=s.Update(.01,in,ceiling);
        Check("already ceiling-aligned airborne vehicle requests no world-up torque",ready.AlreadyAligned&&Earth::Length(ready.TorqueRequestBodyNm)==0);
    }
    {
        Supervisor s;Inputs in=Start();in.Position={0,0,10};in.Velocity={10,0,0};in.Acceleration={};
        const auto out=s.Update(.01,in,Plane({-1,0,0},-6));
        Check("wall landing uses collision normal rather than gravity up",out.ContactPredicted&&out.DesiredLandingNormal.X<-.99&&out.TorqueRequestBodyNm.Y<0,out.TorqueRequestBodyNm.Y);
    }
    {
        Supervisor s;Inputs in=Start(.6);Attitude(in,{1,0,0},Pi);in.AvailableAngularImpulseNms=0;const auto out=s.Update(.01,in,floor);
        Check("empty gas reports nonrecoverable inversion",out.Intervening&&!out.Recoverable&&out.InsufficientGas);
        Check("empty gas cannot produce torque",Earth::Length(out.TorqueRequestBodyNm)==0);
        in.AvailableAngularImpulseNms=1;const auto limited=s.Update(.01,in,floor);
        Check("one-tick angular impulse cannot exceed remaining gas budget",Earth::Length(limited.TorqueRequestBodyNm)*.01<=1+1e-8,Earth::Length(limited.TorqueRequestBodyNm)*.01);
        in=Start(1.8);in.PilotStuntActive=true;Attitude(in,{1,0,0},Pi);in.AvailableAngularImpulseNms=7000;s.Reset();
        const auto economy=s.Update(.01,in,floor);
        Check("slower burn can fit a finite angular impulse budget",economy.Recoverable&&economy.AuthorityScale<1&&economy.RequiredAngularImpulseNms<=7000,economy.RequiredRecoveryTime);
        in.Position.Z=.65+5*1.5*1.5;s.Reset();const auto tight=s.Update(.01,in,floor);
        Check("gas-limited tight deadline includes sampled feedback reserve",!tight.Recoverable&&tight.Intervening,tight.RequiredRecoveryTime);
        in=Start(.2);in.PositiveTorqueLimitNm={};in.NegativeTorqueLimitNm={};Attitude(in,{1,0,0},Pi);s.Reset();
        const auto noauthority=s.Update(.01,in,floor);
        Check("zero nozzle authority is finite and nonrecoverable",!noauthority.Recoverable&&FiniteOutput(noauthority)&&Earth::Length(noauthority.TorqueRequestBodyNm)==0);
    }
    {
        Supervisor s;Inputs in=Start(.2);Attitude(in,{1,1,0},1.4);const auto out=s.Update(.01,in,floor);
        const double fraction=std::fabs(out.TorqueRequestBodyNm.X)/12000+std::fabs(out.TorqueRequestBodyNm.Y)/18000;
        Check("combined pitch-roll respects shared allocation envelope",out.Intervening&&fraction<=1+1e-10,fraction);
        Check("landing supervisor never requests yaw torque",out.TorqueRequestBodyNm.Z==0);
    }
    {
        Supervisor s;Inputs in=Start(.65);in.PilotStuntActive=true;Attitude(in,{1,0,0},1.5);
        const auto first=s.Update(.01,in,floor);Check("deadline fixture latches recovery",first.Intervening,first.RequiredRecoveryTime);
        bool stable=true;for(int k=0;k<20;++k) { in.Position.Z=.65+5*(k%2?.79:.69)*(k%2?.79:.69);stable &= s.Update(.01,in,floor).Intervening; }
        Check("deadline timing jitter does not chatter intervention",stable);
        in.Position.Z=.65+5*1.9*1.9;for(int k=0;k<25;++k)s.Update(.01,in,floor);
        Check("recovery releases after sustained extra flight time",!s.Update(.01,in,floor).Intervening);
        in.Position.Z=.65+5*.3*.3;s.Update(.01,in,floor);
        const auto missing=[](const Vec3&,const Vec3&,double,double){return TraceHit{};};const auto firstMissing=s.Update(.01,in,missing);
        Check("missing contact never applies a stale torque",Earth::Length(firstMissing.TorqueRequestBodyNm)==0);
        for(int k=0;k<20;++k)s.Update(.01,in,missing);
        Check("missing contact eventually clears recovery latch",!s.Update(.01,in,missing).Intervening);
    }
    {
        const double nan=std::numeric_limits<double>::quiet_NaN();
        for(int failure=0;failure<5;++failure) {
            Supervisor s;Inputs in=Start(.1);Attitude(in,{1,0,0},Pi);
            const auto bad=[=](const Vec3&,const Vec3&,double length,double)->TraceHit {
                if(failure==0)return {true,nan,{0,0,1}};
                if(failure==1)return {true,-1,{0,0,1}};
                if(failure==2)return {true,length+2,{0,0,1}};
                if(failure==3)return {true,0,{nan,0,1}};
                return {true,0,{}};
            };
            const auto out=s.Update(.01,in,bad);
            Check("invalid sensor values rejected with finite zero command",!out.SensorValid&&!out.Recoverable&&Earth::Length(out.TorqueRequestBodyNm)==0&&FiniteOutput(out));
        }
        Supervisor s;Inputs in=Start();in.Position.X=nan;Check("nonfinite vehicle state rejected",!s.Update(.01,in,floor).SensorValid);
        in=Start();in.Right={1,0,0};Check("invalid body basis rejected",!s.Update(.01,in,floor).SensorValid);
        in=Start();Check("missing trace callback rejected",!s.Update(.01,in,TraceFn{}).SensorValid);
        Check("invalid update interval rejected",!s.Update(nan,in,floor).SensorValid);
        Check("stalled update cannot overbook held-command gas impulse",!s.Update(.5,in,floor).SensorValid);
        in.Enabled=false;Check("disabled feature does not query unavailable sensors",s.Update(.01,in,TraceFn{}).SensorValid);
    }
    {
        // Closed loop: supervisor at 100 Hz, physical torque integration at 1 kHz. No pose snapping.
        Supervisor s;Inputs in=Start(1.1);double roll=Pi,rate=0;Result command;bool finite=true;double used=0;
        for(int k=0;k<1100;++k) {
            Attitude(in,{1,0,0},roll);in.AngularVelocityBody={rate,0,0};
            if(k%10==0)command=s.Update(.01,in,floor);
            const double torque=command.TorqueRequestBodyNm.X,accel=torque/in.InertiaKgM2.X;
            roll+=rate*.001+.5*accel*1e-6;rate+=accel*.001;used+=std::fabs(torque)*.001;
            in.AvailableAngularImpulseNms=100000-used;
            in.Position+=in.Velocity*.001+in.Acceleration*.5e-6;in.Velocity+=in.Acceleration*.001;
            finite &= FiniteOutput(command)&&std::isfinite(roll)&&std::isfinite(rate);
        }
        const double upDot=std::cos(roll);
        std::printf("CLOSED LOOP floor inversion: alignment %.8f degrees, final roll rate %.8f rad/s, angular impulse %.8f Nms\n",std::acos(Clamp(upDot,-1,1))*180/Pi,rate,used);
        Check("physical closed loop recovers inverted vehicle before contact",finite&&upDot>.99&&std::fabs(rate)<.3,upDot);
        Check("closed-loop impulse remains within actual supplied budget",used<=100000,used);
    }
    {
        Supervisor s;Inputs in=Start();in.Position={0,0,0};in.Velocity={0,0,20};double roll=0,rate=0;Result command;
        const auto ceiling=Plane({0,0,-1},-16.6);double used=0;
        for(int k=0;k<1100;++k) {
            Attitude(in,{1,0,0},roll);in.AngularVelocityBody={rate,0,0};
            if(k%10==0)command=s.Update(.01,in,ceiling);
            const double accel=command.TorqueRequestBodyNm.X/in.InertiaKgM2.X;
            roll+=rate*.001+.5*accel*1e-6;rate+=accel*.001;used+=std::fabs(command.TorqueRequestBodyNm.X)*.001;
            in.AvailableAngularImpulseNms=100000-used;
            in.Position+=in.Velocity*.001+in.Acceleration*.5e-6;in.Velocity+=in.Acceleration*.001;
        }
        Check("physical ceiling recovery aligns wheels upward without world-level bias",std::cos(roll)<-.99&&std::fabs(rate)<.3,std::cos(roll));
    }
    {
        Supervisor s;Inputs in=Start(1.8);in.PilotStuntActive=true;in.AvailableAngularImpulseNms=7000;
        double roll=Pi,rate=0,used=0;Result command;
        for(int k=0;k<1800;++k) {
            Attitude(in,{1,0,0},roll);in.AngularVelocityBody={rate,0,0};
            if(k%10==0)command=s.Update(.01,in,floor);
            if(k%200==0)std::printf("ECONOMY t=%.2f angle=%.3f rate=%.3f scale=%.5f remaining=%.3f need=%.3f recover=%d\n",k*.001,roll,rate,command.AuthorityScale,in.AvailableAngularImpulseNms,command.RequiredAngularImpulseNms,command.Recoverable?1:0);
            const double torque=command.TorqueRequestBodyNm.X,accel=torque/in.InertiaKgM2.X;
            roll+=rate*.001+.5*accel*1e-6;rate+=accel*.001;used+=std::fabs(torque)*.001;
            in.AvailableAngularImpulseNms=std::max(0.,7000-used);
            in.Position+=in.Velocity*.001+in.Acceleration*.5e-6;in.Velocity+=in.Acceleration*.001;
        }
        std::printf("ECONOMY LOOP: alignment %.8f deg rate %.8f rad/s used %.8f Nms\n",std::acos(Clamp(std::cos(roll),-1,1))*180/Pi,rate,used);
        Check("gas-limited physical recovery respects its real budget",used<=7000+1e-5,used);
        Check("gas-limited recoverable fixture reaches wheels-first attitude",std::cos(roll)>.99&&std::fabs(rate)<.3,std::cos(roll));
    }
    std::printf("RESULT %d/%d passed\n",checks-failures,checks);return failures?1:0;
}
