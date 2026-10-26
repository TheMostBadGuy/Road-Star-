using System;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public class InputSystem : MonoBehaviour
    {
        public static InputSystem Instance { get; private set; }

        public float Throttle { get; private set; }
        public float Brake { get; private set; }
        public float Steering { get; private set; }
        public bool Handbrake { get; private set; }
        public bool Boost { get; private set; }
        public bool HoverToggle { get; private set; }
        public bool FanToggle { get; private set; }
        public bool DRSToggle { get; private set; }
        public bool TrimCycle { get; private set; }
        public bool CameraCycle { get; private set; }
        public bool EnterExit { get; private set; }
        public bool ResetVehicle { get; private set; }

        // On-foot sprint management
        public bool IsSprinting { get; private set; }
        private float stamina = 1.0f;

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
            // Do not process driving inputs while Touchscreen UI or Vehicle Menu is open
            if (TeslaTouchscreenUI.Instance != null && TeslaTouchscreenUI.Instance.IsOpen) return;
            if (VehicleMenu.Instance != null && VehicleMenu.Instance.IsOpen) return;

            // 1. Throttle (W, UpArrow, or Gamepad Right Trigger)
            float kThrottle = 0f;
            if (Input.GetKey(KeyCode.W) || Input.GetKey(KeyCode.UpArrow)) kThrottle = 1f;
            float padRT = Mathf.Clamp01(Input.GetAxis("Submit") > 0 ? 1f : 0f); // Fallback or axis
            Throttle = Mathf.Max(kThrottle, padRT);

            // 2. Brake (S, DownArrow, or Gamepad Left Trigger)
            float kBrake = 0f;
            if (Input.GetKey(KeyCode.S) || Input.GetKey(KeyCode.DownArrow)) kBrake = 1f;
            Brake = kBrake;

            // 3. Steering (A/D, Left/Right Arrows, or Gamepad Left Stick)
            float kSteer = 0f;
            if (Input.GetKey(KeyCode.A) || Input.GetKey(KeyCode.LeftArrow)) kSteer -= 1f;
            if (Input.GetKey(KeyCode.D) || Input.GetKey(KeyCode.RightArrow)) kSteer += 1f;
            float padSteer = Input.GetAxis("Horizontal");
            Steering = Mathf.Abs(kSteer) > 0.01f ? kSteer : padSteer;

            // 4. Handbrake / Vertical Jump (Space or Gamepad Button B / JoystickButton1)
            Handbrake = Input.GetKey(KeyCode.Space) || Input.GetKey(KeyCode.JoystickButton1);

            // 5. Cold-Gas Main Boost (LeftShift or Gamepad Button Y / JoystickButton3)
            Boost = Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.JoystickButton3);

            // 6. Toggles
            HoverToggle = Input.GetKeyDown(KeyCode.H) || Input.GetKeyDown(KeyCode.JoystickButton2);
            FanToggle = Input.GetKeyDown(KeyCode.V);
            DRSToggle = Input.GetKeyDown(KeyCode.F) || Input.GetKeyDown(KeyCode.JoystickButton5);
            TrimCycle = Input.GetKeyDown(KeyCode.T);
            CameraCycle = Input.GetKeyDown(KeyCode.C) || Input.GetKeyDown(KeyCode.JoystickButton4);
            EnterExit = Input.GetKeyDown(KeyCode.E) || Input.GetKeyDown(KeyCode.JoystickButton0);
            ResetVehicle = Input.GetKeyDown(KeyCode.R);

            // 7. On-foot sprint handling
            bool onFoot = (RoadsterActor.ActiveRoadster == null || !RoadsterActor.ActiveRoadster.IsPlayerInside);
            if (onFoot)
            {
                bool wantSprint = Input.GetKey(KeyCode.LeftShift);
                if (wantSprint && stamina > 0.05f)
                {
                    IsSprinting = true;
                    stamina = Mathf.Clamp01(stamina - Time.deltaTime * 0.25f);
                }
                else
                {
                    IsSprinting = false;
                    stamina = Mathf.Clamp01(stamina + Time.deltaTime * 0.35f);
                }
                if (ModernHUD.Instance != null)
                {
                    ModernHUD.Instance.PlayerStamina = stamina;
                }
            }

            // Route inputs to Active Roadster
            if (RoadsterActor.ActiveRoadster != null && RoadsterActor.ActiveRoadster.IsPlayerInside)
            {
                var veh = RoadsterActor.ActiveRoadster.Vehicle;
                veh.ThrottleInput = Throttle;
                veh.BrakeInput = Brake;
                veh.SteeringInput = Steering;
                veh.HandbrakeInput = Handbrake;
                veh.BoostInput = Boost;

                if (HoverToggle) veh.IsHoverActive = !veh.IsHoverActive;
                if (FanToggle) veh.IsFanSuctionActive = !veh.IsFanSuctionActive;
                if (DRSToggle) veh.IsDRSActive = !veh.IsDRSActive;
                if (TrimCycle)
                {
                    int nextTrim = ((int)veh.Trim + 1) % 3;
                    veh.Trim = (RoadsterTrim)nextTrim;
                }
                if (ResetVehicle)
                {
                    veh.ResetOrientation();
                }
            }
        }
    }
}
