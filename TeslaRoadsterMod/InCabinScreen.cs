using System;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public class InCabinScreen : MonoBehaviour
    {
        public static InCabinScreen Instance { get; private set; }

        private RenderTexture screenRT;
        private Texture2D bgTex;
        private Material screenMat;
        private Renderer screenRenderer;

        private float updateTimer = 0f;

        void Awake()
        {
            Instance = this;

            screenRT = new RenderTexture(512, 512, 0, RenderTextureFormat.ARGB32);
            screenRT.name = "Tesla_InCabin_Screen_RT";
            screenRT.filterMode = FilterMode.Bilinear;
            screenRT.Create();

            bgTex = new Texture2D(1, 1);
            bgTex.SetPixel(0, 0, new Color(0.04f, 0.05f, 0.08f, 1f));
            bgTex.Apply();
        }

        public void BindScreenRenderer(Renderer rend)
        {
            screenRenderer = rend;
            if (screenRenderer != null)
            {
                screenMat = screenRenderer.material;
                screenMat.mainTexture = screenRT;
                screenMat.EnableKeyword("_EMISSION");
                screenMat.SetTexture("_EmissionMap", screenRT);
                screenMat.SetColor("_EmissionColor", new Color(1.2f, 1.2f, 1.3f));
            }
        }

        void OnGUI()
        {
            if (screenRT == null || Event.current.type != EventType.Repaint) return;

            RenderTexture prev = RenderTexture.active;
            RenderTexture.active = screenRT;
            GL.Clear(false, true, new Color(0.04f, 0.05f, 0.08f, 1.0f));

            // Render Tesla Center Screen inside the 512x512 texture
            Rect fullRect = new Rect(0, 0, 512, 512);

            // 1. Top Status Bar
            DrawRect(new Rect(0, 0, 512, 44), new Color(0.08f, 0.10f, 0.14f, 1.0f));
            DrawRect(new Rect(0, 44, 512, 1), new Color(0.2f, 0.3f, 0.4f, 0.8f));

            GUIStyle tLogo = new GUIStyle(GUI.skin.label)
            {
                fontSize = 20,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            tLogo.normal.textColor = new Color(0.9f, 0.1f, 0.1f);
            GUI.Label(new Rect(18, 8, 120, 30), "TESLA", tLogo);

            GUIStyle subTitle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 11,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            subTitle.normal.textColor = Color.white;
            GUI.Label(new Rect(95, 12, 180, 24), "ROADSTER 2.0", subTitle);

            // SOC / Battery & Tank info on top right
            var actor = RoadsterActor.ActiveRoadster;
            var veh = (actor != null) ? actor.Vehicle : null;
            string batStr = (veh != null) ? string.Format("{0:F0}%  •  {1:F0} PSI", veh.EnergyKWh / veh.BatteryKWh * 100f, veh.ColdGas.TankPressurePsi) : "100%";

            GUIStyle batStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 12,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleRight
            };
            batStyle.normal.textColor = new Color(0.1f, 0.85f, 1.0f);
            GUI.Label(new Rect(250, 10, 244, 26), batStr, batStyle);

            // 2. Large Central Digital Speedometer
            float speedVal = 0f;
            if (veh != null)
            {
                bool useMph = ModernHUD.Instance != null && ModernHUD.Instance.UseMPH;
                speedVal = Mathf.Abs((float)veh.ForwardSpeed) * (useMph ? 2.23694f : 3.6f);
            }

            GUIStyle speedStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 72,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleCenter
            };
            speedStyle.normal.textColor = Color.white;
            GUI.Label(new Rect(0, 75, 512, 80), Mathf.RoundToInt(speedVal).ToString(), speedStyle);

            GUIStyle unitStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 15,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleCenter
            };
            unitStyle.normal.textColor = new Color(0.6f, 0.7f, 0.8f);
            string unit = (ModernHUD.Instance != null && ModernHUD.Instance.UseMPH) ? "MPH" : "KM/H";
            GUI.Label(new Rect(0, 155, 512, 22), unit, unitStyle);

            // Gear Indicator
            string gear = (veh != null) ? veh.GearState : "P";
            string[] gears = new string[] { "P", "R", "N", "D" };
            float gx = (512 - 160) / 2f;
            for (int i = 0; i < 4; i++)
            {
                bool active = gears[i] == gear;
                GUIStyle gs = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 16,
                    fontStyle = active ? FontStyle.Bold : FontStyle.Normal,
                    alignment = TextAnchor.MiddleCenter
                };
                gs.normal.textColor = active ? new Color(0.1f, 0.85f, 1.0f) : new Color(0.4f, 0.45f, 0.5f);
                GUI.Label(new Rect(gx + i * 40f, 182, 35, 24), gears[i], gs);
            }

            // 3. Central Roadster Telemetry Card
            Rect cardRect = new Rect(36, 215, 440, 140);
            DrawRect(cardRect, new Color(0.08f, 0.10f, 0.14f, 0.9f));
            DrawBorder(cardRect, new Color(0.20f, 0.30f, 0.40f, 0.6f), 1);

            GUIStyle cardTitle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 13,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            cardTitle.normal.textColor = new Color(0.1f, 0.85f, 1.0f);
            GUI.Label(new Rect(48, 222, 300, 20), "SPACEX PACKAGE STATUS", cardTitle);

            if (veh != null)
            {
                GUIStyle cardText = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 11,
                    alignment = TextAnchor.MiddleLeft
                };
                cardText.normal.textColor = Color.white;

                GUI.Label(new Rect(48, 246, 400, 18), string.Format("Drive Mode: {0}", veh.Trim), cardText);
                GUI.Label(new Rect(48, 266, 400, 18), string.Format("Fan Vacuum: {0:F1} kN ({1:F0} RPM) - {2}", veh.FanSuctionForceKN, veh.FanRPM, veh.IsFanSuctionActive ? "ACTIVE" : "STANDBY"), cardText);
                GUI.Label(new Rect(48, 286, 400, 18), string.Format("Cold-Gas Pressure: {0:F0} PSI / {1:F0} Bar", veh.ColdGas.TankPressurePsi, veh.ColdGas.TankPressureBar), cardText);
                GUI.Label(new Rect(48, 306, 400, 18), string.Format("Active Aero: DRS {0}  •  Airbrake {1}", veh.IsDRSActive ? "ON" : "OFF", veh.IsAirbrakeActive ? "DEPLOYED" : "STOWED"), cardText);
                GUI.Label(new Rect(48, 326, 400, 18), string.Format("Hover Hold System: {0} ({1:F1} m)", veh.IsHoverActive ? "LOCKED" : "OFF", veh.TargetHoverHeightM), cardText);
            }

            // 4. Energy Flow Bar
            if (veh != null)
            {
                float pDraw = (float)veh.MotorPowerKW;
                float pRatio = Mathf.Clamp01(Mathf.Abs(pDraw) / 1000f);
                Color pColor = pDraw >= 0 ? new Color(1f, 0.5f, 0.1f) : new Color(0.1f, 0.9f, 0.4f);

                DrawRect(new Rect(36, 368, 440, 8), new Color(0.12f, 0.15f, 0.20f));
                if (pDraw >= 0)
                {
                    DrawRect(new Rect(256, 368, 220 * pRatio, 8), pColor);
                }
                else
                {
                    DrawRect(new Rect(256 - 220 * pRatio, 368, 220 * pRatio, 8), pColor);
                }
            }

            // 5. Bottom Navigation / Media Bar
            DrawRect(new Rect(0, 460, 512, 52), new Color(0.06f, 0.08f, 0.11f));
            DrawRect(new Rect(0, 460, 512, 1), new Color(0.2f, 0.25f, 0.3f));

            GUIStyle bottomStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 11,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleCenter
            };
            bottomStyle.normal.textColor = new Color(0.7f, 0.75f, 0.82f);
            GUI.Label(new Rect(0, 472, 512, 28), "♫ SPACE ODDITY  •  CLIMATE 68°F  •  NAV: PROVING GROUNDS", bottomStyle);

            RenderTexture.active = prev;
        }

        private void DrawRect(Rect r, Color c)
        {
            Color old = GUI.color;
            GUI.color = c;
            GUI.DrawTexture(r, bgTex);
            GUI.color = old;
        }

        private void DrawBorder(Rect r, Color c, int thickness)
        {
            DrawRect(new Rect(r.x, r.y, r.width, thickness), c);
            DrawRect(new Rect(r.x, r.y + r.height - thickness, r.width, thickness), c);
            DrawRect(new Rect(r.x, r.y, thickness, r.height), c);
            DrawRect(new Rect(r.x + r.width - thickness, r.y, thickness, r.height), c);
        }

        void OnDestroy()
        {
            if (screenRT != null)
            {
                screenRT.Release();
                Destroy(screenRT);
            }
            if (bgTex != null)
            {
                Destroy(bgTex);
            }
        }
    }
}
