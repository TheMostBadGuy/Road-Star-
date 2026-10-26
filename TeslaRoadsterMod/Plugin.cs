using System;
using System.IO;
using UnityEngine;
using BepInEx;
using HarmonyLib;

namespace TeslaRoadsterMod
{
    [BepInPlugin("com.teslasim.rocketroadster", "Tesla Rocket Roadster Mod", "1.0.0")]
    public class RoadsterModPlugin : BaseUnityPlugin
    {
        public static RoadsterModPlugin Instance { get; private set; }
        private Harmony harmony;

        void Awake()
        {
            Instance = this;
            Logger.LogInfo("Tesla Rocket Roadster Mod Initializing...");

            try
            {
                // 1. Initialize 3D Assets, PBR Materials, Audio, and Textures
                string pluginDir = Paths.PluginPath;
                Logger.LogInfo("Loading 3D assets from: " + pluginDir);
                MeshLoader.Initialize(pluginDir);
                Logger.LogInfo("Assets loaded successfully. Meshes: " + MeshLoader.LoadedMeshes.Count + ", Audio: " + MeshLoader.LoadedAudio.Count);

                // 2. Spawn Mod Controller Systems
                GameObject modRoot = new GameObject("Tesla_Roadster_Mod_Systems");
                DontDestroyOnLoad(modRoot);

                modRoot.AddComponent<GraphicsHyperRealism>();
                modRoot.AddComponent<ModernHUD>();
                modRoot.AddComponent<TeslaTouchscreenUI>();
                modRoot.AddComponent<InCabinScreen>();
                modRoot.AddComponent<GamepadHaptics>();
                modRoot.AddComponent<CameraSystem>();
                modRoot.AddComponent<InputSystem>();
                modRoot.AddComponent<VehicleMenu>();

                // 3. Apply Harmony Patches
                harmony = new Harmony("com.teslasim.rocketroadster");
                harmony.PatchAll();

                Logger.LogInfo("Tesla Rocket Roadster Mod Initialized Successfully!");
            }
            catch (Exception ex)
            {
                Logger.LogError("Error initializing Tesla Roadster Mod: " + ex);
            }
        }

        void Update()
        {
            // Auto-spawn Roadster at start if none exists in scene
            if (RoadsterActor.ActiveRoadster == null && SimControl.current != null)
            {
                SpawnInitialRoadster();
            }

            // Global Enter / Exit handling with 'E'
            if (Input.GetKeyDown(KeyCode.E))
            {
                HandleEnterExitInteraction();
            }
        }

        private void SpawnInitialRoadster()
        {
            Vector3 spawnPos = Vector3.zero;
            Quaternion spawnRot = Quaternion.identity;

            Transform activeTrans = SimControl.current != null ? SimControl.current.GetActiveCarTrans() : null;
            if (activeTrans != null)
            {
                spawnPos = activeTrans.position + activeTrans.right * 4.5f;
                spawnRot = activeTrans.rotation;
            }
            else if (Camera.main != null)
            {
                spawnPos = Camera.main.transform.position + Camera.main.transform.forward * 6f;
            }

            GameObject roadsterGo = new GameObject("Tesla_Rocket_Roadster");
            roadsterGo.transform.position = spawnPos;
            roadsterGo.transform.rotation = spawnRot;
            roadsterGo.AddComponent<RoadsterActor>();
            Logger.LogInfo("Spawned Tesla Rocket Roadster at: " + spawnPos);
        }

        private void HandleEnterExitInteraction()
        {
            var actor = RoadsterActor.ActiveRoadster;
            if (actor == null) return;

            if (actor.IsPlayerInside)
            {
                // Player is exiting the Roadster
                actor.ExitRoadster();

                if (SimControl.current != null && SimControl.current.WalkCamera != null)
                {
                    SimControl.current.WalkCamera.SetActive(true);
                    SimControl.current.WalkCamera.transform.position = actor.transform.position - actor.transform.right * 2.2f + Vector3.up * 1.5f;
                    if (SimControl.current.ChaseCameraGO != null)
                    {
                        SimControl.current.ChaseCameraGO.SetActive(false);
                    }
                }
            }
            else
            {
                // Check if player is close enough to enter
                if (Camera.main != null)
                {
                    float dist = Vector3.Distance(Camera.main.transform.position, actor.transform.position);
                    if (dist < 5.0f)
                    {
                        // Exit existing game car if in one
                        if (SimControl.current != null && SimControl.current.isInACar)
                        {
                            CarController cc = Traverse.Create(SimControl.current).Field("carControlScript").GetValue<CarController>();
                            if (cc != null)
                            {
                                cc.PlayerExitsCar();
                            }
                            SimControl.current.isInACar = false;
                        }

                        // Enter Roadster
                        actor.EnterRoadster();

                        if (SimControl.current != null)
                        {
                            if (SimControl.current.WalkCamera != null)
                            {
                                SimControl.current.WalkCamera.SetActive(false);
                            }
                            if (SimControl.current.ChaseCameraGO != null)
                            {
                                SimControl.current.ChaseCameraGO.SetActive(true);
                            }
                        }

                        if (CameraSystem.Instance != null)
                        {
                            CameraSystem.Instance.ManagedCamera = Camera.main;
                        }
                    }
                }
            }
        }
    }

    // Harmony Patches to cleanly integrate with existing game UI and Systems
    [HarmonyPatch(typeof(GUIController), "Start")]
    public static class GUIController_Start_Patch
    {
        static void Postfix(GUIController __instance)
        {
            try
            {
                // Hide old basic text speedometer and background
                if (__instance.SpeedBG != null)
                {
                    __instance.SpeedBG.SetActive(false);
                }
                if (__instance.TextSpeed != null)
                {
                    __instance.TextSpeed.gameObject.SetActive(false);
                }
            }
            catch (Exception ex)
            {
                Debug.LogWarning("[RoadsterMod] GUIController patch warning: " + ex.Message);
            }
        }
    }

    [HarmonyPatch(typeof(GUIController), "UpdateSpeed")]
    public static class GUIController_UpdateSpeed_Patch
    {
        static bool Prefix(GUIController __instance)
        {
            // Suppress original text speed update so it doesn't re-enable the old text
            if (RoadsterActor.ActiveRoadster != null && RoadsterActor.ActiveRoadster.IsPlayerInside)
            {
                if (__instance.TextSpeed != null && __instance.TextSpeed.gameObject.activeSelf)
                {
                    __instance.TextSpeed.gameObject.SetActive(false);
                }
                return false;
            }
            return true;
        }
    }
}
