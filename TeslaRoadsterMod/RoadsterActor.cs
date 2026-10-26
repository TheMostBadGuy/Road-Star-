using System;
using System.Collections.Generic;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public class RoadsterActor : MonoBehaviour
    {
        public static RoadsterActor ActiveRoadster { get; private set; }

        public RoadsterVehicle Vehicle;
        public bool IsPlayerInside = false;

        // Visual Transforms
        private Transform bodyRoot;
        private Transform yokeTrans;
        private Transform rearWingTrans;
        private Transform skirtsTrans;
        private Transform[] fanTrans = new Transform[4];

        // Wheel visual nodes
        private Transform wheelFL_Steer;
        private Transform wheelFL_Spin;
        private Transform discFL;

        private Transform wheelFR_Steer;
        private Transform wheelFR_Spin;
        private Transform discFR;

        private Transform wheelRL_Spin;
        private Transform discRL;

        private Transform wheelRR_Spin;
        private Transform discRR;

        // Lights
        private Light frontLightLeft;
        private Light frontLightRight;
        private Light brakeLightLeft;
        private Light brakeLightRight;
        private Light cabinGlow;
        private Material tailLightMat;

        // Audio
        private AudioSource motorAudio;
        private AudioSource thrusterAudio;
        private AudioSource fanAudio;
        private AudioSource tireAudio;
        private AudioSource windAudio;

        // Thruster Particle Systems
        private ParticleSystem[] thrusterParticles = new ParticleSystem[10];

        // Tire Smoke FX
        private ParticleSystem[] tireSmokePS = new ParticleSystem[4];

        // Brake Rotor Materials & Thermals
        private Material matDiscFL;
        private Material matDiscFR;
        private Material matDiscRL;
        private Material matDiscRR;
        private float brakeTempFL = 25f;
        private float brakeTempFR = 25f;
        private float brakeTempRL = 25f;
        private float brakeTempRR = 25f;

        // Physics State
        private Rigidbody rb;
        private BoxCollider bodyCollider;

        void Awake()
        {
            ActiveRoadster = this;
            Vehicle = new RoadsterVehicle();

            // Set up Unity Rigidbody for collision interactions with buildings/fences
            rb = gameObject.GetComponent<Rigidbody>();
            if (rb == null) rb = gameObject.AddComponent<Rigidbody>();
            rb.mass = 2000f;
            rb.drag = 0.05f;
            rb.angularDrag = 0.5f;
            rb.interpolation = RigidbodyInterpolation.Interpolate;
            rb.collisionDetectionMode = CollisionDetectionMode.ContinuousDynamic;

            bodyCollider = gameObject.GetComponent<BoxCollider>();
            if (bodyCollider == null) bodyCollider = gameObject.AddComponent<BoxCollider>();
            bodyCollider.center = new Vector3(0f, 0.55f, 0f);
            bodyCollider.size = new Vector3(1.95f, 0.95f, 4.30f);

            BuildVisualHierarchy();
            SetupLights();
            SetupAudio();
            SetupThrusterFX();
            SetupTireSmokeFX();
        }

        void Start()
        {
            // Sync vehicle initial pose from transform
            Vehicle.Position = new V3(transform.position.x, transform.position.y, transform.position.z);
            Vehicle.Orientation = new Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w);
        }

        private void BuildVisualHierarchy()
        {
            bodyRoot = new GameObject("VisualRoot").transform;
            bodyRoot.SetParent(transform, false);

            // Wheel Centers in Unity coordinates:
            Vector3 centerFL = new Vector3(-0.855f, 0.343f, 1.412f);
            Vector3 centerFR = new Vector3(0.855f, 0.343f, 1.412f);
            Vector3 centerRL = new Vector3(-0.844f, 0.367f, -1.413f);
            Vector3 centerRR = new Vector3(0.844f, 0.367f, -1.413f);

            // Front Left Wheel Hierarchy (Steer Node -> Spin Node -> Mesh)
            GameObject flSteerGo = new GameObject("Wheel_FL_Steer");
            flSteerGo.transform.SetParent(bodyRoot, false);
            flSteerGo.transform.localPosition = centerFL;
            wheelFL_Steer = flSteerGo.transform;

            GameObject flSpinGo = new GameObject("Wheel_FL_Spin");
            flSpinGo.transform.SetParent(wheelFL_Steer, false);
            wheelFL_Spin = flSpinGo.transform;

            AttachSubmesh("Preview_Wheel_Front_FL", wheelFL_Spin, -centerFL);
            discFL = AttachSubmesh("Preview_Brake_Front_Disc_FL", wheelFL_Spin, -centerFL);
            if (discFL != null) matDiscFL = discFL.GetComponent<Renderer>().material;
            AttachSubmesh("Preview_Brake_Front_Caliper_FL", wheelFL_Steer, -centerFL);

            // Front Right Wheel Hierarchy
            GameObject frSteerGo = new GameObject("Wheel_FR_Steer");
            frSteerGo.transform.SetParent(bodyRoot, false);
            frSteerGo.transform.localPosition = centerFR;
            wheelFR_Steer = frSteerGo.transform;

            GameObject frSpinGo = new GameObject("Wheel_FR_Spin");
            frSpinGo.transform.SetParent(wheelFR_Steer, false);
            wheelFR_Spin = frSpinGo.transform;

            AttachSubmesh("Preview_Wheel_Front_FR", wheelFR_Spin, -centerFR);
            discFR = AttachSubmesh("Preview_Brake_Front_Disc_FR", wheelFR_Spin, -centerFR);
            if (discFR != null) matDiscFR = discFR.GetComponent<Renderer>().material;
            AttachSubmesh("Preview_Brake_Front_Caliper_FR", wheelFR_Steer, -centerFR);

            // Rear Left Wheel Hierarchy
            GameObject rlSpinGo = new GameObject("Wheel_RL_Spin");
            rlSpinGo.transform.SetParent(bodyRoot, false);
            rlSpinGo.transform.localPosition = centerRL;
            wheelRL_Spin = rlSpinGo.transform;

            AttachSubmesh("Preview_Wheel_Rear_RL", wheelRL_Spin, -centerRL);
            discRL = AttachSubmesh("Preview_Brake_Rear_Disc_RL", wheelRL_Spin, -centerRL);
            if (discRL != null) matDiscRL = discRL.GetComponent<Renderer>().material;
            AttachSubmesh("Preview_Brake_Rear_Caliper_RL", bodyRoot, Vector3.zero);

            // Rear Right Wheel Hierarchy
            GameObject rrSpinGo = new GameObject("Wheel_RR_Spin");
            rrSpinGo.transform.SetParent(bodyRoot, false);
            rrSpinGo.transform.localPosition = centerRR;
            wheelRR_Spin = rrSpinGo.transform;

            AttachSubmesh("Preview_Wheel_Rear_RR", wheelRR_Spin, -centerRR);
            discRR = AttachSubmesh("Preview_Brake_Rear_Disc_RR", wheelRR_Spin, -centerRR);
            if (discRR != null) matDiscRR = discRR.GetComponent<Renderer>().material;
            AttachSubmesh("Preview_Brake_Rear_Caliper_RR", bodyRoot, Vector3.zero);

            // Yoke Cockpit Steering
            Vector3 yokeCenter = new Vector3(-0.380f, 0.761f, 0.537f);
            GameObject yokeGo = new GameObject("Yoke_Pivot");
            yokeGo.transform.SetParent(bodyRoot, false);
            yokeGo.transform.localPosition = yokeCenter;
            yokeTrans = yokeGo.transform;
            AttachSubmesh("Yoke", yokeTrans, -yokeCenter);

            // Rear Wing Active Aero
            Vector3 wingCenter = new Vector3(0.000f, 1.105f, -2.130f);
            GameObject wingGo = new GameObject("RearWing_Pivot");
            wingGo.transform.SetParent(bodyRoot, false);
            wingGo.transform.localPosition = wingCenter;
            rearWingTrans = wingGo.transform;
            AttachSubmesh("Rear_Wing", rearWingTrans, -wingCenter);

            // Active Skirts
            Transform skirtsObj = AttachSubmesh("Skirts", bodyRoot, Vector3.zero);
            if (skirtsObj != null) skirtsTrans = skirtsObj;

            // 4 Suction Fans
            Vector3[] fanCenters = new Vector3[]
            {
                new Vector3( 0.452f, 0.379f, -1.966f),
                new Vector3( 0.145f, 0.379f, -1.966f),
                new Vector3(-0.145f, 0.379f, -1.966f),
                new Vector3(-0.452f, 0.379f, -1.966f)
            };

            for (int i = 0; i < 4; i++)
            {
                GameObject fGo = new GameObject("Fan_" + i + "_Pivot");
                fGo.transform.SetParent(bodyRoot, false);
                fGo.transform.localPosition = fanCenters[i];
                fanTrans[i] = fGo.transform;
                AttachSubmesh("Fan_" + i, fanTrans[i], -fanCenters[i]);
            }

            // Chassis & Static Body Parts
            string[] staticParts = new string[]
            {
                "Roadster_Body", "Hood_Vent", "Front_Lights", "Tail_Lights", "Side_Details",
                "Splitter", "Plate", "Plate_Text", "Rear_Diffuser", "Fan_Box",
                "Thrusters_Rear", "Thrusters_Rocket", "COPV_Rocket", "COPV_Trunk", "ColdGas_Plumbing",
                "Interior", "Seat_Driver", "Seat_Passenger", "Screen_Display", "Screen_Bezel",
                "Forward_Display_A", "Forward_Display_B", "Barge_L", "Barge_R"
            };

            foreach (string part in staticParts)
            {
                Transform t = AttachSubmesh(part, bodyRoot, Vector3.zero);
                if (part == "Tail_Lights" && t != null)
                {
                    Renderer rend = t.GetComponent<Renderer>();
                    if (rend != null && rend.materials.Length > 0)
                    {
                        tailLightMat = rend.materials[0];
                    }
                }
                else if (part == "Screen_Display" && t != null && InCabinScreen.Instance != null)
                {
                    InCabinScreen.Instance.BindScreenRenderer(t.GetComponent<Renderer>());
                }
            }

            // Suspension arms
            string[] suspParts = new string[]
            {
                "Susp_FL_LowerArm", "Susp_FL_UpperArm", "Susp_FL_Upright", "Susp_FL_TieRod", "Susp_FL_Pushrod", "Susp_FL_Rocker", "Susp_FL_Damper", "Susp_FL_DamperRod", "Susp_FL_Spring", "Susp_FL_Mounts",
                "Susp_FR_LowerArm", "Susp_FR_UpperArm", "Susp_FR_Upright", "Susp_FR_TieRod", "Susp_FR_Pushrod", "Susp_FR_Rocker", "Susp_FR_Damper", "Susp_FR_DamperRod", "Susp_FR_Spring", "Susp_FR_Mounts",
                "Susp_RL_LowerArm", "Susp_RL_UpperArm", "Susp_RL_Upright", "Susp_RL_ToeLink", "Susp_RL_Halfshaft", "Susp_RL_Pushrod", "Susp_RL_Rocker", "Susp_RL_Damper", "Susp_RL_DamperRod", "Susp_RL_Spring", "Susp_RL_Mounts",
                "Susp_RR_LowerArm", "Susp_RR_UpperArm", "Susp_RR_Upright", "Susp_RR_ToeLink", "Susp_RR_Halfshaft", "Susp_RR_Pushrod", "Susp_RR_Rocker", "Susp_RR_Damper", "Susp_RR_DamperRod", "Susp_RR_Spring", "Susp_RR_Mounts",
                "Susp_F_SteeringRack"
            };

            foreach (string sp in suspParts)
            {
                AttachSubmesh(sp, bodyRoot, Vector3.zero);
            }
        }

        private Transform AttachSubmesh(string meshName, Transform parent, Vector3 localOffset)
        {
            if (!MeshLoader.LoadedMeshes.ContainsKey(meshName)) return null;

            MeshData md = MeshLoader.LoadedMeshes[meshName];
            GameObject go = new GameObject(meshName);
            go.transform.SetParent(parent, false);
            go.transform.localPosition = localOffset;

            MeshFilter mf = go.AddComponent<MeshFilter>();
            mf.sharedMesh = md.ToUnityMesh();

            MeshRenderer mr = go.AddComponent<MeshRenderer>();
            Material[] mats = new Material[md.MaterialNames.Count];
            for (int i = 0; i < md.MaterialNames.Count; i++)
            {
                string mName = md.MaterialNames[i];
                if (MeshLoader.SharedMaterials.ContainsKey(mName))
                {
                    mats[i] = MeshLoader.SharedMaterials[mName];
                }
                else
                {
                    mats[i] = new Material(Shader.Find("Standard"));
                }
            }
            mr.sharedMaterials = mats;
            mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On;
            mr.receiveShadows = true;

            return go.transform;
        }

        private void SetupLights()
        {
            // Front Projector Headlights
            GameObject flGo = new GameObject("FrontLight_Left");
            flGo.transform.SetParent(bodyRoot, false);
            flGo.transform.localPosition = new Vector3(-0.68f, 0.58f, 1.85f);
            frontLightLeft = flGo.AddComponent<Light>();
            frontLightLeft.type = LightType.Spot;
            frontLightLeft.color = new Color(0.95f, 0.98f, 1.0f);
            frontLightLeft.intensity = 3.5f;
            frontLightLeft.range = 80f;
            frontLightLeft.spotAngle = 65f;
            frontLightLeft.shadows = LightShadows.Soft;

            GameObject frGo = new GameObject("FrontLight_Right");
            frGo.transform.SetParent(bodyRoot, false);
            frGo.transform.localPosition = new Vector3(0.68f, 0.58f, 1.85f);
            frontLightRight = frGo.AddComponent<Light>();
            frontLightRight.type = LightType.Spot;
            frontLightRight.color = new Color(0.95f, 0.98f, 1.0f);
            frontLightRight.intensity = 3.5f;
            frontLightRight.range = 80f;
            frontLightRight.spotAngle = 65f;
            frontLightRight.shadows = LightShadows.Soft;

            // Rear Brake Glow Lights
            GameObject blGo = new GameObject("BrakeLight_Left");
            blGo.transform.SetParent(bodyRoot, false);
            blGo.transform.localPosition = new Vector3(-0.65f, 0.72f, -2.15f);
            brakeLightLeft = blGo.AddComponent<Light>();
            brakeLightLeft.type = LightType.Point;
            brakeLightLeft.color = new Color(1.0f, 0.05f, 0.02f);
            brakeLightLeft.intensity = 1.0f;
            brakeLightLeft.range = 4.5f;

            GameObject brGo = new GameObject("BrakeLight_Right");
            brGo.transform.SetParent(bodyRoot, false);
            brGo.transform.localPosition = new Vector3(0.65f, 0.72f, -2.15f);
            brakeLightRight = brGo.AddComponent<Light>();
            brakeLightRight.type = LightType.Point;
            brakeLightRight.color = new Color(1.0f, 0.05f, 0.02f);
            brakeLightRight.intensity = 1.0f;
            brakeLightRight.range = 4.5f;

            // Interior Cabin Glow
            GameObject cabGo = new GameObject("Cabin_Ambient_Glow");
            cabGo.transform.SetParent(bodyRoot, false);
            cabGo.transform.localPosition = new Vector3(0f, 0.75f, 0.2f);
            cabinGlow = cabGo.AddComponent<Light>();
            cabinGlow.type = LightType.Point;
            cabinGlow.color = new Color(0.95f, 0.15f, 0.10f);
            cabinGlow.intensity = 0.65f;
            cabinGlow.range = 2.2f;
        }

        private void SetupAudio()
        {
            motorAudio = AddAudioChannel("S_Motor", true, 0f);
            thrusterAudio = AddAudioChannel("S_Thrust", true, 0f);
            fanAudio = AddAudioChannel("S_Fan", true, 0f);
            tireAudio = AddAudioChannel("S_Tire", true, 0f);
            windAudio = AddAudioChannel("S_Wind", true, 0f);
        }

        private AudioSource AddAudioChannel(string clipName, bool loop, float startVol)
        {
            GameObject go = new GameObject("Audio_" + clipName);
            go.transform.SetParent(transform, false);
            AudioSource src = go.AddComponent<AudioSource>();
            if (MeshLoader.LoadedAudio.ContainsKey(clipName))
            {
                src.clip = MeshLoader.LoadedAudio[clipName];
            }
            src.loop = loop;
            src.volume = startVol;
            src.spatialBlend = 0.85f;
            src.minDistance = 3f;
            src.maxDistance = 60f;
            src.dopplerLevel = 1.0f;
            src.Play();
            return src;
        }

        private void SetupThrusterFX()
        {
            Vector3[] nozzlePositions = new Vector3[]
            {
                // 2 Rear Boost Nozzles (X=lateral, Y=up, Z=forward)
                new Vector3(-0.25f, 0.35f, -2.18f),
                new Vector3( 0.25f, 0.35f, -2.18f),
                // 4 Bottom Hover/Jump Nozzles
                new Vector3(-0.70f, 0.12f,  1.30f),
                new Vector3( 0.70f, 0.12f,  1.30f),
                new Vector3(-0.70f, 0.12f, -1.30f),
                new Vector3( 0.70f, 0.12f, -1.30f),
                // 2 Front Downforce Nozzles
                new Vector3(-0.35f, 0.55f,  2.10f),
                new Vector3( 0.35f, 0.55f,  2.10f),
                // 2 Lateral Sway/Yaw Nozzles
                new Vector3(-0.95f, 0.45f, -1.80f),
                new Vector3( 0.95f, 0.45f, -1.80f)
            };

            Vector3[] nozzleDirs = new Vector3[]
            {
                Vector3.back, Vector3.back,
                Vector3.down, Vector3.down, Vector3.down, Vector3.down,
                Vector3.up, Vector3.up,
                Vector3.left, Vector3.right
            };

            for (int i = 0; i < 10; i++)
            {
                GameObject fxGo = new GameObject("Thruster_FX_" + i);
                fxGo.transform.SetParent(bodyRoot, false);
                fxGo.transform.localPosition = nozzlePositions[i];
                fxGo.transform.localRotation = Quaternion.LookRotation(nozzleDirs[i]);

                ParticleSystem ps = fxGo.AddComponent<ParticleSystem>();
                var main = ps.main;
                main.startLifetime = 0.12f;
                main.startSpeed = 35f;
                main.startSize = 0.22f;
                main.startColor = new Color(0.9f, 0.95f, 1.0f, 0.75f);
                main.simulationSpace = ParticleSystemSimulationSpace.World;
                main.playOnAwake = false;

                var emission = ps.emission;
                emission.rateOverTime = 0f;

                var shape = ps.shape;
                shape.shapeType = ParticleSystemShapeType.Cone;
                shape.angle = 8f;
                shape.radius = 0.04f;

                thrusterParticles[i] = ps;
            }
        }

        private void SetupTireSmokeFX()
        {
            Vector3[] tireGroundPos = new Vector3[]
            {
                new Vector3(-0.855f, 0.05f,  1.412f),
                new Vector3( 0.855f, 0.05f,  1.412f),
                new Vector3(-0.844f, 0.05f, -1.413f),
                new Vector3( 0.844f, 0.05f, -1.413f)
            };

            for (int i = 0; i < 4; i++)
            {
                GameObject smGo = new GameObject("TireSmoke_FX_" + i);
                smGo.transform.SetParent(bodyRoot, false);
                smGo.transform.localPosition = tireGroundPos[i];
                smGo.transform.localRotation = Quaternion.Euler(-90f, 0f, 0f);

                ParticleSystem ps = smGo.AddComponent<ParticleSystem>();
                var main = ps.main;
                main.startLifetime = 1.2f;
                main.startSpeed = 2.5f;
                main.startSize = 0.45f;
                main.startColor = new Color(0.92f, 0.92f, 0.95f, 0.45f);
                main.simulationSpace = ParticleSystemSimulationSpace.World;
                main.playOnAwake = false;

                var emission = ps.emission;
                emission.rateOverTime = 0f;

                var shape = ps.shape;
                shape.shapeType = ParticleSystemShapeType.Cone;
                shape.angle = 20f;
                shape.radius = 0.15f;

                tireSmokePS[i] = ps;
            }
        }

        private void UpdateDiscGlow(Material mat, float temp)
        {
            if (mat == null) return;
            if (temp > 350f)
            {
                float t = Mathf.Clamp01((temp - 350f) / 500f);
                Color glow = Color.Lerp(new Color(2.5f, 0.25f, 0.02f), new Color(4.5f, 2.2f, 0.2f), t);
                mat.EnableKeyword("_EMISSION");
                mat.SetColor("_EmissionColor", glow * (0.5f + t * 2.0f));
            }
            else
            {
                mat.SetColor("_EmissionColor", Color.black);
            }
        }

        void FixedUpdate()
        {
            if (Vehicle == null) return;

            // Step the 6-DoF symplectic physical simulation engine
            Vehicle.Step(Time.fixedDeltaTime);

            // Sync Unity Transform from physical state
            transform.position = new Vector3((float)Vehicle.Position.x, (float)Vehicle.Position.y, (float)Vehicle.Position.z);
            transform.rotation = new Quaternion((float)Vehicle.Orientation.x, (float)Vehicle.Orientation.y, (float)Vehicle.Orientation.z, (float)Vehicle.Orientation.w);

            if (rb != null)
            {
                rb.velocity = new Vector3((float)Vehicle.Velocity.x, (float)Vehicle.Velocity.y, (float)Vehicle.Velocity.z);
                rb.angularVelocity = new Vector3((float)Vehicle.AngularVelocity.x, (float)Vehicle.AngularVelocity.y, (float)Vehicle.AngularVelocity.z);
            }
        }

        void Update()
        {
            UpdateVisualComponents();
            UpdateAudio();
            UpdateThrusterEffects();
        }

        private void UpdateVisualComponents()
        {
            float dt = Time.deltaTime;
            float steerAngle = (float)Vehicle.SteeringAngle * Mathf.Rad2Deg;

            // 1. Steer Front Wheels
            if (wheelFL_Steer != null) wheelFL_Steer.localRotation = Quaternion.Euler(0f, steerAngle, 0f);
            if (wheelFR_Steer != null) wheelFR_Steer.localRotation = Quaternion.Euler(0f, steerAngle, 0f);

            // 2. Spin All 4 Wheels & Discs
            float speedMPS = (float)Vehicle.ForwardSpeed;
            float wheelSpinAngle = (speedMPS / 0.34f) * Mathf.Rad2Deg * dt;

            if (wheelFL_Spin != null) wheelFL_Spin.Rotate(wheelSpinAngle, 0f, 0f, Space.Self);
            if (wheelFR_Spin != null) wheelFR_Spin.Rotate(wheelSpinAngle, 0f, 0f, Space.Self);
            if (wheelRL_Spin != null) wheelRL_Spin.Rotate(wheelSpinAngle, 0f, 0f, Space.Self);
            if (wheelRR_Spin != null) wheelRR_Spin.Rotate(wheelSpinAngle, 0f, 0f, Space.Self);

            // 3. Cockpit Yoke Steer Rotation
            if (yokeTrans != null)
            {
                yokeTrans.localRotation = Quaternion.Euler(0f, 0f, -steerAngle * 4.2f);
            }

            // 4. Active Rear Wing Aero
            if (rearWingTrans != null)
            {
                float targetAngle = 0f;
                if (Vehicle.IsAirbrakeActive) targetAngle = 25f;
                else if (Vehicle.IsDRSActive) targetAngle = -5f;
                else targetAngle = Mathf.Clamp(speedMPS * 0.15f, 0f, 12f);

                rearWingTrans.localRotation = Quaternion.Slerp(rearWingTrans.localRotation, Quaternion.Euler(targetAngle, 0f, 0f), dt * 8f);
            }

            // 5. Active Skirts deployment with fan suction
            if (skirtsTrans != null)
            {
                float targetSkirtY = Vehicle.IsFanSuctionActive ? -0.045f : 0f;
                skirtsTrans.localPosition = Vector3.Lerp(skirtsTrans.localPosition, new Vector3(0f, targetSkirtY, 0f), dt * 6f);
            }

            // 6. 4 Underbody Suction Fans Spinning
            float fanAngle = (float)Vehicle.FanRPM * 6f * dt;
            for (int i = 0; i < 4; i++)
            {
                if (fanTrans[i] != null)
                {
                    fanTrans[i].Rotate(0f, fanAngle * ((i % 2 == 0) ? 1f : -1f), 0f, Space.Self);
                }
            }

            // 7. Dynamic Brake Lights
            bool isBraking = Vehicle.IsBraking;
            if (brakeLightLeft != null) brakeLightLeft.intensity = isBraking ? 3.5f : 0.6f;
            if (brakeLightRight != null) brakeLightRight.intensity = isBraking ? 3.5f : 0.6f;

            if (tailLightMat != null)
            {
                Color emission = isBraking ? new Color(5.0f, 0.05f, 0.02f) : new Color(1.2f, 0.02f, 0.01f);
                tailLightMat.SetColor("_EmissionColor", emission);
            }

            // 8. Dynamic Carbon-Ceramic Brake Rotor Glowing Under High-Speed Braking
            float brakePwr = isBraking ? Mathf.Clamp01(speedMPS / 40f) * 180f : 0f;
            brakeTempFL = Mathf.Clamp(brakeTempFL + (brakePwr * 0.65f - (brakeTempFL - 25f) * (0.08f + speedMPS * 0.005f)) * dt, 25f, 950f);
            brakeTempFR = Mathf.Clamp(brakeTempFR + (brakePwr * 0.65f - (brakeTempFR - 25f) * (0.08f + speedMPS * 0.005f)) * dt, 25f, 950f);
            brakeTempRL = Mathf.Clamp(brakeTempRL + (brakePwr * 0.35f - (brakeTempRL - 25f) * (0.08f + speedMPS * 0.005f)) * dt, 25f, 950f);
            brakeTempRR = Mathf.Clamp(brakeTempRR + (brakePwr * 0.35f - (brakeTempRR - 25f) * (0.08f + speedMPS * 0.005f)) * dt, 25f, 950f);

            UpdateDiscGlow(matDiscFL, brakeTempFL);
            UpdateDiscGlow(matDiscFR, brakeTempFR);
            UpdateDiscGlow(matDiscRL, brakeTempRL);
            UpdateDiscGlow(matDiscRR, brakeTempRR);

            // 9. Tire Friction Billowing Smoke under Hard Slip or Rocket Launch
            float slip = (float)Vehicle.MaxTireSlip;
            bool highSlip = (slip > 0.16f && speedMPS > 2f) || (Vehicle.IsThrusterFiring && speedMPS < 20f && Vehicle.ThrottleInput > 0.5);
            for (int i = 0; i < 4; i++)
            {
                if (tireSmokePS[i] != null)
                {
                    var em = tireSmokePS[i].emission;
                    em.rateOverTime = highSlip ? Mathf.Clamp((slip - 0.14f) * 85f + 25f, 15f, 85f) : 0f;
                }
            }
        }

        private void UpdateAudio()
        {
            float speedKmh = Mathf.Abs((float)Vehicle.ForwardSpeed * 3.6f);
            float throttle = (float)Vehicle.ThrottleInput;

            // Motor Inverter Whine
            if (motorAudio != null)
            {
                float motorPitch = Mathf.Clamp(0.4f + (speedKmh / 420f) * 2.2f, 0.3f, 2.6f);
                float targetVol = Mathf.Clamp(0.15f + throttle * 0.75f + (speedKmh / 400f) * 0.35f, 0.1f, 1.0f);
                motorAudio.pitch = Mathf.Lerp(motorAudio.pitch, motorPitch, Time.deltaTime * 10f);
                motorAudio.volume = Mathf.Lerp(motorAudio.volume, targetVol, Time.deltaTime * 8f);
            }

            // Thruster Roar
            if (thrusterAudio != null)
            {
                float thrustActive = Vehicle.IsThrusterFiring ? 1.0f : 0.0f;
                thrusterAudio.volume = Mathf.Lerp(thrusterAudio.volume, thrustActive * 0.95f, Time.deltaTime * 20f);
                thrusterAudio.pitch = 0.95f + (float)(Vehicle.ColdGas.TankPressureBar / 379.0) * 0.25f;
            }

            // Fan Spool Whine
            if (fanAudio != null)
            {
                float fanRatio = (float)(Vehicle.FanRPM / 18000.0);
                fanAudio.volume = Mathf.Lerp(fanAudio.volume, fanRatio * 0.85f, Time.deltaTime * 5f);
                fanAudio.pitch = 0.5f + fanRatio * 1.5f;
            }

            // Tire Screech
            if (tireAudio != null)
            {
                float slip = (float)Vehicle.MaxTireSlip;
                float slipVol = Mathf.Clamp01((slip - 0.12f) * 3.5f);
                tireAudio.volume = Mathf.Lerp(tireAudio.volume, slipVol * 0.8f, Time.deltaTime * 15f);
                tireAudio.pitch = 0.85f + slipVol * 0.3f;
            }

            // Wind Rush
            if (windAudio != null)
            {
                float windVol = Mathf.Clamp01((speedKmh - 60f) / 360f);
                windAudio.volume = Mathf.Lerp(windAudio.volume, windVol * 0.7f, Time.deltaTime * 6f);
                windAudio.pitch = 0.7f + windVol * 0.6f;
            }
        }

        private void UpdateThrusterEffects()
        {
            bool firing = Vehicle.IsThrusterFiring;
            float massFlow = (float)Vehicle.ColdGasMassFlow;

            for (int i = 0; i < 10; i++)
            {
                if (thrusterParticles[i] != null)
                {
                    var em = thrusterParticles[i].emission;
                    if (firing && Vehicle.IsNozzleActive(i))
                    {
                        em.rateOverTime = Mathf.Clamp(massFlow * 120f, 60f, 250f);
                    }
                    else
                    {
                        em.rateOverTime = 0f;
                    }
                }
            }
        }

        public void EnterRoadster()
        {
            IsPlayerInside = true;
            if (cabinGlow != null) cabinGlow.enabled = true;
            if (frontLightLeft != null) frontLightLeft.enabled = true;
            if (frontLightRight != null) frontLightRight.enabled = true;
        }

        public void ExitRoadster()
        {
            IsPlayerInside = false;
            Vehicle.ThrottleInput = 0;
            Vehicle.BrakeInput = 1.0;
        }
    }
}
