using System;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public class ModernHUD : MonoBehaviour
    {
        public static ModernHUD Instance { get; private set; }

        public bool UseMPH = true;
        public bool ShowTelemetryCluster = true;
        public bool ShowGMeter = true;
        public bool ShowMinimap = true;

        // Visual textures
        private Texture2D panelTex;
        private Texture2D dotTex;
        private Texture2D barTex;

        // G-Meter tracking
        private Vector2 currentG = Vector2.zero;
        private Vector2 peakG = Vector2.zero;
        private float peakGTimer = 0f;

        // Player sprint tracking
        public float PlayerStamina = 1.0f;
        public bool IsPlayerNearRoadster = false;

        void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }
            Instance = this;

            CreateTextures();
        }

        private void CreateTextures()
        {
            // Frosted glass background
            panelTex = new Texture2D(1, 1);
            panelTex.SetPixel(0, 0, new Color(0.04f, 0.05f, 0.07f, 0.82f));
            panelTex.Apply();

            // Accent dot texture
            dotTex = new Texture2D(1, 1);
            dotTex.SetPixel(0, 0, Color.white);
            dotTex.Apply();

            // Bar texture
            barTex = new Texture2D(1, 1);
            barTex.SetPixel(0, 0, Color.white);
            barTex.Apply();
        }

        void Update()
        {
            // Toggle MPH/KMH with 'U'
            if (Input.GetKeyDown(KeyCode.U))
            {
                UseMPH = !UseMPH;
            }

            // Toggle Telemetry Cluster with 'Y'
            if (Input.GetKeyDown(KeyCode.Y))
            {
                ShowTelemetryCluster = !ShowTelemetryCluster;
            }

            // Toggle Minimap with 'N'
            if (Input.GetKeyDown(KeyCode.N))
            {
                ShowMinimap = !ShowMinimap;
            }

            // Check distance to active roadster for interaction prompt
            if (RoadsterActor.ActiveRoadster != null && Camera.main != null)
            {
                float dist = Vector3.Distance(Camera.main.transform.position, RoadsterActor.ActiveRoadster.transform.position);
                IsPlayerNearRoadster = (dist < 4.5f);
            }
            else
            {
                IsPlayerNearRoadster = false;
            }

            // Smooth G-Meter calculation
            if (RoadsterActor.ActiveRoadster != null && RoadsterActor.ActiveRoadster.Vehicle != null)
            {
                var veh = RoadsterActor.ActiveRoadster.Vehicle;
                Vector2 targetG = new Vector2((float)veh.LateralG, (float)veh.LongitudinalG);
                currentG = Vector2.Lerp(currentG, targetG, Time.deltaTime * 12f);

                if (currentG.magnitude > peakG.magnitude)
                {
                    peakG = currentG;
                    peakGTimer = 2.5f;
                }
                else
                {
                    peakGTimer -= Time.deltaTime;
                    if (peakGTimer <= 0f)
                    {
                        peakG = Vector2.Lerp(peakG, currentG, Time.deltaTime * 2f);
                    }
                }
            }
        }

        void OnGUI()
        {
            // Suppress HUD when Touchscreen UI or Vehicle Menu is open
            if (TeslaTouchscreenUI.Instance != null && TeslaTouchscreenUI.Instance.IsOpen) return;
            if (VehicleMenu.Instance != null && VehicleMenu.Instance.IsOpen) return;

            GUI.depth = 0;

            bool inCar = RoadsterActor.ActiveRoadster != null && RoadsterActor.ActiveRoadster.IsPlayerInside;

            if (inCar)
            {
                DrawCarDashboard();
                if (ShowTelemetryCluster)
                {
                    DrawTelemetryCluster();
                }
                if (ShowGMeter)
                {
                    DrawGMeter();
                }
                if (ShowMinimap)
                {
                    DrawMinimap();
                }
            }
            else
            {
                DrawOnFootHUD();
            }
        }

        private void DrawCarDashboard()
        {
            var veh = RoadsterActor.ActiveRoadster.Vehicle;
            float speedMPS = Mathf.Abs((float)veh.ForwardSpeed);
            float speedVal = UseMPH ? (speedMPS * 2.23694f) : (speedMPS * 3.6f);
            string unitStr = UseMPH ? "MPH" : "KM/H";

            // Bottom-Center Digital Instrument Cluster
            float hudW = 460f;
            float hudH = 130f;
            float hudX = (Screen.width - hudW) / 2f;
            float hudY = Screen.height - hudH - 24f;

            // Frosted Glass Panel
            DrawRect(new Rect(hudX, hudY, hudW, hudH), new Color(0.05f, 0.06f, 0.08f, 0.88f));
            DrawBorder(new Rect(hudX, hudY, hudW, hudH), new Color(0.25f, 0.35f, 0.45f, 0.5f), 1);

            // Large Digital Speed
            GUIStyle speedStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 58,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleCenter
            };
            speedStyle.normal.textColor = Color.white;
            GUI.Label(new Rect(hudX, hudY + 8f, hudW, 64f), Mathf.RoundToInt(speedVal).ToString(), speedStyle);

            // Unit Label
            GUIStyle unitStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 14,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleCenter
            };
            unitStyle.normal.textColor = new Color(0.65f, 0.75f, 0.85f, 0.9f);
            GUI.Label(new Rect(hudX, hudY + 68f, hudW, 18f), unitStr + "  •  [U] TOGGLE", unitStyle);

            // Gear Indicator (P  R  N  D)
            string gear = veh.GearState.ToString();
            float gearX = hudX + (hudW - 180f) / 2f;
            float gearY = hudY + 90f;
            string[] gears = new string[] { "P", "R", "N", "D" };
            for (int i = 0; i < gears.Length; i++)
            {
                bool active = (gears[i] == gear);
                GUIStyle gStyle = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 17,
                    fontStyle = active ? FontStyle.Bold : FontStyle.Normal,
                    alignment = TextAnchor.MiddleCenter
                };
                gStyle.normal.textColor = active ? new Color(0.1f, 0.85f, 1.0f) : new Color(0.45f, 0.50f, 0.55f);
                GUI.Label(new Rect(gearX + i * 45f, gearY, 40f, 24f), gears[i], gStyle);
            }

            // Power / Regen Dynamic Bar (Below speed)
            float barW = hudW - 40f;
            float barH = 6f;
            float barX = hudX + 20f;
            float barY = hudY + hudH - 12f;

            // Background line
            DrawRect(new Rect(barX, barY, barW, barH), new Color(0.15f, 0.18f, 0.22f, 0.8f));

            float powerDraw = (float)veh.MotorPowerKW; // -350 kW regen to +1200 kW power
            float centerBarX = barX + barW * 0.35f;

            if (powerDraw >= 0)
            {
                // Discharge Power (Orange to Red glow)
                float powerRatio = Mathf.Clamp01(powerDraw / 1200f);
                float pw = (barX + barW - centerBarX) * powerRatio;
                Color pCol = Color.Lerp(new Color(1f, 0.6f, 0.1f), new Color(1f, 0.15f, 0.1f), powerRatio);
                DrawRect(new Rect(centerBarX, barY, pw, barH), pCol);
            }
            else
            {
                // Regenerative Braking (Bright Green to Cyan glow)
                float regenRatio = Mathf.Clamp01(-powerDraw / 350f);
                float rw = (centerBarX - barX) * regenRatio;
                Color rCol = Color.Lerp(new Color(0.1f, 0.9f, 0.4f), new Color(0.1f, 0.9f, 1.0f), regenRatio);
                DrawRect(new Rect(centerBarX - rw, barY, rw, barH), rCol);
            }
        }

        private void DrawTelemetryCluster()
        {
            var veh = RoadsterActor.ActiveRoadster.Vehicle;

            float w = 290f;
            float h = 260f;
            float x = Screen.width - w - 24f;
            float y = Screen.height - h - 24f;

            DrawRect(new Rect(x, y, w, h), new Color(0.04f, 0.05f, 0.07f, 0.85f));
            DrawBorder(new Rect(x, y, w, h), new Color(0.20f, 0.30f, 0.40f, 0.5f), 1);

            GUIStyle titleStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 14,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            titleStyle.normal.textColor = new Color(0.1f, 0.85f, 1.0f);
            GUI.Label(new Rect(x + 14f, y + 10f, w - 28f, 20f), "SPACEX TELEMETRY", titleStyle);

            GUIStyle textStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 12,
                alignment = TextAnchor.MiddleLeft
            };
            textStyle.normal.textColor = Color.white;

            // Trim
            GUI.Label(new Rect(x + 14f, y + 36f, w - 28f, 18f), "Trim: " + veh.Trim.ToString(), textStyle);

            // Cold-Gas Tank Pressure
            float psi = (float)veh.ColdGas.TankPressurePsi;
            float bar = (float)veh.ColdGas.TankPressureBar;
            float gasPct = (float)veh.ColdGas.GasMassRemainingFraction * 100f;
            GUI.Label(new Rect(x + 14f, y + 58f, w - 28f, 18f), string.Format("Nitrogen: {0:F0} PSI ({1:F0} Bar)", psi, bar), textStyle);

            // Pressure Bar
            DrawRect(new Rect(x + 14f, y + 78f, w - 28f, 8f), new Color(0.12f, 0.15f, 0.18f));
            Color tankCol = gasPct > 30f ? new Color(0.1f, 0.85f, 1.0f) : (gasPct > 12f ? Color.yellow : Color.red);
            DrawRect(new Rect(x + 14f, y + 78f, (w - 28f) * (gasPct / 100f), 8f), tankCol);

            // 4-Fan Ground Suction System
            float fanRpm = (float)veh.FanRPM;
            float suctionKn = (float)veh.FanSuctionForceKN;
            GUI.Label(new Rect(x + 14f, y + 96f, w - 28f, 18f), string.Format("Fan Suction: {0:F1} kN ({1:F0} RPM)", suctionKn, fanRpm), textStyle);

            // Fan Bar
            DrawRect(new Rect(x + 14f, y + 116f, w - 28f, 8f), new Color(0.12f, 0.15f, 0.18f));
            Color fanCol = veh.IsCeilingAdherent ? new Color(0.9f, 0.2f, 1.0f) : new Color(0.15f, 0.85f, 0.45f);
            DrawRect(new Rect(x + 14f, y + 116f, (w - 28f) * (fanRpm / 18000f), 8f), fanCol);

            // Status Badges
            float badgeY = y + 138f;
            DrawBadge(new Rect(x + 14f, badgeY, 78f, 22f), "FANS [V]", veh.IsFanSuctionActive ? Color.green : Color.gray);
            DrawBadge(new Rect(x + 98f, badgeY, 80f, 22f), "HOVER [H]", veh.IsHoverActive ? Color.cyan : Color.gray);
            DrawBadge(new Rect(x + 184f, badgeY, 78f, 22f), "DRS [F]", veh.IsDRSActive ? Color.yellow : Color.gray);

            // Thruster Boost Status
            float badgeY2 = y + 168f;
            DrawBadge(new Rect(x + 14f, badgeY2, 120f, 22f), "BOOST [LSHIFT]", veh.IsThrusterFiring ? Color.magenta : Color.gray);
            DrawBadge(new Rect(x + 140f, badgeY2, 122f, 22f), "JUMP [SPACE]", Color.white);

            // Ceiling / Wall adherence alert
            if (veh.IsCeilingAdherent)
            {
                GUIStyle alertStyle = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 12,
                    fontStyle = FontStyle.Bold,
                    alignment = TextAnchor.MiddleCenter
                };
                alertStyle.normal.textColor = new Color(0.9f, 0.2f, 1.0f);
                GUI.Label(new Rect(x + 14f, y + 200f, w - 28f, 22f), "▲ CEILING LOCK ACTIVE ▲", alertStyle);
            }

            // Quick Hotkeys Help
            GUIStyle helpStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 10,
                alignment = TextAnchor.MiddleCenter
            };
            helpStyle.normal.textColor = new Color(0.6f, 0.65f, 0.7f);
            GUI.Label(new Rect(x + 14f, y + h - 22f, w - 28f, 16f), "[TAB] Center Screen  •  [C] Camera Mode", helpStyle);
        }

        private void DrawGMeter()
        {
            float size = 130f;
            float x = 24f;
            float y = Screen.height - size - 24f;

            DrawRect(new Rect(x, y, size, size), new Color(0.04f, 0.05f, 0.07f, 0.85f));
            DrawBorder(new Rect(x, y, size, size), new Color(0.20f, 0.30f, 0.40f, 0.5f), 1);

            Vector2 center = new Vector2(x + size / 2f, y + size / 2f);

            // Crosshairs
            DrawRect(new Rect(center.x - size / 2f + 8f, center.y, size - 16f, 1f), new Color(0.3f, 0.35f, 0.4f, 0.6f));
            DrawRect(new Rect(center.x, center.y - size / 2f + 8f, 1f, size - 16f), new Color(0.3f, 0.35f, 0.4f, 0.6f));

            // 1.0 G & 2.0 G reference rings
            float scale = (size - 24f) / 4f; // 2.0 G radius

            // Peak G dot
            Vector2 peakPos = center + new Vector2(peakG.x * scale, -peakG.y * scale);
            DrawRect(new Rect(peakPos.x - 2f, peakPos.y - 2f, 4f, 4f), new Color(1.0f, 0.3f, 0.1f, 0.7f));

            // Live G dot
            Vector2 dotPos = center + new Vector2(currentG.x * scale, -currentG.y * scale);
            DrawRect(new Rect(dotPos.x - 3.5f, dotPos.y - 3.5f, 7f, 7f), new Color(0.1f, 0.9f, 1.0f, 0.95f));

            // Peak G text
            GUIStyle gStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 11,
                alignment = TextAnchor.UpperLeft
            };
            gStyle.normal.textColor = Color.white;
            GUI.Label(new Rect(x + 6f, y + 4f, size - 12f, 16f), string.Format("{0:F2}G", currentG.magnitude), gStyle);
        }

        private void DrawOnFootHUD()
        {
            // Minimalist crosshair
            float cx = Screen.width / 2f;
            float cy = Screen.height / 2f;
            DrawRect(new Rect(cx - 5f, cy - 1f, 10f, 2f), new Color(1f, 1f, 1f, 0.75f));
            DrawRect(new Rect(cx - 1f, cy - 5f, 2f, 10f), new Color(1f, 1f, 1f, 0.75f));

            // Interaction Prompt when near Roadster
            if (IsPlayerNearRoadster)
            {
                float promptW = 280f;
                float promptH = 44f;
                float px = (Screen.width - promptW) / 2f;
                float py = cy + 45f;

                DrawRect(new Rect(px, py, promptW, promptH), new Color(0.04f, 0.05f, 0.08f, 0.88f));
                DrawBorder(new Rect(px, py, promptW, promptH), new Color(0.1f, 0.85f, 1.0f, 0.8f), 1);

                GUIStyle promptStyle = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 14,
                    fontStyle = FontStyle.Bold,
                    alignment = TextAnchor.MiddleCenter
                };
                promptStyle.normal.textColor = Color.white;
                GUI.Label(new Rect(px, py, promptW, promptH), "[E] ENTER TESLA ROADSTER", promptStyle);
            }

            // Sprint stamina bar
            float sw = 180f;
            float sh = 6f;
            float sx = (Screen.width - sw) / 2f;
            float sy = Screen.height - 40f;
            DrawRect(new Rect(sx, sy, sw, sh), new Color(0.15f, 0.18f, 0.22f, 0.7f));
            DrawRect(new Rect(sx, sy, sw * PlayerStamina, sh), new Color(0.1f, 0.85f, 1.0f, 0.9f));
        }

        private void DrawBadge(Rect r, string text, Color col)
        {
            DrawRect(r, new Color(col.r * 0.2f, col.g * 0.2f, col.b * 0.2f, 0.7f));
            DrawBorder(r, col, 1);
            GUIStyle st = new GUIStyle(GUI.skin.label)
            {
                fontSize = 11,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleCenter
            };
            st.normal.textColor = col;
            GUI.Label(r, text, st);
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

        private void DrawMinimap()
        {
            var actor = RoadsterActor.ActiveRoadster;
            if (actor == null) return;

            float size = 160f;
            float x = Screen.width - size - 24f;
            float y = 24f;
            Vector2 center = new Vector2(x + size / 2f, y + size / 2f);
            float radius = size / 2f - 4f;

            // Frosted background
            DrawRect(new Rect(x, y, size, size), new Color(0.04f, 0.06f, 0.09f, 0.90f));
            DrawBorder(new Rect(x, y, size, size), new Color(0.15f, 0.40f, 0.65f, 0.7f), 1);

            // Range circles (100m and 250m)
            float r100 = radius * (100f / 250f);
            DrawRect(new Rect(center.x - r100, center.y - 1f, r100 * 2f, 2f), new Color(0.2f, 0.3f, 0.4f, 0.4f));
            DrawRect(new Rect(center.x - 1f, center.y - r100, 2f, r100 * 2f), new Color(0.2f, 0.3f, 0.4f, 0.4f));

            // Crosshairs
            DrawRect(new Rect(center.x - radius, center.y, radius * 2f, 1f), new Color(0.25f, 0.35f, 0.45f, 0.5f));
            DrawRect(new Rect(center.x, center.y - radius, 1f, radius * 2f), new Color(0.25f, 0.35f, 0.45f, 0.5f));

            // Compass points (N, E, S, W) relative to car's yaw
            float yawRad = actor.transform.eulerAngles.y * Mathf.Deg2Rad;
            string[] dirs = new string[] { "N", "E", "S", "W" };
            for (int i = 0; i < 4; i++)
            {
                float angle = -yawRad + i * (Mathf.PI * 0.5f);
                Vector2 pos = center + new Vector2(Mathf.Sin(angle), -Mathf.Cos(angle)) * (radius - 12f);
                GUIStyle cs = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 10,
                    fontStyle = FontStyle.Bold,
                    alignment = TextAnchor.MiddleCenter
                };
                cs.normal.textColor = (i == 0) ? new Color(1.0f, 0.3f, 0.2f) : new Color(0.6f, 0.7f, 0.8f);
                GUI.Label(new Rect(pos.x - 10f, pos.y - 10f, 20f, 20f), dirs[i], cs);
            }

            // Other Cars Blips
            if (SimControl.current != null)
            {
                GameObject[] cars = HarmonyLib.Traverse.Create(SimControl.current).Field("drivableCarsArray").GetValue<GameObject[]>();
                if (cars != null)
                {
                    foreach (GameObject cGo in cars)
                    {
                        if (cGo == null || !cGo.activeInHierarchy) continue;
                        Vector3 delta = cGo.transform.position - actor.transform.position;
                        Vector3 localDelta = actor.transform.InverseTransformDirection(delta);
                        float dist = delta.magnitude;
                        if (dist < 250f && dist > 1f)
                        {
                            float bx = center.x + (localDelta.x / 250f) * radius;
                            float by = center.y - (localDelta.z / 250f) * radius;
                            DrawRect(new Rect(bx - 3f, by - 3f, 6f, 6f), Color.yellow);
                        }
                    }
                }
            }

            // Center Player / Roadster icon (cyan diamond / arrow)
            DrawRect(new Rect(center.x - 4f, center.y - 4f, 8f, 8f), new Color(0.1f, 0.95f, 1.0f));

            // Heading & Altitude badge below
            int heading = Mathf.RoundToInt(actor.transform.eulerAngles.y);
            float alt = actor.transform.position.y;
            GUIStyle infoStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 10,
                alignment = TextAnchor.MiddleCenter
            };
            infoStyle.normal.textColor = new Color(0.7f, 0.8f, 0.9f);
            GUI.Label(new Rect(x, y + size + 2f, size, 16f), string.Format("HDG: {0:D3}°  •  ALT: {1:F0}m", heading, alt), infoStyle);
        }
    }
}
