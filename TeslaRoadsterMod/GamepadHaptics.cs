using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace TeslaRoadsterMod
{
    [StructLayout(LayoutKind.Sequential)]
    public struct XINPUT_VIBRATION
    {
        public ushort wLeftMotorSpeed;
        public ushort wRightMotorSpeed;
    }

    public class GamepadHaptics : MonoBehaviour
    {
        public static GamepadHaptics Instance { get; private set; }

        [DllImport("xinput1_4.dll", EntryPoint = "XInputSetState")]
        private static extern int XInputSetState_14(int dwUserIndex, ref XINPUT_VIBRATION pVibration);

        [DllImport("xinput9_1_0.dll", EntryPoint = "XInputSetState")]
        private static extern int XInputSetState_91(int dwUserIndex, ref XINPUT_VIBRATION pVibration);

        private static bool useFallback = false;
        private static bool isAvailable = true;

        public bool EnableHaptics = true;

        void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }
            Instance = this;
        }

        void Update()
        {
            if (!EnableHaptics || !isAvailable) return;

            var actor = RoadsterActor.ActiveRoadster;
            if (actor == null || !actor.IsPlayerInside || actor.Vehicle == null)
            {
                SetVibration(0f, 0f);
                return;
            }

            var veh = actor.Vehicle;
            float leftRumble = 0f;
            float rightBuzz = 0f;

            // 1. Cold Gas Thruster Boost Rumble (Heavy low frequency)
            if (veh.IsThrusterFiring)
            {
                leftRumble += 0.85f;
                rightBuzz += 0.35f;
            }

            // 2. High-speed Acceleration Motor Torque
            if (veh.ThrottleInput > 0.1f && veh.MotorPowerKW > 200.0)
            {
                float tRatio = Mathf.Clamp01((float)veh.MotorPowerKW / 1000f);
                leftRumble += tRatio * 0.45f;
            }

            // 3. ABS & Hard Braking Pulses (High-frequency buzz)
            if (veh.IsBraking)
            {
                float bSpeed = Mathf.Abs((float)veh.ForwardSpeed);
                if (bSpeed > 5f)
                {
                    // Simulated ABS pulse rate at 15 Hz
                    float absPulse = (Mathf.Sin(Time.time * 95f) > 0f) ? 0.75f : 0.25f;
                    rightBuzz += absPulse * 0.8f;
                    leftRumble += 0.25f;
                }
            }

            // 4. Tire Slip / Drift Shudder
            float slip = (float)veh.MaxTireSlip;
            if (slip > 0.15f)
            {
                float slipFrac = Mathf.Clamp01((slip - 0.15f) * 3f);
                rightBuzz += slipFrac * 0.9f;
                leftRumble += slipFrac * 0.3f;
            }

            // 5. Suction Fan High-RPM Micro-Vibration
            if (veh.IsFanSuctionActive && veh.FanRPM > 5000.0)
            {
                float fFrac = Mathf.Clamp01((float)veh.FanRPM / 18000f);
                rightBuzz += fFrac * 0.15f;
            }

            SetVibration(Mathf.Clamp01(leftRumble), Mathf.Clamp01(rightBuzz));
        }

        public void SetVibration(float left, float right)
        {
            if (!isAvailable) return;

            XINPUT_VIBRATION vib;
            vib.wLeftMotorSpeed = (ushort)(left * 65535f);
            vib.wRightMotorSpeed = (ushort)(right * 65535f);

            try
            {
                if (!useFallback)
                {
                    XInputSetState_14(0, ref vib);
                }
                else
                {
                    XInputSetState_91(0, ref vib);
                }
            }
            catch (DllNotFoundException)
            {
                if (!useFallback)
                {
                    useFallback = true;
                    try { XInputSetState_91(0, ref vib); }
                    catch { isAvailable = false; }
                }
                else
                {
                    isAvailable = false;
                }
            }
            catch
            {
                isAvailable = false;
            }
        }

        void OnDisable()
        {
            SetVibration(0f, 0f);
        }

        void OnApplicationQuit()
        {
            SetVibration(0f, 0f);
        }
    }
}
