using System;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public class TeslaTouchscreenUI : MonoBehaviour
    {
        public static TeslaTouchscreenUI Instance { get; private set; }

        public bool IsOpen = false;
        private int currentTab = 0;
        private readonly string[] tabNames = new string[] { "DYNAMICS", "AERO & FANS", "COLD GAS LAB", "PAINT & FINISH", "CONTROLS" };

        private Texture2D panelTex;
        private Texture2D headerTex;
        private Texture2D buttonTex;

        // Custom Paint Colors
        private readonly Color[] presetColors = new Color[]
        {
            new Color(0.96f, 0.96f, 0.97f), // Pearl White
            new Color(0.85f, 0.05f, 0.08f), // Ultra Red
            new Color(0.04f, 0.04f, 0.04f), // Solid Black
            new Color(0.35f, 0.38f, 0.42f), // Midnight Silver
            new Color(0.92f, 0.72f, 0.28f), // SpaceX Titanium Gold
            new Color(0.70f, 0.72f, 0.75f), // Quicksilver Metallic
            new Color(0.05f, 0.75f, 0.95f), // Electric Cyan
            new Color(0.20f, 0.85f, 0.25f)  // Track Lime
        };
        private readonly string[] presetNames = new string[]
        {
            "Pearl White", "Ultra Red", "Solid Black", "Midnight Silver",
            "SpaceX Gold", "Quicksilver", "Electric Cyan", "Track Lime"
        };

        private float metallicVal = 0.85f;
        private float glossVal = 0.92f;
        private Color currentColor = new Color(0.96f, 0.96f, 0.97f);

        void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }
            Instance = this;

            panelTex = new Texture2D(1, 1);
            panelTex.SetPixel(0, 0, new Color(0.05f, 0.06f, 0.08f, 0.94f));
            panelTex.Apply();

            headerTex = new Texture2D(1, 1);
            headerTex.SetPixel(0, 0, new Color(0.08f, 0.10f, 0.13f, 0.98f));
            headerTex.Apply();

            buttonTex = new Texture2D(1, 1);
            buttonTex.SetPixel(0, 0, new Color(0.12f, 0.15f, 0.19f, 0.95f));
            buttonTex.Apply();
        }

        void Update()
        {
            if (Input.GetKeyDown(KeyCode.Tab))
            {
                IsOpen = !IsOpen;
                // Manage cursor lock
                if (IsOpen)
                {
                    Cursor.lockState = CursorLockMode.None;
                    Cursor.visible = true;
                }
                else
                {
                    if (RoadsterActor.ActiveRoadster != null && RoadsterActor.ActiveRoadster.IsPlayerInside)
                    {
                        Cursor.lockState = CursorLockMode.Locked;
                        Cursor.visible = false;
                    }
                }
            }
        }

        void OnGUI()
        {
            if (!IsOpen) return;

            GUI.depth = -10;

            float w = 720f;
            float h = 500f;
            float x = (Screen.width - w) / 2f;
            float y = (Screen.height - h) / 2f;

            // Main Background Panel
            DrawRect(new Rect(x, y, w, h), new Color(0.04f, 0.05f, 0.07f, 0.95f));
            DrawBorder(new Rect(x, y, w, h), new Color(0.20f, 0.35f, 0.50f, 0.7f), 2);

            // Screen Header Bar
            DrawRect(new Rect(x, y, w, 52f), new Color(0.08f, 0.10f, 0.14f, 0.98f));
            DrawBorder(new Rect(x, y + 52f, w, 1f), new Color(0.25f, 0.30f, 0.35f, 0.6f), 1);

            // Title
            GUIStyle headerStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 18,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            headerStyle.normal.textColor = Color.white;
            GUI.Label(new Rect(x + 20f, y + 10f, 250f, 32f), "TESLA ROADSTER 2.0", headerStyle);

            // Close button
            if (GUI.Button(new Rect(x + w - 44f, y + 10f, 32f, 32f), "✕"))
            {
                IsOpen = false;
            }

            // Tabs Bar
            float tabW = w / tabNames.Length;
            float tabY = y + 54f;
            for (int i = 0; i < tabNames.Length; i++)
            {
                bool active = (currentTab == i);
                Rect tabRect = new Rect(x + i * tabW, tabY, tabW, 36f);
                Color tabCol = active ? new Color(0.10f, 0.15f, 0.22f) : new Color(0.05f, 0.06f, 0.08f);
                DrawRect(tabRect, tabCol);
                if (active)
                {
                    DrawRect(new Rect(tabRect.x, tabRect.y + 34f, tabW, 2f), new Color(0.1f, 0.85f, 1.0f));
                }

                GUIStyle tStyle = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 12,
                    fontStyle = active ? FontStyle.Bold : FontStyle.Normal,
                    alignment = TextAnchor.MiddleCenter
                };
                tStyle.normal.textColor = active ? new Color(0.1f, 0.85f, 1.0f) : new Color(0.6f, 0.65f, 0.7f);
                if (GUI.Button(tabRect, tabNames[i], GUIStyle.none))
                {
                    currentTab = i;
                }
                GUI.Label(tabRect, tabNames[i], tStyle);
            }

            // Content Area
            Rect contentRect = new Rect(x + 24f, y + 105f, w - 48f, h - 120f);
            GUILayout.BeginArea(contentRect);

            var actor = RoadsterActor.ActiveRoadster;
            var veh = (actor != null) ? actor.Vehicle : null;

            switch (currentTab)
            {
                case 0: DrawDynamicsTab(veh); break;
                case 1: DrawAeroTab(veh); break;
                case 2: DrawGasLabTab(veh); break;
                case 3: DrawPaintTab(); break;
                case 4: DrawControlsTab(); break;
            }

            GUILayout.EndArea();
        }

        private void DrawDynamicsTab(RoadsterVehicle veh)
        {
            if (veh == null) { GUILayout.Label("No active Roadster"); return; }

            GUILayout.Label("DRIVE MODES & TRIM", GetSectionStyle());
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("PERFORMANCE (Street)", GUILayout.Height(36))) veh.Trim = RoadsterTrim.Performance;
            if (GUILayout.Button("SPACEX THRUSTER (Hypercar)", GUILayout.Height(36))) veh.Trim = RoadsterTrim.Thrust;
            if (GUILayout.Button("ROCKET TRACK (Max Suction + Boost)", GUILayout.Height(36))) veh.Trim = RoadsterTrim.Rocket;
            GUILayout.EndHorizontal();

            GUILayout.Space(16);
            GUILayout.Label(string.Format("Active Trim: {0}", veh.Trim), GetSubStyle());

            GUILayout.Space(20);
            GUILayout.Label("TORQUE VECTORING (FRONT / REAR BIAS)", GetSectionStyle());
            GUILayout.BeginHorizontal();
            GUILayout.Label(string.Format("Front: {0:F0}%", (1f - (float)veh.RearBias) * 100f), GUILayout.Width(100));
            veh.RearBias = GUILayout.HorizontalSlider((float)veh.RearBias, 0.1f, 0.9f);
            GUILayout.Label(string.Format("Rear: {0:F0}%", (float)veh.RearBias * 100f), GUILayout.Width(100));
            GUILayout.EndHorizontal();

            GUILayout.Space(20);
            GUILayout.Label("TRACTION CONTROL SYSTEM", GetSectionStyle());
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("STANDARD TC", GUILayout.Height(32))) veh.TractionControlMode = 0;
            if (GUILayout.Button("TRACK MODE", GUILayout.Height(32))) veh.TractionControlMode = 1;
            if (GUILayout.Button("DRIFT MODE (Oversteer Assist)", GUILayout.Height(32))) veh.TractionControlMode = 2;
            if (GUILayout.Button("TC OFF", GUILayout.Height(32))) veh.TractionControlMode = 3;
            GUILayout.EndHorizontal();

            GUILayout.Space(20);
            GUILayout.Label("REGENERATIVE BRAKING STRENGTH", GetSectionStyle());
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("LOW (120 kW)", GUILayout.Height(30))) veh.RegenStrengthKW = 120.0;
            if (GUILayout.Button("STANDARD (240 kW)", GUILayout.Height(30))) veh.RegenStrengthKW = 240.0;
            if (GUILayout.Button("MAX ONE-PEDAL (350 kW)", GUILayout.Height(30))) veh.RegenStrengthKW = 350.0;
            GUILayout.EndHorizontal();
        }

        private void DrawAeroTab(RoadsterVehicle veh)
        {
            if (veh == null) { GUILayout.Label("No active Roadster"); return; }

            GUILayout.Label("4-FAN GROUND EFFECT SUCTION SYSTEM", GetSectionStyle());
            GUILayout.BeginHorizontal();
            string fanBtnText = veh.IsFanSuctionActive ? "DISABLE FAN SUCTION [V]" : "ACTIVATE FAN SUCTION [V]";
            if (GUILayout.Button(fanBtnText, GUILayout.Height(38)))
            {
                veh.IsFanSuctionActive = !veh.IsFanSuctionActive;
            }
            GUILayout.EndHorizontal();

            GUILayout.Space(12);
            GUILayout.Label(string.Format("Target Suction Downforce: {0:F1} kN", veh.TargetSuctionForceKN), GetSubStyle());
            veh.TargetSuctionForceKN = GUILayout.HorizontalSlider((float)veh.TargetSuctionForceKN, 5f, 40f);

            GUILayout.Space(20);
            GUILayout.Label("ACTIVE REAR WING (DRS & AIRBRAKE)", GetSectionStyle());
            GUILayout.BeginHorizontal();
            if (GUILayout.Button(veh.IsDRSActive ? "DRS ACTIVE [F]" : "ENGAGE DRS [F]", GUILayout.Height(32)))
            {
                veh.IsDRSActive = !veh.IsDRSActive;
            }
            if (GUILayout.Button("CYCLE AIRBRAKE TEST", GUILayout.Height(32)))
            {
                veh.IsAirbrakeActive = !veh.IsAirbrakeActive;
            }
            GUILayout.EndHorizontal();

            GUILayout.Space(20);
            GUILayout.Label("CEILING / INVERTED SURFACE ADHERENCE", GetSectionStyle());
            GUILayout.Label(string.Format("Adherence Status: {0}", veh.IsCeilingAdherent ? "LOCKED TO CEILING / WALL" : "Normal Gravity"), GetSubStyle());
            GUILayout.Label("With 4 suction fans spinning at 18,000 RPM, the underbody generates up to 35 kN of vacuum downforce, exceeding the car's weight (19.6 kN) to hold the Roadster firmly to ceilings and 90-degree tunnel walls.", GetSubStyle());
        }

        private void DrawGasLabTab(RoadsterVehicle veh)
        {
            if (veh == null) { GUILayout.Label("No active Roadster"); return; }

            GUILayout.Label("SPACEX COLD GAS NITROGEN SYSTEM", GetSectionStyle());
            GUILayout.Label(string.Format("Pressure: {0:F0} PSI / {1:F0} Bar  ({2:F1}% remaining)", veh.ColdGas.TankPressurePsi, veh.ColdGas.TankPressureBar, veh.ColdGas.GasMassRemainingFraction * 100f), GetSubStyle());

            GUILayout.Space(8);
            if (GUILayout.Button("REFILL NITROGEN COPV TANKS (Instant 5,500 PSI)", GUILayout.Height(36)))
            {
                veh.ColdGas.RefillTanks();
            }

            GUILayout.Space(20);
            GUILayout.Label("HOVER SYSTEM [H]", GetSectionStyle());
            GUILayout.BeginHorizontal();
            string hoverText = veh.IsHoverActive ? "DEACTIVATE HOVER [H]" : "ACTIVATE HOVER [H]";
            if (GUILayout.Button(hoverText, GUILayout.Height(34)))
            {
                veh.IsHoverActive = !veh.IsHoverActive;
            }
            GUILayout.EndHorizontal();

            GUILayout.Space(8);
            GUILayout.Label(string.Format("Target Hover Height: {0:F2} m", veh.TargetHoverHeightM), GetSubStyle());
            veh.TargetHoverHeightM = GUILayout.HorizontalSlider((float)veh.TargetHoverHeightM, 0.3f, 3.5f);

            GUILayout.Space(20);
            GUILayout.Label("THRUSTER FORCE AUTHORITY", GetSectionStyle());
            GUILayout.Label(string.Format("Main Booster Peak Thrust: {0:F1} kN", veh.MainBoostThrustKN), GetSubStyle());
            veh.MainBoostThrustKN = GUILayout.HorizontalSlider((float)veh.MainBoostThrustKN, 5f, 25f);
        }

        private void DrawPaintTab()
        {
            GUILayout.Label("FACTORY EXTERIOR COLOR PALETTE", GetSectionStyle());
            GUILayout.BeginHorizontal();
            for (int i = 0; i < 4; i++)
            {
                if (GUILayout.Button(presetNames[i], GUILayout.Height(36)))
                {
                    currentColor = presetColors[i];
                    MeshLoader.SetCarColor(currentColor, metallicVal, glossVal);
                }
            }
            GUILayout.EndHorizontal();

            GUILayout.BeginHorizontal();
            for (int i = 4; i < 8; i++)
            {
                if (GUILayout.Button(presetNames[i], GUILayout.Height(36)))
                {
                    currentColor = presetColors[i];
                    MeshLoader.SetCarColor(currentColor, metallicVal, glossVal);
                }
            }
            GUILayout.EndHorizontal();

            GUILayout.Space(20);
            GUILayout.Label(string.Format("METALLIC FLAKE: {0:F2}", metallicVal), GetSectionStyle());
            float newMet = GUILayout.HorizontalSlider(metallicVal, 0.0f, 1.0f);
            if (Mathf.Abs(newMet - metallicVal) > 0.01f)
            {
                metallicVal = newMet;
                MeshLoader.SetCarColor(currentColor, metallicVal, glossVal);
            }

            GUILayout.Space(14);
            GUILayout.Label(string.Format("CLEAR COAT GLOSS: {0:F2}", glossVal), GetSectionStyle());
            float newGloss = GUILayout.HorizontalSlider(glossVal, 0.1f, 1.0f);
            if (Mathf.Abs(newGloss - glossVal) > 0.01f)
            {
                glossVal = newGloss;
                MeshLoader.SetCarColor(currentColor, metallicVal, glossVal);
            }
        }

        private void DrawControlsTab()
        {
            GUILayout.Label("CONTROLS & KEYBOARD / GAMEPAD SHORTCUTS", GetSectionStyle());
            string legend =
                "• W / Up Arrow / Gamepad RT   : Throttle / Accelerate\n" +
                "• S / Down Arrow / Gamepad LT : Brake / Reverse\n" +
                "• A / D / Left Stick          : Steer Left / Right\n" +
                "• Space                       : Handbrake (ground) / Vertical Thruster Jump (hold)\n" +
                "• Left Shift                  : Cold-Gas Rocket Main Booster\n" +
                "• H                           : Toggle SpaceX Hover Mode\n" +
                "• V                           : Toggle 4-Fan Ground Suction System\n" +
                "• F                           : Toggle Active DRS (Drag Reduction System)\n" +
                "• T                           : Cycle Trims (Performance / Thrust / Rocket)\n" +
                "• C                           : Cycle Camera (Chase / Cockpit / Hood / Wheel / Orbit)\n" +
                "• RMB + Mouse Drag            : Free Orbit Look Around Vehicle\n" +
                "• E                           : Enter / Exit Car\n" +
                "• Tab                         : Toggle This Touchscreen\n" +
                "• M                           : Vehicle Spawner & Teleport Menu\n" +
                "• U                           : Toggle Speedometer Units (MPH / KMH)\n" +
                "• R                           : Reset Vehicle Orientation / Upright";

            GUIStyle legStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 12,
                alignment = TextAnchor.UpperLeft
            };
            legStyle.normal.textColor = Color.white;
            GUILayout.Label(legend, legStyle);
        }

        private GUIStyle GetSectionStyle()
        {
            GUIStyle s = new GUIStyle(GUI.skin.label)
            {
                fontSize = 13,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            s.normal.textColor = new Color(0.1f, 0.85f, 1.0f);
            return s;
        }

        private GUIStyle GetSubStyle()
        {
            GUIStyle s = new GUIStyle(GUI.skin.label)
            {
                fontSize = 11,
                alignment = TextAnchor.MiddleLeft
            };
            s.normal.textColor = new Color(0.85f, 0.90f, 0.95f);
            return s;
        }

        private void DrawRect(Rect r, Color c)
        {
            Color old = GUI.color;
            GUI.color = c;
            GUI.DrawTexture(r, panelTex);
            GUI.color = old;
        }

        private void DrawBorder(Rect r, Color c, int thickness)
        {
            DrawRect(new Rect(r.x, r.y, r.width, thickness), c);
            DrawRect(new Rect(r.x, r.y + r.height - thickness, r.width, thickness), c);
            DrawRect(new Rect(r.x, r.y, thickness, r.height), c);
            DrawRect(new Rect(r.x + r.width - thickness, r.y, thickness, r.height), c);
        }
    }
}
