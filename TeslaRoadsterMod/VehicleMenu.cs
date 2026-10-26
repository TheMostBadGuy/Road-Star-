using System;
using UnityEngine;
using HarmonyLib;

namespace TeslaRoadsterMod
{
    public class VehicleMenu : MonoBehaviour
    {
        public static VehicleMenu Instance { get; private set; }

        public bool IsOpen = false;

        private Texture2D panelTex;
        private Texture2D btnTex;

        void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }
            Instance = this;

            panelTex = new Texture2D(1, 1);
            panelTex.SetPixel(0, 0, new Color(0.04f, 0.05f, 0.08f, 0.95f));
            panelTex.Apply();

            btnTex = new Texture2D(1, 1);
            btnTex.SetPixel(0, 0, new Color(0.12f, 0.16f, 0.22f, 0.9f));
            btnTex.Apply();
        }

        void Update()
        {
            if (Input.GetKeyDown(KeyCode.M))
            {
                IsOpen = !IsOpen;
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

            GUI.depth = -20;

            float w = 620f;
            float h = 440f;
            float x = (Screen.width - w) / 2f;
            float y = (Screen.height - h) / 2f;

            DrawRect(new Rect(x, y, w, h), new Color(0.04f, 0.05f, 0.07f, 0.96f));
            DrawBorder(new Rect(x, y, w, h), new Color(0.15f, 0.45f, 0.70f, 0.8f), 2);

            // Title
            GUIStyle titleStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 17,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            titleStyle.normal.textColor = Color.white;
            GUI.Label(new Rect(x + 24f, y + 14f, 300f, 28f), "VEHICLE SELECT & TELEPORT", titleStyle);

            if (GUI.Button(new Rect(x + w - 44f, y + 12f, 32f, 32f), "✕"))
            {
                IsOpen = false;
            }

            Rect contentRect = new Rect(x + 24f, y + 54f, w - 48f, h - 70f);
            GUILayout.BeginArea(contentRect);

            GUILayout.Label("SPAWN VEHICLE", GetSectionStyle());

            if (GUILayout.Button("★ TESLA ROCKET ROADSTER (SpaceX Package) ★", GUILayout.Height(38)))
            {
                SpawnOrSwitchToRoadster(RoadsterTrim.Rocket);
                IsOpen = false;
            }

            if (GUILayout.Button("TESLA ROADSTER 2.0 (Performance Trim)", GUILayout.Height(34)))
            {
                SpawnOrSwitchToRoadster(RoadsterTrim.Performance);
                IsOpen = false;
            }

            GUILayout.Space(10);
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("TESLA MODEL S", GUILayout.Height(32))) SwitchToOriginalCar(VehicleType.ModelS);
            if (GUILayout.Button("TESLA MODEL 3", GUILayout.Height(32))) SwitchToOriginalCar(VehicleType.Model3);
            if (GUILayout.Button("TESLA MODEL X", GUILayout.Height(32))) SwitchToOriginalCar(VehicleType.ModelX);
            if (GUILayout.Button("TESLA SEMI", GUILayout.Height(32))) SwitchToOriginalCar(VehicleType.Semi);
            GUILayout.EndHorizontal();

            GUILayout.Space(18);
            GUILayout.Label("TELEPORT LOCATIONS", GetSectionStyle());

            GUILayout.BeginHorizontal();
            if (GUILayout.Button("DRAG STRIP", GUILayout.Height(32))) TeleportActiveVehicle(new Vector3(0f, 1.5f, 0f));
            if (GUILayout.Button("HIGH SPEED RING", GUILayout.Height(32))) TeleportActiveVehicle(new Vector3(250f, 1.5f, 150f));
            if (GUILayout.Button("MOUNTAIN PASS", GUILayout.Height(32))) TeleportActiveVehicle(new Vector3(-180f, 15f, 280f));
            GUILayout.EndHorizontal();

            GUILayout.Space(14);
            GUILayout.Label("VEHICLE RECOVERY", GetSectionStyle());
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("RESET ORIENTATION (Upright)", GUILayout.Height(32)))
            {
                if (RoadsterActor.ActiveRoadster != null)
                {
                    RoadsterActor.ActiveRoadster.Vehicle.ResetOrientation();
                }
            }
            if (GUILayout.Button("TELEPORT ROADSTER TO ME", GUILayout.Height(32)))
            {
                TeleportRoadsterToPlayer();
            }
            GUILayout.EndHorizontal();

            GUILayout.EndArea();
        }

        private void SpawnOrSwitchToRoadster(RoadsterTrim trim)
        {
            if (RoadsterActor.ActiveRoadster == null)
            {
                Vector3 spawnPos = Vector3.zero;
                Transform activeTrans = SimControl.current != null ? SimControl.current.GetActiveCarTrans() : null;
                if (activeTrans != null)
                {
                    spawnPos = activeTrans.position + activeTrans.right * 4.0f;
                }
                else if (Camera.main != null)
                {
                    spawnPos = Camera.main.transform.position + Camera.main.transform.forward * 5f;
                }

                GameObject roadsterGo = new GameObject("Tesla_Rocket_Roadster");
                roadsterGo.transform.position = spawnPos;
                roadsterGo.transform.rotation = Quaternion.identity;
                roadsterGo.AddComponent<RoadsterActor>();
            }

            RoadsterActor.ActiveRoadster.Vehicle.Trim = trim;
            RoadsterActor.ActiveRoadster.EnterRoadster();

            // Deactivate original game car if player was inside
            if (SimControl.current != null && SimControl.current.isInACar)
            {
                CarController cc = Traverse.Create(SimControl.current).Field("carControlScript").GetValue<CarController>();
                if (cc != null)
                {
                    cc.PlayerExitsCar();
                }
                SimControl.current.isInACar = false;
            }
        }

        private void SwitchToOriginalCar(VehicleType vType)
        {
            if (RoadsterActor.ActiveRoadster != null && RoadsterActor.ActiveRoadster.IsPlayerInside)
            {
                RoadsterActor.ActiveRoadster.ExitRoadster();
            }

            if (SimControl.current != null)
            {
                GameObject[] cars = Traverse.Create(SimControl.current).Field("drivableCarsArray").GetValue<GameObject[]>();
                if (cars != null)
                {
                    foreach (GameObject carGo in cars)
                    {
                        CarController cc = carGo.GetComponent<CarController>();
                        if (cc != null && cc.vehicleType == vType)
                        {
                            Traverse.Create(SimControl.current).Method("ActivateCar", carGo.transform).GetValue();
                            IsOpen = false;
                            return;
                        }
                    }
                }
            }
        }

        private void TeleportActiveVehicle(Vector3 pos)
        {
            if (RoadsterActor.ActiveRoadster != null && RoadsterActor.ActiveRoadster.IsPlayerInside)
            {
                RoadsterActor.ActiveRoadster.transform.position = pos;
                RoadsterActor.ActiveRoadster.transform.rotation = Quaternion.identity;
                RoadsterActor.ActiveRoadster.Vehicle.Position = new V3(pos.x, pos.y, pos.z);
                RoadsterActor.ActiveRoadster.Vehicle.Velocity = V3.Zero;
                RoadsterActor.ActiveRoadster.Vehicle.ResetOrientation();
            }
            else if (SimControl.current != null)
            {
                Transform activeTrans = SimControl.current.GetActiveCarTrans();
                if (activeTrans != null)
                {
                    activeTrans.position = pos;
                    activeTrans.rotation = Quaternion.identity;
                }
            }
            IsOpen = false;
        }

        private void TeleportRoadsterToPlayer()
        {
            if (RoadsterActor.ActiveRoadster != null && Camera.main != null)
            {
                Vector3 pPos = Camera.main.transform.position + Camera.main.transform.forward * 4f;
                RoadsterActor.ActiveRoadster.transform.position = pPos;
                RoadsterActor.ActiveRoadster.transform.rotation = Quaternion.LookRotation(Camera.main.transform.forward, Vector3.up);
                RoadsterActor.ActiveRoadster.Vehicle.Position = new V3(pPos.x, pPos.y, pPos.z);
                RoadsterActor.ActiveRoadster.Vehicle.Velocity = V3.Zero;
                RoadsterActor.ActiveRoadster.Vehicle.ResetOrientation();
            }
        }

        private GUIStyle GetSectionStyle()
        {
            GUIStyle s = new GUIStyle(GUI.skin.label)
            {
                fontSize = 12,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            s.normal.textColor = new Color(0.1f, 0.85f, 1.0f);
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
