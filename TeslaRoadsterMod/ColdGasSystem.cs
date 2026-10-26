using System;

namespace TeslaRoadsterMod
{
    // Valve-level Cold Gas System matching ColdGasSystem.h
    public class ColdGasSystem
    {
        public const int RearA = 0;
        public const int RearB = 1;
        public const int RearC = 2;
        public const int RearD = 3;
        public const int LiftFL = 4;
        public const int LiftFR = 5;
        public const int LiftRL = 6;
        public const int LiftRR = 7;
        public const int YawLeftFiring = 8;
        public const int YawRightFiring = 9;
        public const int NumNozzles = 10;

        public struct NozzleState
        {
            public double CommandThrustN;
            public double ThrustN;
            public float ThrustFrac; // 0..1 for visual plumes
        }

        public double TankVolumeL = 600.0;
        public double TankPressureBar = 379.0; // 5,500 PSI full
        public double GasTempK = 293.15;
        public double UsableGasKg = 70.0;
        public double MaxGasKg = 70.0;
        public bool IsArmed = true;

        public NozzleState[] Nozzles = new NozzleState[NumNozzles];

        // Rated maximum thrusts per nozzle at full pressure
        public double RearThrustMaxN = 20000.0; // 4 nozzles = 80 kN total
        public double LiftThrustMaxN = 13000.0; // 4 nozzles = 52 kN total
        public double YawThrustMaxN = 4000.0;   // 2 nozzles = 8 kN

        public ColdGasSystem(double volumeL = 600.0)
        {
            TankVolumeL = volumeL;
            Fill(379.0, 293.15);
        }

        public void Fill(double pressureBar, double tempK)
        {
            TankPressureBar = pressureBar;
            GasTempK = tempK;
            // Real nitrogen density at 379 bar, ~293 K is ~350 kg/m3
            MaxGasKg = (TankVolumeL * 0.001) * 350.0;
            UsableGasKg = MaxGasKg * (pressureBar / 379.0);
        }

        public void TopUp()
        {
            TankPressureBar = 379.0;
            UsableGasKg = MaxGasKg;
        }

        public double DisplayBar => TankPressureBar;
        public double DisplayPSI => TankPressureBar * RMath.kPsiPerBar;
        public double GasFraction => MaxGasKg > 0 ? RMath.Clamp(UsableGasKg / MaxGasKg, 0.0, 1.0) : 0.0;

        public double TankPressurePsi => DisplayPSI;
        public double GasMassRemainingFraction => GasFraction;
        public void RefillTanks() => TopUp();
        public double MassFlowKgPerSec = 0.0;
        public double ThrottleScale = 1.0;
        public bool IsNozzleFiring(int idx) => idx >= 0 && idx < NumNozzles && Nozzles[idx].ThrustFrac > 0.05f;
        public double TotalThrustN
        {
            get
            {
                double sum = 0.0;
                for (int i = 0; i < NumNozzles; i++) sum += Nozzles[i].ThrustN;
                return sum;
            }
        }

        public double MaxThrustN(int nozzleIdx)
        {
            if (nozzleIdx < 4) return RearThrustMaxN * (TankPressureBar / 379.0);
            if (nozzleIdx < 8) return LiftThrustMaxN * (TankPressureBar / 379.0);
            return YawThrustMaxN * (TankPressureBar / 379.0);
        }

        public void CommandThrust(int nozzleIdx, double thrustN)
        {
            if (nozzleIdx >= 0 && nozzleIdx < NumNozzles)
            {
                Nozzles[nozzleIdx].CommandThrustN = thrustN;
            }
        }

        public void Step(double dt, double ambientPressurePa, bool infiniteGas)
        {
            double totalMassFlow = 0.0;
            double pressureFactor = RMath.Clamp(TankPressureBar / 379.0, 0.0, 1.2);

            for (int i = 0; i < NumNozzles; i++)
            {
                double targetThrust = Nozzles[i].CommandThrustN;
                if (!IsArmed || UsableGasKg <= 0.1) targetThrust = 0.0;

                double maxCap = MaxThrustN(i);
                targetThrust = RMath.Clamp(targetThrust, 0.0, maxCap);

                // Valve slew / lag (approx 35 ms response)
                double currentThrust = Nozzles[i].ThrustN;
                double maxDelta = 50000.0 * dt; // fast valve actuation
                Nozzles[i].ThrustN = RMath.MoveTo(currentThrust, targetThrust, maxDelta);

                double rated = (i < 4) ? RearThrustMaxN : ((i < 8) ? LiftThrustMaxN : YawThrustMaxN);
                Nozzles[i].ThrustFrac = (float)RMath.Clamp(Nozzles[i].ThrustN / rated, 0.0, 1.0);

                // Isp ~60s: mass flow = F / (g0 * Isp) ~ F / (9.81 * 60) ~ F / 588.6
                if (Nozzles[i].ThrustN > 0.0)
                {
                    totalMassFlow += Nozzles[i].ThrustN / 600.0;
                }
            }

            if (!infiniteGas && totalMassFlow > 0.0)
            {
                double gasConsumed = totalMassFlow * dt;
                UsableGasKg = Math.Max(0.0, UsableGasKg - gasConsumed);
                TankPressureBar = (UsableGasKg / MaxGasKg) * 379.0;
            }
            else if (infiniteGas)
            {
                TankPressureBar = 379.0;
                UsableGasKg = MaxGasKg;
            }
        }
    }
}
