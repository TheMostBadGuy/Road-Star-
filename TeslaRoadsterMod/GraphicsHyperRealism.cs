using System;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.PostProcessing;

namespace TeslaRoadsterMod
{
    public class GraphicsHyperRealism : MonoBehaviour
    {
        public static GraphicsHyperRealism Instance { get; private set; }

        private PostProcessingProfile profile;
        private PostProcessingBehaviour currentBehaviour;
        private ReflectionProbe dynamicProbe;
        private Light dirSunLight;
        private bool isEnhanced = false;

        void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }
            Instance = this;
            DontDestroyOnLoad(gameObject);
        }

        void Start()
        {
            ApplyUltraQualitySettings();
            SetupPostProcessingProfile();
            EnhanceAllCameras();
            CreateDynamicReflectionProbe();
        }

        void Update()
        {
            Camera cam = Camera.main;
            if (cam != null && (currentBehaviour == null || currentBehaviour.gameObject != cam.gameObject))
            {
                AttachToCamera(cam);
            }

            if (dynamicProbe != null)
            {
                if (RoadsterActor.ActiveRoadster != null)
                {
                    dynamicProbe.transform.position = RoadsterActor.ActiveRoadster.transform.position + Vector3.up * 1.2f;
                }
                else if (cam != null)
                {
                    dynamicProbe.transform.position = cam.transform.position;
                }
            }
        }

        public void ApplyUltraQualitySettings()
        {
            QualitySettings.vSyncCount = 1;
            QualitySettings.anisotropicFiltering = AnisotropicFiltering.ForceEnable;
            QualitySettings.shadowCascades = 4;
            QualitySettings.shadowCascade4Split = new Vector3(0.06f, 0.20f, 0.45f);
            QualitySettings.shadowDistance = 350f;
            QualitySettings.shadowResolution = ShadowResolution.VeryHigh;
            QualitySettings.shadowProjection = ShadowProjection.CloseFit;
            QualitySettings.softParticles = true;
            QualitySettings.pixelLightCount = 8;
            QualitySettings.antiAliasing = 0; // TAA will take over for superior temporal stability
            QualitySettings.realtimeReflectionProbes = true;
            QualitySettings.billboardsFaceCameraPosition = true;

            // Tune Directional Sun if found
            Light[] lights = FindObjectsOfType<Light>();
            foreach (Light l in lights)
            {
                if (l.type == LightType.Directional)
                {
                    dirSunLight = l;
                    l.shadows = LightShadows.Soft;
                    l.shadowStrength = 1.0f;
                    l.shadowBias = 0.05f;
                    l.shadowNormalBias = 0.35f;
                    l.shadowNearPlane = 0.1f;
                    break;
                }
            }
        }

        public void SetupPostProcessingProfile()
        {
            profile = ScriptableObject.CreateInstance<PostProcessingProfile>();
            profile.name = "HyperRealistic_PostProcessing_Profile";

            // 1. Raytraced Screen Space Reflections (SSR)
            var ssr = new ScreenSpaceReflectionModel.Settings();
            ssr.reflection.reflectionQuality = ScreenSpaceReflectionModel.SSRResolution.High;
            ssr.reflection.maxDistance = 150f;
            ssr.reflection.iterationCount = 512;
            ssr.reflection.stepSize = 1;
            ssr.reflection.widthModifier = 0.5f;
            ssr.reflection.reflectionBlur = 1.0f;
            ssr.reflection.reflectBackfaces = false;
            ssr.reflection.blendType = ScreenSpaceReflectionModel.SSRReflectionBlendType.PhysicallyBased;
            ssr.intensity.reflectionMultiplier = 1.05f;
            ssr.intensity.fadeDistance = 120f;
            ssr.intensity.fresnelFade = 1.0f;
            ssr.intensity.fresnelFadePower = 1.0f;
            ssr.screenEdgeMask.intensity = 0.05f;

            profile.screenSpaceReflection.enabled = true;
            profile.screenSpaceReflection.settings = ssr;

            // 2. Full-Resolution Ambient Occlusion (SSAO)
            var ao = new AmbientOcclusionModel.Settings();
            ao.intensity = 1.25f;
            ao.radius = 0.45f;
            ao.sampleCount = AmbientOcclusionModel.SampleCount.High;
            ao.downsampling = false; // Full resolution AO!
            ao.forceForwardCompatibility = false;
            ao.ambientOnly = false;
            ao.highPrecision = true;

            profile.ambientOcclusion.enabled = true;
            profile.ambientOcclusion.settings = ao;

            // 3. Cinematic Bloom
            var bloom = new BloomModel.Settings();
            bloom.bloom.intensity = 0.60f;
            bloom.bloom.threshold = 1.08f;
            bloom.bloom.softKnee = 0.55f;
            bloom.bloom.radius = 4.2f;
            bloom.bloom.antiFlicker = true;

            profile.bloom.enabled = true;
            profile.bloom.settings = bloom;

            // 4. ACES Filmic Tonemapping & Color Grading
            var cg = new ColorGradingModel.Settings();
            cg.tonemapping.tonemapper = ColorGradingModel.Tonemapper.ACES;
            cg.basic.contrast = 1.15f;
            cg.basic.saturation = 1.08f;
            cg.basic.postExposure = 0.15f;
            cg.basic.temperature = 2.0f;
            cg.basic.tint = 0.0f;

            profile.colorGrading.enabled = true;
            profile.colorGrading.settings = cg;

            // 5. Temporal Anti-Aliasing (TAA)
            var aa = new AntialiasingModel.Settings();
            aa.method = AntialiasingModel.Method.Taa;
            aa.taaSettings.jitterSpread = 0.75f;
            aa.taaSettings.sharpen = 0.35f;
            aa.taaSettings.stationaryBlending = 0.95f;
            aa.taaSettings.motionBlending = 0.85f;

            profile.antialiasing.enabled = true;
            profile.antialiasing.settings = aa;

            // 6. Motion Blur
            var mb = new MotionBlurModel.Settings();
            mb.shutterAngle = 180f;
            mb.sampleCount = 10;
            mb.frameBlending = 0.0f;

            profile.motionBlur.enabled = true;
            profile.motionBlur.settings = mb;

            // 7. Subtle Vignette
            var vig = new VignetteModel.Settings();
            vig.mode = VignetteModel.Mode.Classic;
            vig.color = new Color(0.02f, 0.02f, 0.02f, 1f);
            vig.intensity = 0.18f;
            vig.smoothness = 0.35f;
            vig.roundness = 1.0f;

            profile.vignette.enabled = true;
            profile.vignette.settings = vig;

            // 8. Chromatic Aberration
            var ca = new ChromaticAberrationModel.Settings();
            ca.intensity = 0.08f;
            profile.chromaticAberration.enabled = true;
            profile.chromaticAberration.settings = ca;
        }

        public void EnhanceAllCameras()
        {
            Camera[] cams = FindObjectsOfType<Camera>();
            foreach (Camera c in cams)
            {
                AttachToCamera(c);
            }
        }

        public void AttachToCamera(Camera cam)
        {
            if (cam == null) return;

            cam.renderingPath = RenderingPath.DeferredShading;
            cam.allowHDR = true;
            cam.allowMSAA = false; // TAA is used
            cam.useOcclusionCulling = true;
            cam.depthTextureMode = DepthTextureMode.Depth | DepthTextureMode.DepthNormals | DepthTextureMode.MotionVectors;

            PostProcessingBehaviour ppb = cam.GetComponent<PostProcessingBehaviour>();
            if (ppb == null)
            {
                ppb = cam.gameObject.AddComponent<PostProcessingBehaviour>();
            }

            if (profile != null)
            {
                ppb.profile = profile;
            }

            currentBehaviour = ppb;
        }

        private void CreateDynamicReflectionProbe()
        {
            if (dynamicProbe != null) return;

            GameObject probeGo = new GameObject("Tesla_Dynamic_Reflection_Probe");
            probeGo.transform.SetParent(transform);
            dynamicProbe = probeGo.AddComponent<ReflectionProbe>();

            dynamicProbe.resolution = 256;
            dynamicProbe.mode = ReflectionProbeMode.Realtime;
            dynamicProbe.refreshMode = ReflectionProbeRefreshMode.EveryFrame;
            dynamicProbe.timeSlicingMode = ReflectionProbeTimeSlicingMode.IndividualFaces;
            dynamicProbe.clearFlags = ReflectionProbeClearFlags.Skybox;
            dynamicProbe.size = new Vector3(80f, 40f, 80f);
            dynamicProbe.importance = 2;
            dynamicProbe.boxProjection = true;
            dynamicProbe.intensity = 1.1f;
            dynamicProbe.hdr = true;
            dynamicProbe.nearClipPlane = 0.2f;
            dynamicProbe.farClipPlane = 250f;
        }
    }
}
