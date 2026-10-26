#pragma once
// Predictive landing ATTITUDE recovery, SI units, pure C++17; no pose/velocity writes.
// Uses a finite ballistic sphere sweep and the hit's actual surface normal, including ceilings.
// The caller must supply current gas/nozzle torque and angular-impulse authority and apply requests
// through its real nozzle allocator. Recoverable is a conditional attitude estimate, not a guarantee
// of survivable touchdown, traction, geometric clearance or sufficient translational braking.
// Ballistics: NASA Glenn, Ballistic Flight Equations. Bounded double-integrator bang-bang control:
// MIT Underactuated Robotics, Dynamic Programming. Full assumptions/tests: Tests/LandingAssist-20261025.

#include "PlanetEnvironment.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace RoadsterLanding
{
    using Vec3 = Earth::Vec3;
    inline constexpr double Pi = 3.14159265358979323846;
    inline constexpr double UnavailableTimeS = 1e9; // finite sentinel, never infinity/NaN in telemetry
    inline double Clamp(double x,double lo,double hi) { return x<lo?lo:x>hi?hi:x; }
    inline bool Finite(const Vec3& v) { return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z); }
    inline Vec3 Unit(const Vec3& v) { const double l=Earth::Length(v);return l>1e-12&&std::isfinite(l)?v/l:Vec3{}; }
    inline double Sign(double x) { return x>0?1.0:x<0?-1.0:0.0; }

    struct TraceHit
    {
        bool Hit=false;
        double CenterDistanceM=0; // distance travelled by the sweep centre, in [0, supplied length]
        Vec3 Normal{0,0,1};       // unit normal pointing from solid into free space
    };
    using TraceFn = std::function<TraceHit(const Vec3& start,const Vec3& direction,double lengthM,double radiusM)>;

    struct Config
    {
        double HorizonS=2.0, SegmentS=.04;
        double SweepRadiusM=.65; // caller should choose a conservative envelope appropriate to the vehicle
        double ActuatorMarginS=.10;
        double AutomaticLeadS=.30, PilotLeadS=.10;
        double ReleaseTimeHysteresisS=.30, ReleaseHoldS=.20, ContactLossHoldS=.12;
        double AlignmentToleranceRad=.10, TiltRateToleranceRadS=.15;
        double RateResponseS=.055;
        double ImpulseReserveFactor=1.35; // reserve for sampled feedback/valve losses beyond the ideal two-burn plan
        double RotationTimeReserveFactor=1.25;
    };
    struct Inputs
    {
        bool Enabled=true, PilotStuntActive=false, FanSealed=false;
        int WheelsInContact=0;
        Vec3 Position, Velocity, Acceleration{0,0,-9.80665};
        Vec3 Forward{1,0,0}, Right{0,1,0}, Up{0,0,1}; // world-space right-handed body basis
        Vec3 AngularVelocityBody;                    // rad/s; +X roll, +Y nose-down, +Z yaw
        Vec3 InertiaKgM2{520,2250,2500};
        Vec3 PositiveTorqueLimitNm, NegativeTorqueLimitNm; // nonnegative magnitudes; Z unused
        double AvailableAngularImpulseNms=0; // conservative total remaining roll+pitch budget; 0 means no gas
    };
    struct Result
    {
        bool SensorValid=true, ContactPredicted=false, Intervening=false, Recoverable=true;
        bool AlreadyAligned=false, InsufficientGas=false;
        double TimeToContact=UnavailableTimeS, RequiredRecoveryTime=0;
        double AlignmentErrorRad=0, RequiredAngularImpulseNms=0, ImpactNormalSpeedMps=0;
        double AuthorityScale=1;
        Vec3 DesiredLandingNormal{0,0,1}, ContactCenter, TorqueRequestBodyNm;
    };

    struct AxisPlan
    {
        bool Valid=false;
        double TimeS=UnavailableTimeS, AngularImpulseNms=0;
    };
    // Exact one-axis minimum-time rest-to-rest/final-rate-zero solution, including arbitrary initial rate.
    // For rate=0 and symmetric accel a, TimeS=2*sqrt(abs(angle)/a). An opposing rate must first be cancelled.
    inline AxisPlan MinimumRotationPlan(double angle,double rate,double inertia,double positiveAccel,double negativeAccel)
    {
        if(!std::isfinite(angle)||!std::isfinite(rate)||!std::isfinite(inertia)||inertia<=0) return {};
        if(std::fabs(angle)<1e-10&&std::fabs(rate)<1e-10) return {true,0,0};
        if(!(positiveAccel>0)||!(negativeAccel>0)||!std::isfinite(positiveAccel)||!std::isfinite(negativeAccel)) return {};
        AxisPlan best;
        for(double direction:{-1.0,1.0})
        {
            const double a=direction>0?positiveAccel:negativeAccel, b=direction>0?negativeAccel:positiveAccel;
            const double square=(2*direction*angle+rate*rate/a)/(1/a+1/b);
            if(square<0||!std::isfinite(square))continue;
            const double switchRate=direction*std::sqrt(square), t1=(switchRate-rate)/(direction*a), t2=std::fabs(switchRate)/b;
            const double time=std::max(0.0,t1)+t2;
            if(t1>=-1e-10&&std::isfinite(time)&&time<best.TimeS)
                best={true,time,inertia*(std::fabs(switchRate-rate)+std::fabs(switchRate))};
        }
        return best;
    }

    class Supervisor
    {
    public:
        explicit Supervisor(const Config& config=Config{}) : C(config) {}
        void Reset() { Latched=false;ReleaseTimer=LostTimer=0;LatchedAuthorityScale=1;LastAxis={1,0,0}; }
        const Config& Settings() const { return C; }

        Result Update(double dt,const Inputs& in,const TraceFn& trace)
        {
            Result out;
            if(!Valid(dt,in)) { Reset();out.SensorValid=false;out.Recoverable=false;return out; }
            dt=std::min(dt,.25);
            // Attached ceiling/wall driving is an intentional constraint, not an airborne landing emergency.
            if(!in.Enabled || in.WheelsInContact>=4 || in.FanSealed) { Reset();return out; }
            if(!trace) { Reset();out.SensorValid=false;out.Recoverable=false;return out; }
            const Vec3 fwd=Unit(in.Forward),right=Unit(in.Right),up=Unit(in.Up);
            if(!Predict(in,trace,out)) { Reset();out.SensorValid=false;out.Recoverable=false;return out; }
            if(!out.ContactPredicted)
            {
                LostTimer+=dt;
                if(LostTimer>=C.ContactLossHoldS)Reset();
                out.Intervening=Latched; // loss hysteresis only; never command torque from a stale/missing hit
                return out;
            }
            LostTimer=0;
            const Vec3 n=out.DesiredLandingNormal;
            const Vec3 target{Earth::Dot(fwd,n),Earth::Dot(right,n),Earth::Dot(up,n)};
            double angle=std::acos(Clamp(target.Z,-1.0,1.0));
            Vec3 axis{-target.Y,target.X,0};
            const double axisLength=Earth::Length(axis);
            if(axisLength>1e-7)axis=axis/axisLength;
            else if(angle>.5*Pi)
            {
                // At exact inversion the cross product vanishes. Choose a real roll/pitch route, never zero error.
                const Vec3 rate{in.AngularVelocityBody.X,in.AngularVelocityBody.Y,0};
                if(Latched)axis=LastAxis;
                else if(Earth::Length(rate)>.05)axis=Unit(rate);
                else
                {
                    const double roll=std::min(in.PositiveTorqueLimitNm.X,in.NegativeTorqueLimitNm.X)/in.InertiaKgM2.X;
                    const double pitch=std::min(in.PositiveTorqueLimitNm.Y,in.NegativeTorqueLimitNm.Y)/in.InertiaKgM2.Y;
                    axis=roll>=pitch?Vec3{1,0,0}:Vec3{0,1,0};
                }
            }
            else axis={1,0,0};
            // Hold the selected antipodal branch through small sensor perturbations to avoid flip-direction chatter.
            if(Latched&&angle>Pi-.15&&Earth::Dot(axis,LastAxis)<0) { axis=-axis;angle=2*Pi-angle; }
            if(angle>.15)LastAxis=axis;
            out.AlignmentErrorRad=std::acos(Clamp(target.Z,-1.0,1.0));
            const double tiltRate=std::hypot(in.AngularVelocityBody.X,in.AngularVelocityBody.Y);
            out.AlreadyAligned=out.AlignmentErrorRad<=C.AlignmentToleranceRad&&tiltRate<=C.TiltRateToleranceRadS;
            if(out.AlreadyAligned)
            {
                ReleaseTimer+=dt;
                if(ReleaseTimer>=C.ReleaseHoldS)Reset();
                out.Intervening=Latched;return out;
            }

            const Vec3 error=axis*angle;
            Recovery plan=Plan(error,in);
            out.AuthorityScale=plan.Scale;
            out.RequiredAngularImpulseNms=plan.Impulse;
            out.RequiredRecoveryTime=plan.Valid?std::min(UnavailableTimeS,plan.Time*C.RotationTimeReserveFactor+C.ActuatorMarginS):UnavailableTimeS;
            out.InsufficientGas=in.AvailableAngularImpulseNms<=0||(plan.Valid&&!plan.BudgetFeasible);
            out.Recoverable=plan.Valid&&plan.BudgetFeasible&&out.TimeToContact>=out.RequiredRecoveryTime;
            const double lead=in.PilotStuntActive?C.PilotLeadS:C.AutomaticLeadS;
            const bool deadline=out.TimeToContact<=out.RequiredRecoveryTime+lead;
            if(!Latched&&(deadline||!out.Recoverable))
            {
                LatchedAuthorityScale=plan.Valid&&plan.BudgetFeasible?plan.Scale:1.0;
                Latched=true;ReleaseTimer=0;
            }
            if(Latched)
            {
                const bool roomAgain=out.Recoverable&&out.TimeToContact>out.RequiredRecoveryTime+lead+C.ReleaseTimeHysteresisS;
                ReleaseTimer=roomAgain?ReleaseTimer+dt:0;
                if(ReleaseTimer>=C.ReleaseHoldS) { Reset();return out; }
            }
            out.Intervening=Latched;
            if(!Latched||in.AvailableAngularImpulseNms<=0)return out;

            // Near-time-optimal stopping-distance rate envelope. The allocator supplies the real achieved torque.
            const double scale=plan.Valid&&plan.BudgetFeasible?plan.Scale:LatchedAuthorityScale;
            out.TorqueRequestBodyNm={AxisTorque(error.X,in.AngularVelocityBody.X,in.InertiaKgM2.X,in.PositiveTorqueLimitNm.X*scale,in.NegativeTorqueLimitNm.X*scale),
                                    AxisTorque(error.Y,in.AngularVelocityBody.Y,in.InertiaKgM2.Y,in.PositiveTorqueLimitNm.Y*scale,in.NegativeTorqueLimitNm.Y*scale),0};
            // L1 envelope prevents simultaneous requests each consuming the entire shared nozzle authority.
            const double xlim=out.TorqueRequestBodyNm.X>=0?in.PositiveTorqueLimitNm.X:in.NegativeTorqueLimitNm.X;
            const double ylim=out.TorqueRequestBodyNm.Y>=0?in.PositiveTorqueLimitNm.Y:in.NegativeTorqueLimitNm.Y;
            const double use=(xlim>0?std::fabs(out.TorqueRequestBodyNm.X)/xlim:0)+(ylim>0?std::fabs(out.TorqueRequestBodyNm.Y)/ylim:0);
            if(use>1)out.TorqueRequestBodyNm=out.TorqueRequestBodyNm/use;
            const double requestedImpulse=(std::fabs(out.TorqueRequestBodyNm.X)+std::fabs(out.TorqueRequestBodyNm.Y))*dt;
            if(requestedImpulse>in.AvailableAngularImpulseNms)out.TorqueRequestBodyNm*=in.AvailableAngularImpulseNms/requestedImpulse;
            return out;
        }

    private:
        Config C;
        bool Latched=false;
        double ReleaseTimer=0,LostTimer=0;
        double LatchedAuthorityScale=1;
        Vec3 LastAxis{1,0,0};
        struct Recovery { bool Valid=false,BudgetFeasible=false;double Time=UnavailableTimeS,Impulse=0,Scale=1; };

        bool Valid(double dt,const Inputs& in) const
        {
            if(!(dt>0)||dt>.25||!std::isfinite(dt)||!Finite(in.Position)||!Finite(in.Velocity)||!Finite(in.Acceleration)
                ||!Finite(in.Forward)||!Finite(in.Right)||!Finite(in.Up)||!Finite(in.AngularVelocityBody)||!Finite(in.InertiaKgM2)
                ||!Finite(in.PositiveTorqueLimitNm)||!Finite(in.NegativeTorqueLimitNm)||!std::isfinite(in.AvailableAngularImpulseNms)
                ||in.AvailableAngularImpulseNms<0||in.InertiaKgM2.X<=0||in.InertiaKgM2.Y<=0
                ||in.PositiveTorqueLimitNm.X<0||in.PositiveTorqueLimitNm.Y<0||in.NegativeTorqueLimitNm.X<0||in.NegativeTorqueLimitNm.Y<0)return false;
            const Vec3 f=Unit(in.Forward),r=Unit(in.Right),u=Unit(in.Up);
            if(Earth::Length(f)<.99||Earth::Length(r)<.99||Earth::Length(u)<.99||std::fabs(Earth::Dot(f,r))>.01
                ||std::fabs(Earth::Dot(f,u))>.01||std::fabs(Earth::Dot(r,u))>.01||Earth::Dot(Earth::Cross(f,r),u)<.99)return false;
            const double values[]={C.HorizonS,C.SegmentS,C.SweepRadiusM,C.ActuatorMarginS,C.AutomaticLeadS,C.PilotLeadS,
                C.ReleaseTimeHysteresisS,C.ReleaseHoldS,C.ContactLossHoldS,C.AlignmentToleranceRad,C.TiltRateToleranceRadS,C.RateResponseS,C.ImpulseReserveFactor,C.RotationTimeReserveFactor};
            for(double v:values)if(!std::isfinite(v)||v<0)return false;
            return C.HorizonS>0&&C.HorizonS<=3&&C.SegmentS>=.005&&C.SegmentS<=.1&&C.SweepRadiusM>=0
                &&C.AlignmentToleranceRad>0&&C.AlignmentToleranceRad<Pi/2&&C.TiltRateToleranceRadS>0&&C.RateResponseS>0
                &&C.ImpulseReserveFactor>=1&&C.RotationTimeReserveFactor>=1;
        }
        bool Predict(const Inputs& in,const TraceFn& trace,Result& out) const
        {
            auto position=[&](double t) { return in.Position+in.Velocity*t+in.Acceleration*(.5*t*t); };
            const int count=std::max(1,static_cast<int>(std::ceil(C.HorizonS/C.SegmentS)));
            const double step=C.HorizonS/count;
            for(int i=0;i<count;++i)
            {
                const double t0=i*step,t1=(i+1)*step;
                const Vec3 a=position(t0),b=position(t1),delta=b-a;
                if(!Finite(a)||!Finite(b))return false;
                const double length=Earth::Length(delta);if(!std::isfinite(length))return false;if(length<1e-9)continue;
                const Vec3 direction=delta/length;const TraceHit hit=trace(a,direction,length,C.SweepRadiusM);
                if(!hit.Hit)continue;
                if(!std::isfinite(hit.CenterDistanceM)||hit.CenterDistanceM<0||hit.CenterDistanceM>length+1e-6||!Finite(hit.Normal))return false;
                const Vec3 n=Unit(hit.Normal);if(Earth::Length(n)<.99)return false;
                // Refine the chord time against the hit's tangent plane using the actual quadratic trajectory.
                const Vec3 center=a+direction*Clamp(hit.CenterDistanceM,0,length);
                double lo=t0,hi=t1;
                const double fa=Earth::Dot(a-center,n),fb=Earth::Dot(b-center,n);
                double contact=t0+step*Clamp(hit.CenterDistanceM/length,0,1);
                if(fa*fb<=0)
                {
                    for(int k=0;k<30;++k) { const double m=.5*(lo+hi),fm=Earth::Dot(position(m)-center,n);(fm*fa>0?lo:hi)=m; }
                    contact=.5*(lo+hi);
                }
                const Vec3 impactVelocity=in.Velocity+in.Acceleration*contact;
                // A back-facing/retreating hit is not an approaching landing surface.
                if(Earth::Dot(impactVelocity,n)>1e-6)return false;
                out.ContactPredicted=true;out.TimeToContact=contact;out.DesiredLandingNormal=n;out.ContactCenter=position(contact);
                out.ImpactNormalSpeedMps=std::max(0.0,-Earth::Dot(impactVelocity,n));return true;
            }
            return true;
        }
        Recovery Plan(const Vec3& error,const Inputs& in) const
        {
            Recovery fastest,best;
            const bool roll=std::fabs(error.X)>1e-6||std::fabs(in.AngularVelocityBody.X)>1e-6;
            const bool pitch=std::fabs(error.Y)>1e-6||std::fabs(in.AngularVelocityBody.Y)>1e-6;
            // Search a family of lower-authority plans when gas cannot fund the fastest burn.
            // Two axes reserve half authority each; sum their times conservatively rather than claim full 3-D optimality.
            const double share=roll&&pitch?.5:1.0;
            for(int k=0;k<40;++k)
            {
                const double scale=std::pow(.8,k),s=scale*share;
                // Do not spend the economic plan's braking reserve by accelerating harder halfway through a burn.
                if(Latched&&s>LatchedAuthorityScale*(1+1e-10))continue;
                const auto x=MinimumRotationPlan(error.X,in.AngularVelocityBody.X,in.InertiaKgM2.X,
                    in.PositiveTorqueLimitNm.X*s/in.InertiaKgM2.X,in.NegativeTorqueLimitNm.X*s/in.InertiaKgM2.X);
                const auto y=MinimumRotationPlan(error.Y,in.AngularVelocityBody.Y,in.InertiaKgM2.Y,
                    in.PositiveTorqueLimitNm.Y*s/in.InertiaKgM2.Y,in.NegativeTorqueLimitNm.Y*s/in.InertiaKgM2.Y);
                if(!x.Valid||!y.Valid)continue;
                const double time=x.TimeS+y.TimeS,impulse=(x.AngularImpulseNms+y.AngularImpulseNms)*C.ImpulseReserveFactor;
                if(!fastest.Valid||time<fastest.Time)fastest={true,false,time,impulse,s};
                if(impulse<=in.AvailableAngularImpulseNms&&(!best.Valid||time<best.Time))best={true,true,time,impulse,s};
            }
            return best.Valid?best:fastest;
        }
        double AxisTorque(double error,double rate,double inertia,double positive,double negative) const
        {
            if(std::fabs(error)<.25*C.AlignmentToleranceRad&&std::fabs(rate)<.5*C.TiltRateToleranceRadS)return 0;
            const double brake=rate>=0?negative:positive;
            if(brake<=0)return Clamp(Sign(error)*std::max(positive,negative),-negative,positive);
            const double stoppingAngle=rate*std::fabs(rate)/(2*brake/inertia);
            // A smoothed bang-bang switching curve starts braking at the actual stopping distance.
            // Tracking sqrt(2*a*angle) with a lagged rate servo brakes too late and can waste a finite gas budget.
            const double width=std::max(.002,std::fabs(rate)*C.RateResponseS*.4);
            const double command=Clamp((error-stoppingAngle)/width,-1.0,1.0);
            return command>=0?command*positive:command*negative;
        }
    };
}
