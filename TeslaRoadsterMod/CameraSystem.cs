using System;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public enum CameraViewMode
    {
        Chase,
        Cockpit,
        Hood,
        Wheel,
        Orbit
    }

    public class CameraSystem : MonoBehaviour
    {
        public static CameraSystem Instance { get; private set; }

        public CameraViewMode CurrentMode = CameraViewMode.Chase;
        public Camera ManagedCamera;

        // Chase Cam tuning
        public float ChaseDistance = 5.2f;
        public float ChaseHeight = 1.45f;
        public float DampingPos = 12f;
        public float DampingRot = 10f;
        public float BaseFOV = 62f;
        public float MaxSpeedFOV = 85f;

        // Cockpit tuning
        private readonly Vector3 cockpitOffset = new Vector3(-0.38f, 0.92f, 0.15f);

        // Hood tuning
        private readonly Vector3 hoodOffset = new Vector3(0.0f, 0.68f, 1.65f);

        // Wheel tuning
        private readonly Vector3 wheelOffset = new Vector3(-1.35f, 0.42f, 1.40f);

        // Orbit tuning
        private float orbitX = 0f;
        private float orbitY = 20f;
        private float orbitDist = 5.5f;

        // Shake tracking
        private float currentShake = 0f;

        void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }
            Instance = this;
        }

        void Start()
        {
            if (ManagedCamera == null)
            {
                ManagedCamera = Camera.main;
            }
        }

        void Update()
        {
            // Cycle Camera mode with 'C'
            if (Input.GetKeyDown(KeyCode.C))
            {
                CycleMode();
            }

            // Orbit controls with Right Mouse Button
            if (Input.GetMouseButton(1) || CurrentMode == CameraViewMode.Orbit)
            {
                orbitX += Input.GetAxis("Mouse X") * 3.5f;
                orbitY -= Input.GetAxis("Mouse Y") * 3.0f;
                orbitY = Mathf.Clamp(orbitY, -10f, 75f);
            }

            // Scroll zoom
            float scroll = Input.GetAxis("Mouse ScrollWheel");
            if (Mathf.Abs(scroll) > 0.01f)
            {
                orbitDist = Mathf.Clamp(orbitDist - scroll * 4.0f, 2.5f, 16.0f);
            }
        }

        public void CycleMode()
        {
            int next = ((int)CurrentMode + 1) % 5;
            CurrentMode = (CameraViewMode)next;
        }

        void LateUpdate()
        {
            var actor = RoadsterActor.ActiveRoadster;
            if (actor == null || !actor.IsPlayerInside) return;
            if (ManagedCamera == null)
            {
                ManagedCamera = Camera.main;
                if (ManagedCamera == null) return;
            }

            Transform target = actor.transform;
            float dt = Time.deltaTime;
            float speedMPS = Mathf.Abs((float)actor.Vehicle.ForwardSpeed);

            // Compute dynamic FOV based on speed
            float targetFOV = Mathf.Lerp(BaseFOV, MaxSpeedFOV, speedMPS / 120f);
            if (actor.Vehicle.IsThrusterFiring) targetFOV += 4.0f;
            ManagedCamera.fieldOfView = Mathf.Lerp(ManagedCamera.fieldOfView, targetFOV, dt * 6f);

            // Dynamic Camera Shake on Thrusters or High Speed
            float shakeAmp = 0f;
            if (actor.Vehicle.IsThrusterFiring) shakeAmp += 0.05f;
            if (speedMPS > 80f) shakeAmp += ((speedMPS - 80f) / 100f) * 0.03f;
            currentShake = Mathf.Lerp(currentShake, shakeAmp, dt * 10f);
            Vector3 shakeVec = UnityEngine.Random.insideUnitSphere * currentShake;

            switch (CurrentMode)
            {
                case CameraViewMode.Chase:
                    UpdateChaseCamera(target, dt, speedMPS, shakeVec);
                    break;

                case CameraViewMode.Cockpit:
                    UpdateCockpitCamera(target, dt, shakeVec);
                    break;

                case CameraViewMode.Hood:
                    UpdateHoodCamera(target, dt, shakeVec);
                    break;

                case CameraViewMode.Wheel:
                    UpdateWheelCamera(target, dt, shakeVec);
                    break;

                case CameraViewMode.Orbit:
                    UpdateOrbitCamera(target, dt);
                    break;
            }
        }

        private void UpdateChaseCamera(Transform target, float dt, float speedMPS, Vector3 shake)
        {
            // Dynamic distance and height adjustment with speed
            float dist = ChaseDistance + (speedMPS / 100f) * 1.5f;
            float height = ChaseHeight + (speedMPS / 100f) * 0.35f;

            // Target camera position behind car
            Vector3 targetCamPos = target.position - target.forward * dist + target.up * height;

            ManagedCamera.transform.position = Vector3.Lerp(ManagedCamera.transform.position, targetCamPos + shake, dt * DampingPos);

            // Look slightly ahead of car
            Vector3 lookTarget = target.position + target.forward * 2.5f + target.up * 0.7f;
            Quaternion targetRot = Quaternion.LookRotation(lookTarget - ManagedCamera.transform.position, target.up);
            ManagedCamera.transform.rotation = Quaternion.Slerp(ManagedCamera.transform.rotation, targetRot, dt * DampingRot);
        }

        private void UpdateCockpitCamera(Transform target, float dt, Vector3 shake)
        {
            Vector3 pos = target.TransformPoint(cockpitOffset) + shake * 0.5f;
            ManagedCamera.transform.position = pos;

            // Smooth forward view with subtle tilt
            Quaternion rot = Quaternion.LookRotation(target.forward, target.up);
            ManagedCamera.transform.rotation = Quaternion.Slerp(ManagedCamera.transform.rotation, rot, dt * 25f);
        }

        private void UpdateHoodCamera(Transform target, float dt, Vector3 shake)
        {
            Vector3 pos = target.TransformPoint(hoodOffset) + shake * 0.4f;
            ManagedCamera.transform.position = pos;

            Quaternion rot = Quaternion.LookRotation(target.forward, target.up);
            ManagedCamera.transform.rotation = rot;
        }

        private void UpdateWheelCamera(Transform target, float dt, Vector3 shake)
        {
            Vector3 pos = target.TransformPoint(wheelOffset) + shake * 0.3f;
            ManagedCamera.transform.position = pos;

            Vector3 lookAtPoint = target.TransformPoint(new Vector3(-0.6f, 0.34f, 1.4f));
            ManagedCamera.transform.rotation = Quaternion.LookRotation(lookAtPoint - pos, target.up);
        }

        private void UpdateOrbitCamera(Transform target, float dt)
        {
            Quaternion rot = Quaternion.Euler(orbitY, target.eulerAngles.y + orbitX, 0f);
            Vector3 pos = target.position + Vector3.up * 0.8f - (rot * Vector3.forward * orbitDist);

            ManagedCamera.transform.position = Vector3.Lerp(ManagedCamera.transform.position, pos, dt * 15f);
            ManagedCamera.transform.LookAt(target.position + Vector3.up * 0.8f);
        }
    }
}
