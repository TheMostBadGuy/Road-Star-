using System;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public enum TrimLevel : byte { Performance, Thrust, Rocket }
    public enum RoadsterTrim : byte { Performance, Thrust, Rocket }
    public enum FanMode : byte { Off, Braking, LowDrag, MaxGrip, Custom }
    public enum AccelMode : byte { Chill, Standard, Sport }
    public enum HandlingMode : byte { Comfort, Balance, Sport }
    public enum AeroPreset : byte { Auto, LowDrag, HighDownforce, Custom }
    public enum DynamicsPreset : byte { Comfort, Balance, Sport, Custom, Track }

    public struct TraceHit
    {
        public bool bHit;
        public double Dist;
        public V3 Point;
        public V3 Normal;
    }

    public delegate TraceHit WorldTraceFn(V3 start, V3 dir, double len, double radius);

    public struct RoadsterInputs
    {
        public double Throttle;   // 0..1
        public double Brake;      // 0..1
        public double Steer;      // -1..1 (+ = right)
        public double Handbrake;  // 0..1
        public double Boost;      // 0..1 (rear thrusters)
        public double Lift;       // 0..1 (corner lift thrusters)
        public double Pitch;      // -1..1 (+ = nose up)
        public double Roll;       // -1..1 (+ = roll right)
        public double Yaw;        // -1..1 (+ = nose right)
        public bool bWantReverse;
        public bool bDRS;
        public bool bAttitudeBrake;
    }

    public class RoadsterSettings
    {
        public DynamicsPreset Dynamics = DynamicsPreset.Sport;
        public AccelMode Acceleration = AccelMode.Sport;
        public HandlingMode Handling = HandlingMode.Sport;
        public bool bLowSuspension = false;
        public AeroPreset Aero = AeroPreset.Auto;
        public FanMode Fan = FanMode.MaxGrip;
        public double FanCustomKPa = 4.0;
        public bool bHoodVentOpen = false;
        public bool bBargeDeployed = true;
        public bool bESC = true;
        public bool bTCS = true;
        public bool bABS = true;
        public double RegenLevel = 1.0;
        public double BoostLevel = 1.0;
        public bool bThrustersArmed = true;
        public bool bFlightAssist = true;
        public bool bInfiniteGas = false;
        public bool bInfiniteBattery = false;
        public bool bActiveSuspension = true;
        public bool bLaunchControl = true;
        public bool bHoverHold = false;
        public double HoverAltitudeM = 2.0;

        public double AeroFrontShare() => 0.48;
        public double BrakeFrontShare() => 0.62;
    }

    public struct RoadsterWheel
    {
        public V3 Hardpoint;
        public double Radius;
        public double Inertia;
        public double Width;
        public bool bFront;
        public bool bLeft;

        public double Omega; // angular velocity (rad/s)
        public double Spin;  // visual rotation angle
        public double Steer; // steer angle (rad)
        public double Length;
        public double LengthPrev;
        public double LengthVel;
        public double Compression;
        public bool bContact;
        public V3 ContactPoint;
        public V3 ContactNormal;
        public double Load;
        public double Fx, Fy;
        public double SlideSpeed;
        public double GripUsage;
        public double DriveTorque;
        public double BrakeTorque;
        public double TCScale;
        public double ABSScale;
    }

    public struct RoadsterTelemetry
    {
        public double Speed;
        public double ForwardSpeed;
        public double AltitudeM;
        public double VerticalSpeedMps;
        public V3 AccelG;
        public double PowerKW;
        public double MotorPowerKW;
        public double RegenKW;
        public double FanPowerKW;
        public double FanRPM;
        public double FanPressureKPa;
        public double FanTargetKPa;
        public double FanDownforceN;
        public double FanSeal;
        public double SkirtDeploy;
        public double WingDownforceN;
        public double DragN;
        public double WingAngle;
        public double FrontWingAngle;
        public double TankBar;
        public double TankPSI;
        public double TankPercent;
        public double ThrustN;
        public double ThrustRear;
        public double ThrustYawL;
        public double ThrustYawR;
        public double[] ThrustCorner; // FL, FR, RL, RR
        public double SOC;
        public double BatteryKWh;
        public double MotorRPM;
        public double RearTorque;
        public double UpDotWorld;
        public int WheelsInContact;
        public bool bReverse;
        public bool bTC;
        public bool bABS;
        public bool bESC;
        public bool bLaunch;
        public bool bAirborne;
        public double AirTime;

        // Timers
        public bool bRunActive;
        public double RunTime;
        public double RunDist;
        public double T60;
        public double T100kmh;
        public double TQuarter;
        public double TopSpeed;
        public double PeakG;
    }

    public class RoadsterVehicle
    {
        // Dimensions and parameters
        public TrimLevel CurrentTrim = TrimLevel.Rocket;
        public double DryMassKg = 1700.0;
        public double TrimExtraMassKg = 330.0; // Rocket trim
        public double CGHeight = 0.42;
        public double CGForward = -0.226;
        public V3 Inertia = new V3(520, 2250, 2500);
        public double Wheelbase = 2.825;
        public double TrackFront = 1.70;
        public double TrackRear = 1.68;
        public double RideHeight = 0.120;
        public double LowRideDrop = 0.025;

        // Powertrain & Aero
        public double RearMotorPeakKW = 520.0; // each (2 motors > 1000 kW)
        public double MotorTorqueNm = 760.0;
        public double GearRatio = 8.3;
        public double TopSpeedMps = 105.0; // ~235 mph
        public double BatteryKWh = 120.0;
        public double EnergyKWh = 120.0;

        // Suspension
        public double SuspensionTravel = 0.11;
        public double StaticCompression = 0.060;
        public double SpringRateFront = 70000.0;
        public double SpringRateRear = 82000.0;
        public double DampingBump = 5200.0;
        public double DampingRebound = 7800.0;
        public double AntiRollFront = 42000.0;
        public double AntiRollRear = 28000.0;

        // Fans & Skirts
        public double FanMaxPressureKPa = 8.0;
        public double FanEffectiveArea = 4.2;
        public double SkirtDeployMax = 0.22;
        public double FanMaxRPM = 16000.0;
        public double FanSpoolTime = 0.30;
        public double FanMaxPowerKW = 80.0;

        // Thruster nozzle positions (body frame: +X fwd, -Y right / +Y left, +Z up)
        public V3[] RearNozzlePos = new V3[]
        {
            new V3(-2.15, -0.352, 0.584),
            new V3(-2.15, -0.118, 0.584),
            new V3(-2.15, 0.118, 0.584),
            new V3(-2.15, 0.352, 0.584)
        };
        public V3[] LiftNozzlePos = new V3[]
        {
            new V3(0.975, 0.855, 0.09),   // FL
            new V3(0.975, -0.855, 0.09),  // FR
            new V3(-2.00, 0.740, 0.20),   // RL
            new V3(-2.00, -0.740, 0.20)   // RR
        };
        public V3[] YawNozzlePos = new V3[]
        {
            new V3(1.97, 0.918, 0.235),  // Left firing
            new V3(1.97, -0.918, 0.235)  // Right firing
        };

        // State
        public V3 X, V, W;
        public Quat Q = Quat.Identity;
        public RoadsterWheel[] Wheels = new RoadsterWheel[4];
        public RoadsterSettings Set = new RoadsterSettings();
        public RoadsterTelemetry T = new RoadsterTelemetry();
        public ColdGasSystem Gas;

        public double FanRPM = 0.0;
        public double SkirtDeploy = 0.0;
        public double WingAngle = 1.0;
        public double FrontWingAngle = 1.0;
        public double HeavePreload = 0.0;
        public bool bReverseGear = false;

        public V3 CGBody() => new V3(CGForward, 0, CGHeight - (Set.bLowSuspension ? LowRideDrop : 0.0));
        public double TotalMass() => DryMassKg + TrimExtraMassKg + (Gas != null ? Gas.UsableGasKg : 0.0);
        public V3 BodyOriginWorld() => X - Q.Rotate(CGBody());
        public V3 BodyToWorld(V3 pBody) => X + Q.Rotate(pBody - CGBody());

        public double MinLen() => 0.10;
        public double MaxLen() => MinLen() + SuspensionTravel;

        public RoadsterVehicle()
        {
            Gas = new ColdGasSystem(600.0);
            T.ThrustCorner = new double[4];
            Init(TrimLevel.Rocket);
        }

        public void Init(TrimLevel trim)
        {
            CurrentTrim = trim;
            switch (trim)
            {
                case TrimLevel.Performance:
                    TrimExtraMassKg = 0.0;
                    Gas.TankVolumeL = 0.0;
                    Gas.IsArmed = false;
                    break;
                case TrimLevel.Thrust:
                    TrimExtraMassKg = 135.0;
                    Gas.TankVolumeL = 200.0;
                    Gas.IsArmed = true;
                    Gas.Fill(379.0, 293.15);
                    break;
                case TrimLevel.Rocket:
                    TrimExtraMassKg = 330.0;
                    Gas.TankVolumeL = 600.0;
                    Gas.IsArmed = true;
                    Gas.Fill(379.0, 293.15);
                    break;
            }

            double hw_f = TrackFront * 0.5, hw_r = TrackRear * 0.5;
            double xf = Wheelbase * 0.5, xr = -Wheelbase * 0.5;

            // FL, FR, RL, RR
            SetupWheel(0, xf, hw_f, true, true);
            SetupWheel(1, xf, -hw_f, true, false);
            SetupWheel(2, xr, hw_r, false, true);
            SetupWheel(3, xr, -hw_r, false, false);

            EnergyKWh = BatteryKWh;
            RefillTank();
        }

        private void SetupWheel(int i, double x, double y, bool front, bool left)
        {
            Wheels[i] = new RoadsterWheel
            {
                bFront = front,
                bLeft = left,
                Radius = front ? 0.3425 : 0.3672,
                Inertia = front ? 1.35 : 1.85,
                Width = front ? 0.295 : 0.335,
                Hardpoint = new V3(x, y, (front ? 0.3425 : 0.3672) + (MaxLen() - StaticCompression)),
                Length = MaxLen() - StaticCompression,
                LengthPrev = MaxLen() - StaticCompression,
                TCScale = 1.0,
                ABSScale = 1.0
            };
        }

        public void RefillTank()
        {
            Gas.Fill(379.0, 293.15);
        }

        public void Recharge()
        {
            EnergyKWh = BatteryKWh;
        }

        public void Place(V3 bodyOriginWorld, Quat q, double speedForward = 0.0)
        {
            Q = q.Normalized();
            X = bodyOriginWorld + Q.Rotate(CGBody());
            V = Q.Fwd() * speedForward;
            W = new V3();
            for (int i = 0; i < 4; i++)
            {
                Wheels[i].Omega = speedForward / Wheels[i].Radius;
                Wheels[i].Length = Wheels[i].LengthPrev = MaxLen() - StaticCompression;
                Wheels[i].TCScale = Wheels[i].ABSScale = 1.0;
            }
            bReverseGear = false;
            T.bRunActive = false;
        }

        public void Step(double h, RoadsterInputs In, WorldTraceFn trace)
        {
            V3 fwd = Q.Fwd(), right = Q.Right(), up = Q.Up();
            double mass = TotalMass();
            V3 gravity = new V3(0, 0, -RMath.kG);
            V3 F = gravity * mass;
            V3 Tq = new V3();

            void AddForceAt(V3 force, V3 ptWorld)
            {
                F += force;
                Tq += (ptWorld - X).Cross(force);
            }

            V3 velBody = Q.Unrotate(V);
            double vFwd = velBody.x;
            double speed = V.Len();
            V3 wBody = Q.Unrotate(W);

            // Reverse latch
            if (In.bWantReverse && Math.Abs(vFwd) < 1.0) bReverseGear = true;
            if (!In.bWantReverse && In.Throttle > 0.1) bReverseGear = false;

            double pedalDrive = bReverseGear ? In.Brake : In.Throttle;
            double pedalBrake = bReverseGear ? In.Throttle : In.Brake;

            // Steer-by-wire speed sensitivity
            double steerLimit = 32.0 * RMath.kPi / 180.0 / (1.0 + Math.Pow(Math.Abs(vFwd) / 30.0, 2.0) * 1.6);
            double steer = RMath.Clamp(In.Steer, -1.0, 1.0) * Math.Max(steerLimit, 2.5 * RMath.kPi / 180.0);
            Wheels[0].Steer = Wheels[1].Steer = steer;
            Wheels[2].Steer = Wheels[3].Steer = 0.0;

            // Aero preset
            double wingTarget = Set.Aero == AeroPreset.LowDrag || In.bDRS ? 0.0 : (Set.Aero == AeroPreset.HighDownforce ? 1.0 : (Math.Abs(vFwd) > 20.0 ? 1.0 : 0.45));
            WingAngle = RMath.MoveTo(WingAngle, wingTarget, h * 2.0);
            double qDynamic = 0.5 * 1.20 * speed * speed;
            double wingDownforce = qDynamic * (1.2 + WingAngle * 1.8) * Math.Pow(RMath.Clamp(vFwd / Math.Max(speed, 0.01), 0.0, 1.0), 2.0);
            double dragN = qDynamic * (0.30 * 1.92 + WingAngle * 0.17);
            AddForceAt(V * (-dragN / Math.Max(speed, 0.01)), BodyToWorld(new V3(-0.2, 0, 0.55)));
            AddForceAt(-up * (wingDownforce * 0.48), BodyToWorld(new V3(Wheelbase * 0.5, 0, 0.4)));
            AddForceAt(-up * (wingDownforce * 0.52), BodyToWorld(new V3(-Wheelbase * 0.5, 0, 0.4)));
            T.WingDownforceN = wingDownforce;
            T.DragN = dragN;
            T.WingAngle = WingAngle;

            // 4-Fan Suction & Skirts
            double fanTargetKPa = 0.0;
            switch (Set.Fan)
            {
                case FanMode.Off: fanTargetKPa = 0.0; break;
                case FanMode.Braking: fanTargetKPa = pedalBrake > 0.05 ? FanMaxPressureKPa * RMath.Clamp(pedalBrake * 1.6, 0.0, 1.0) : 0.0; break;
                case FanMode.LowDrag: fanTargetKPa = FanMaxPressureKPa * 0.15; break;
                case FanMode.MaxGrip: fanTargetKPa = FanMaxPressureKPa; break;
                case FanMode.Custom: fanTargetKPa = RMath.Clamp(Set.FanCustomKPa, 0.0, FanMaxPressureKPa); break;
            }
            if (EnergyKWh <= 0.0 && !Set.bInfiniteBattery) fanTargetKPa = 0.0;

            SkirtDeploy = RMath.MoveTo(SkirtDeploy, fanTargetKPa > 0.8 ? 1.0 : 0.0, h * 4.0);
            double fanRpmTarget = FanMaxRPM * Math.Sqrt(RMath.Clamp(fanTargetKPa / FanMaxPressureKPa, 0.0, 1.0));
            FanRPM += (fanRpmTarget - FanRPM) * RMath.Clamp(h / FanSpoolTime, 0.0, 1.0);
            double fanRpmFrac = FanRPM / FanMaxRPM;

            // Floor clearance probe
            double floorH = RideHeight - (Set.bLowSuspension ? LowRideDrop : 0.0);
            V3 floorCenter = BodyToWorld(new V3(-0.20, 0, floorH + 0.20));
            TraceHit floorHit = trace(floorCenter, -up, 0.70, 0.0);
            double seal = 0.0;
            if (floorHit.bHit)
            {
                double clearance = Math.Max(0.005, floorHit.Dist - 0.20 - SkirtDeployMax * SkirtDeploy);
                seal = 1.0 / (1.0 + (clearance * clearance) / (0.025 * 0.025));
            }

            double fanDownforce = fanTargetKPa * 1000.0 * (fanRpmFrac * fanRpmFrac) * FanEffectiveArea * seal;
            AddForceAt(-up * fanDownforce, BodyToWorld(new V3(-0.20, 0, 0.1)));
            double fanPowerKW = FanMaxPowerKW * Math.Pow(fanRpmFrac, 3.0);

            T.FanRPM = FanRPM;
            T.FanPressureKPa = (fanDownforce / FanEffectiveArea) / 1000.0;
            T.FanDownforceN = fanDownforce;
            T.FanSeal = seal;
            T.FanPowerKW = fanPowerKW;
            T.SkirtDeploy = SkirtDeploy;

            // Suspension & Wheel Contacts
            int contacts = 0;
            for (int i = 0; i < 4; i++)
            {
                ref RoadsterWheel w = ref Wheels[i];
                V3 hp = BodyToWorld(w.Hardpoint);
                double reach = MaxLen() + w.Radius;
                TraceHit hit = trace(hp, -up, reach + 0.02, 0.04);
                w.LengthPrev = w.Length;

                if (hit.bHit && (hit.Dist - w.Radius) < MaxLen())
                {
                    w.bContact = true;
                    contacts++;
                    w.Length = RMath.Clamp(hit.Dist - w.Radius, MinLen() - 0.06, MaxLen());
                    w.ContactPoint = hit.Point;
                    w.ContactNormal = hit.Normal;
                }
                else
                {
                    w.bContact = false;
                    w.Length = Math.Min(MaxLen(), w.Length + 0.6 * h);
                }

                w.LengthVel = (w.Length - w.LengthPrev) / h;
                w.Compression = MaxLen() - w.Length;
            }

            bool airborne = contacts == 0;

            // Cold-Gas Thrusters
            if (CurrentTrim != TrimLevel.Performance && Gas != null)
            {
                double auth = RMath.Clamp(Set.BoostLevel, 0.25, 1.0);

                // Rear Boost
                double rearCmd = (CurrentTrim == TrimLevel.Thrust || CurrentTrim == TrimLevel.Rocket) ? In.Boost * auth * Gas.RearThrustMaxN : 0.0;
                for (int k = 0; k < 4; k++) Gas.CommandThrust(k, rearCmd * 0.25);

                // Corner Lift & Flight Assist (Rocket trim)
                double[] cornerCmd = new double[4];
                double yawL = 0.0, yawR = 0.0;

                if (CurrentTrim == TrimLevel.Rocket && Gas.IsArmed)
                {
                    double baseLift = RMath.Clamp(In.Lift, 0.0, 1.0);

                    // Hover Hold PID
                    if (Set.bHoverHold)
                    {
                        double capacity = Gas.LiftThrustMaxN * 4.0 * auth;
                        double accelReq = RMath.Clamp((Set.HoverAltitudeM - X.z) * 4.0 - V.z * 3.5, -8.0, 12.0);
                        double needed = mass * (RMath.kG + accelReq);
                        baseLift = Math.Max(baseLift, RMath.Clamp(needed / Math.Max(1.0, capacity), 0.0, 1.0));
                    }

                    // Attitude torque demand (pitch, roll, yaw)
                    V3 tauB = new V3();
                    if (In.bAttitudeBrake && airborne)
                    {
                        tauB = new V3(-wBody.x * 15000.0, -wBody.y * 28000.0, 0);
                    }
                    else if (Set.bFlightAssist && (airborne || baseLift > 0.05))
                    {
                        tauB = new V3(
                            (In.Roll * 3.0 - wBody.x) * 12000.0,
                            (-In.Pitch * 2.5 - wBody.y) * 24000.0,
                            0
                        );
                    }
                    else if (airborne)
                    {
                        tauB = new V3(In.Roll * 20000.0, -In.Pitch * 35000.0, 0);
                    }

                    // Distribute to 4 corner lift thrusters
                    double cap = Gas.MaxThrustN(ColdGasSystem.LiftFL) * auth;
                    for (int k = 0; k < 4; k++)
                    {
                        double rollArm = LiftNozzlePos[k].y;
                        double pitchArm = LiftNozzlePos[k].x - CGForward;
                        double deltaT = (tauB.x / 1.5) * (rollArm > 0 ? 0.5 : -0.5) - (tauB.y / 2.8) * (pitchArm > 0 ? 0.5 : -0.5);
                        cornerCmd[k] = RMath.Clamp(baseLift * cap + deltaT, 0.0, cap);
                    }

                    // Yaw thrusters
                    double yawCmd = RMath.Clamp(In.Yaw, -1.0, 1.0);
                    if (yawCmd > 0.02) yawR = yawCmd * Gas.MaxThrustN(ColdGasSystem.YawLeftFiring) * auth;
                    if (yawCmd < -0.02) yawL = -yawCmd * Gas.MaxThrustN(ColdGasSystem.YawRightFiring) * auth;
                }

                for (int k = 0; k < 4; k++) Gas.CommandThrust(ColdGasSystem.LiftFL + k, cornerCmd[k]);
                Gas.CommandThrust(ColdGasSystem.YawLeftFiring, yawR);
                Gas.CommandThrust(ColdGasSystem.YawRightFiring, yawL);

                Gas.Step(h, 101325.0, Set.bInfiniteGas);

                // Apply delivered forces
                double rearTotal = 0.0, liftTotal = 0.0;
                for (int k = 0; k < 4; k++)
                {
                    double fn = Gas.Nozzles[k].ThrustN;
                    if (fn > 0.0) AddForceAt(fwd * fn, BodyToWorld(RearNozzlePos[k]));
                    rearTotal += fn;
                }
                for (int k = 0; k < 4; k++)
                {
                    double fn = Gas.Nozzles[ColdGasSystem.LiftFL + k].ThrustN;
                    T.ThrustCorner[k] = fn / Gas.LiftThrustMaxN;
                    if (fn > 0.0) AddForceAt(up * fn, BodyToWorld(LiftNozzlePos[k]));
                    liftTotal += fn;
                }

                double fyR = Gas.Nozzles[ColdGasSystem.YawLeftFiring].ThrustN;
                double fyL = Gas.Nozzles[ColdGasSystem.YawRightFiring].ThrustN;
                if (fyR > 0.0) AddForceAt(right * fyR, BodyToWorld(YawNozzlePos[0]));
                if (fyL > 0.0) AddForceAt(-right * fyL, BodyToWorld(YawNozzlePos[1]));

                T.ThrustRear = rearTotal / (Gas.RearThrustMaxN * 4.0);
                T.ThrustYawR = fyR / Gas.YawThrustMaxN;
                T.ThrustYawL = fyL / Gas.YawThrustMaxN;
                T.ThrustN = rearTotal + liftTotal + fyR + fyL;
                T.TankBar = Gas.DisplayBar;
                T.TankPSI = Gas.DisplayPSI;
                T.TankPercent = Gas.GasFraction * 100.0;
            }

            // Powertrain Torques
            double throttle = Math.Pow(RMath.Clamp(pedalDrive, 0.0, 1.0), 1.0);
            double wheelTorqueMax = MotorTorqueNm * GearRatio * 0.93;
            double rearReq = throttle * wheelTorqueMax * (Set.Acceleration == AccelMode.Chill ? 0.4 : (Set.Acceleration == AccelMode.Standard ? 0.75 : 1.0));
            if (bReverseGear) rearReq *= 0.25;

            // Per-wheel forces (Suspension + Pacejka tire)
            double brakeFront = Set.BrakeFrontShare();
            for (int i = 0; i < 4; i++)
            {
                ref RoadsterWheel w = ref Wheels[i];
                bool front = w.bFront;
                double kSpring = front ? SpringRateFront : SpringRateRear;
                double maxBrake = front ? 5400.0 * (brakeFront / 0.6) : 4600.0 * ((1.0 - brakeFront) / 0.4);
                double brakeT = RMath.Clamp(pedalBrake, 0.0, 1.0) * maxBrake + (front ? 0.0 : In.Handbrake * 5000.0);

                double driveT = front ? 0.0 : (rearReq * (bReverseGear ? -1.0 : 1.0));

                if (w.bContact)
                {
                    double c = w.Compression;
                    double fs = kSpring * c + HeavePreload;
                    double fd = -DampingBump * w.LengthVel;
                    double fz = Math.Max(0.0, fs + fd);
                    w.Load = fz;
                    AddForceAt(up * fz, w.ContactPoint);

                    // Pacejka tire model
                    V3 wf = fwd * Math.Cos(w.Steer) + right * Math.Sin(w.Steer);
                    V3 n = w.ContactNormal;
                    V3 tf = (wf - n * wf.Dot(n)).Norm();
                    V3 ts = n.Cross(tf).Norm();
                    V3 vc = V + W.Cross(w.ContactPoint - X);
                    double vx = vc.Dot(tf), vy = vc.Dot(ts);

                    double fmax = 1.45 * fz; // Peak mu ~ 1.45
                    double k0 = (w.Omega * w.Radius - vx) / Math.Max(Math.Abs(vx), 1.0);
                    double alpha = -Math.Atan2(vy, Math.Max(Math.Abs(vx), 1.0));

                    double fx = fmax * Math.Sin(1.65 * Math.Atan(10.0 * k0));
                    double fy = fmax * Math.Sin(1.65 * Math.Atan(12.0 * alpha));

                    // TCS / ABS
                    if (Set.bTCS && Math.Abs(driveT) > 1.0 && Math.Abs(k0) > 0.12)
                    {
                        driveT *= 0.5;
                        T.bTC = true;
                    }
                    if (Set.bABS && brakeT > 10.0 && k0 < -0.15)
                    {
                        brakeT *= 0.4;
                        T.bABS = true;
                    }

                    // Wheel spin integration
                    double Ieff = w.Inertia;
                    w.Omega += h * (driveT - brakeT * Math.Sign(w.Omega) - w.Radius * fx) / Ieff;

                    AddForceAt(tf * fx + ts * fy, w.ContactPoint);
                    w.Fx = fx; w.Fy = fy;
                    w.SlideSpeed = Math.Sqrt(Math.Pow(w.Omega * w.Radius - vx, 2.0) + vy * vy);
                }
                else
                {
                    w.Omega += h * (driveT - brakeT * Math.Sign(w.Omega)) / w.Inertia;
                    w.Omega *= (1.0 - 0.2 * h);
                }

                w.Spin = (w.Spin + w.Omega * h) % (2.0 * RMath.kPi);
            }

            // 6-DoF Rigid Body Integration
            V3 accel = F / mass;
            V += accel * h;
            X += V * h;

            V3 tb = Q.Unrotate(Tq);
            V3 wb = Q.Unrotate(W);
            V3 dwb = new V3(tb.x / Inertia.x, tb.y / Inertia.y, tb.z / Inertia.z);
            wb += dwb * h;
            W = Q.Rotate(wb);

            double wl = W.Len();
            if (wl > 1e-9)
            {
                Q = (Quat.AxisAngle(W / wl, wl * h) * Q).Normalized();
            }

            // Battery consumption
            double elecKW = Math.Abs(rearReq * 2.0 * Math.Abs(vFwd / 0.3672)) / 1000.0 / 0.93 + fanPowerKW + 1.2;
            if (!Set.bInfiniteBattery)
            {
                EnergyKWh = Math.Max(0.0, EnergyKWh - (elecKW * h / 3600.0));
            }
            else EnergyKWh = BatteryKWh;

            // Telemetry updates
            V3 aBody = Q.Unrotate(accel - gravity) / RMath.kG;
            T.AccelG = aBody;
            T.Speed = speed;
            T.ForwardSpeed = vFwd;
            T.AltitudeM = X.z;
            T.VerticalSpeedMps = V.z;
            T.MotorPowerKW = elecKW;
            T.SOC = EnergyKWh / BatteryKWh;
            T.BatteryKWh = EnergyKWh;
            T.WheelsInContact = contacts;
            T.bAirborne = contacts == 0;
            T.UpDotWorld = up.z;
            T.bReverse = bReverseGear;

            // Performance Timer
            double vAbs = Math.Abs(vFwd);
            T.TopSpeed = Math.Max(T.TopSpeed, speed);
            double gTotal = Math.Sqrt(aBody.x * aBody.x + aBody.y * aBody.y);
            if (contacts > 0) T.PeakG = Math.Max(T.PeakG, gTotal);

            if (!T.bRunActive)
            {
                if (vAbs < 0.2 && (In.Throttle > 0.5 || In.Boost > 0.5) && !bReverseGear)
                {
                    T.bRunActive = true;
                    T.RunTime = 0.0;
                    T.RunDist = 0.0;
                    T.T60 = -1.0;
                    T.T100kmh = -1.0;
                    T.TQuarter = -1.0;
                }
            }
            else
            {
                T.RunTime += h;
                T.RunDist += vAbs * h;
                if (T.T60 < 0 && vAbs >= 60.0 * RMath.kMph) T.T60 = T.RunTime;
                if (T.T100kmh < 0 && vAbs >= 100.0 / 3.6) T.T100kmh = T.RunTime;
                if (T.TQuarter < 0 && T.RunDist >= 402.336) T.TQuarter = T.RunTime;
                if (In.Brake > 0.3 || T.RunTime > 60.0) T.bRunActive = false;
            }
        }

        // --- Unity Interop Properties & Methods ---
        public RoadsterTrim Trim
        {
            get { return (RoadsterTrim)CurrentTrim; }
            set
            {
                CurrentTrim = (TrimLevel)value;
                Init(CurrentTrim);
            }
        }

        public V3 Position
        {
            get { return X; }
            set { X = value; }
        }

        public Quat Orientation
        {
            get { return Q; }
            set { Q = value; }
        }

        public V3 Velocity
        {
            get { return V; }
            set { V = value; }
        }

        public V3 AngularVelocity
        {
            get { return W; }
            set { W = value; }
        }

        public double ForwardSpeed { get { return T.ForwardSpeed; } }
        public double MotorPowerKW { get { return T.MotorPowerKW; } }
        public double SteeringAngle { get { return (Wheels != null && Wheels.Length > 0) ? Wheels[0].Steer : 0.0; } }
        public bool IsBraking { get { return Wheels != null && Wheels.Length > 0 && Wheels[0].BrakeTorque > 10.0; } }
        public bool IsThrusterFiring { get { return T.ThrustN > 50.0; } }
        public ColdGasSystem ColdGas { get { return Gas; } }
        public double ColdGasMassFlow { get { return Gas != null ? Gas.MassFlowKgPerSec : 0.0; } }
        public bool IsNozzleActive(int idx) { return Gas != null && Gas.IsNozzleFiring(idx); }

        public bool IsFanSuctionActive
        {
            get { return Set.Fan != FanMode.Off; }
            set { Set.Fan = value ? FanMode.MaxGrip : FanMode.Off; }
        }

        public double FanSuctionForceKN { get { return T.FanDownforceN / 1000.0; } }
        public bool IsCeilingAdherent { get { return T.FanDownforceN > (TotalMass() * RMath.kG * 1.05); } }

        public bool IsHoverActive
        {
            get { return Set.bHoverHold; }
            set { Set.bHoverHold = value; }
        }

        public double TargetHoverHeightM
        {
            get { return Set.HoverAltitudeM; }
            set { Set.HoverAltitudeM = value; }
        }

        public bool IsDRSActive
        {
            get { return Set.Aero == AeroPreset.LowDrag; }
            set { Set.Aero = value ? AeroPreset.LowDrag : AeroPreset.Auto; }
        }

        public bool IsAirbrakeActive
        {
            get { return WingAngle > 0.85; }
            set { WingAngle = value ? 1.0 : 0.0; }
        }

        public double MaxTireSlip
        {
            get
            {
                if (Wheels == null || Wheels.Length < 4) return 0.0;
                return Math.Max(Math.Max(Wheels[0].SlideSpeed, Wheels[1].SlideSpeed), Math.Max(Wheels[2].SlideSpeed, Wheels[3].SlideSpeed));
            }
        }

        public string GearState { get { return bReverseGear ? "R" : (Math.Abs(ForwardSpeed) < 0.1 ? "P" : "D"); } }
        public double LateralG { get { return T.AccelG.y; } }
        public double LongitudinalG { get { return T.AccelG.x; } }
        public double RearBias = 0.70;
        public int TractionControlMode = 0;

        public double RegenStrengthKW
        {
            get { return Set.RegenLevel * 350.0; }
            set { Set.RegenLevel = value / 350.0; }
        }

        public double TargetSuctionForceKN
        {
            get { return Set.FanCustomKPa * FanEffectiveArea; }
            set { Set.FanCustomKPa = value / Math.Max(FanEffectiveArea, 0.1); }
        }

        public double MainBoostThrustKN
        {
            get { return Gas != null ? Gas.TotalThrustN / 1000.0 : 15.0; }
            set { if (Gas != null) Gas.ThrottleScale = value / 15.0; }
        }

        public double ThrottleInput = 0.0;
        public double BrakeInput = 0.0;
        public double SteeringInput = 0.0;
        public bool HandbrakeInput = false;
        public bool BoostInput = false;

        public void ResetOrientation()
        {
            Q = Quat.AxisAngle(new V3(0, 0, 1), 0.0);
            V = new V3(0, 0, 0);
            W = new V3(0, 0, 0);
            X = new V3(X.x, X.y, X.z + 0.5);
        }

        public void Step(float dt)
        {
            RoadsterInputs In = new RoadsterInputs
            {
                Throttle = ThrottleInput,
                Brake = BrakeInput,
                Steer = SteeringInput,
                Handbrake = HandbrakeInput ? 1.0 : 0.0,
                Boost = BoostInput ? 1.0 : 0.0,
                bWantReverse = (BrakeInput > 0.5 && ForwardSpeed < 0.5),
                bDRS = IsDRSActive,
                Lift = IsHoverActive ? 1.0 : 0.0
            };

            WorldTraceFn trace = delegate(V3 start, V3 dir, double len, double radius)
            {
                Vector3 uStart = start.ToUnityVec();
                Vector3 uDir = dir.ToUnityVec();
                RaycastHit hit;
                if (Physics.Raycast(uStart, uDir, out hit, (float)len))
                {
                    return new TraceHit
                    {
                        bHit = true,
                        Dist = hit.distance,
                        Point = V3.FromUnityVec(hit.point),
                        Normal = V3.FromUnityVec(hit.normal)
                    };
                }
                return new TraceHit { bHit = false, Dist = len };
            };

            Step((double)dt, In, trace);
        }
    }
}
