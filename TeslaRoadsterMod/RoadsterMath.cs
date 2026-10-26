using System;
using UnityEngine;

namespace TeslaRoadsterMod
{
    // 3D Vector matching RoadsterPhysics.h V3
    public struct V3
    {
        public double x, y, z;

        public V3(double x, double y, double z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public static V3 operator +(V3 a, V3 b) => new V3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static V3 operator -(V3 a, V3 b) => new V3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static V3 operator -(V3 a) => new V3(-a.x, -a.y, -a.z);
        public static V3 operator *(V3 a, double s) => new V3(a.x * s, a.y * s, a.z * s);
        public static V3 operator /(V3 a, double s) => new V3(a.x / s, a.y / s, a.z / s);
        public static V3 Zero => new V3(0, 0, 0);

        public double Dot(V3 o) => x * o.x + y * o.y + z * o.z;
        public V3 Cross(V3 o) => new V3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
        public double Len() => Math.Sqrt(x * x + y * y + z * z);
        public double Len2() => x * x + y * y + z * z;

        public V3 Norm()
        {
            double l = Len();
            return l > 1e-12 ? this / l : new V3(0, 0, 0);
        }

        public V3 Mul(V3 o) => new V3(x * o.x, y * o.y, z * o.z);

        // Convert Blender (+X fwd, +Y left, +Z up) to Unity (+Z fwd, +X right, +Y up)
        public Vector3 ToUnityVec() => new Vector3((float)-y, (float)z, (float)x);
        public static V3 FromUnityVec(Vector3 v) => new V3(v.z, -v.x, v.y);
    }

    // Quaternion matching RoadsterPhysics.h Quat
    public struct Quat
    {
        public double w, x, y, z;

        public Quat(double w, double x, double y, double z)
        {
            this.w = w;
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public static Quat Identity => new Quat(1, 0, 0, 0);

        public static Quat AxisAngle(V3 axis, double angle)
        {
            V3 a = axis.Norm();
            double s = Math.Sin(angle * 0.5);
            return new Quat(Math.Cos(angle * 0.5), a.x * s, a.y * s, a.z * s);
        }

        public static Quat operator *(Quat a, Quat b)
        {
            return new Quat(
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
            );
        }

        public Quat Conj() => new Quat(w, -x, -y, -z);

        public Quat Normalized()
        {
            double l = Math.Sqrt(w * w + x * x + y * y + z * z);
            return l > 1e-12 ? new Quat(w / l, x / l, y / l, z / l) : Identity;
        }

        public V3 Rotate(V3 v)
        {
            V3 u = new V3(x, y, z);
            V3 t = u.Cross(v) * 2.0;
            return v + t * w + u.Cross(t);
        }

        public V3 Unrotate(V3 v) => Conj().Rotate(v);
        public V3 Fwd() => Rotate(new V3(1, 0, 0));
        public V3 Right() => Rotate(new V3(0, -1, 0)); // Blender +Y is LEFT, so Right is -Y
        public V3 Up() => Rotate(new V3(0, 0, 1));

        public Quaternion ToUnityQuat()
        {
            // Blender (+X fwd, +Y left, +Z up) to Unity (+Z fwd, +X right, +Y up)
            // w is real, vector part (-y, z, x)
            return new Quaternion((float)-y, (float)z, (float)x, (float)w);
        }

        public static Quat FromUnityQuat(Quaternion q)
        {
            return new Quat(q.w, q.z, -q.x, q.y);
        }
    }

    public static class RMath
    {
        public const double kG = 9.80665;
        public const double kPi = 3.14159265358979323846;
        public const double kMph = 0.44704; // m/s per mph
        public const double kPsiPerBar = 14.5038;
        public const double kPsiPerKPa = 0.145038;

        public static double Clamp(double v, double lo, double hi) => v < lo ? lo : (v > hi ? hi : v);
        public static float Clamp(float v, float lo, float hi) => v < lo ? lo : (v > hi ? hi : v);
        public static double Lerp(double a, double b, double t) => a + (b - a) * t;
        public static double Sign(double v) => v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0);

        public static double MoveTo(double cur, double target, double maxDelta)
        {
            if (Math.Abs(target - cur) <= maxDelta) return target;
            return cur + Sign(target - cur) * maxDelta;
        }
    }
}
