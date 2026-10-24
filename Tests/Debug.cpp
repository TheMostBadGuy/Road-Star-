#include "../Unreal/RoadsterSim/Source/RoadsterSim/Public/RoadsterPhysics.h"
#include <cstdio>
using namespace Roadster;
static Hit FlatTrace(const V3& s, const V3& d, double len, double r)
{
	Hit h; const double ds = s.z; const double dn = d.z;
	if (ds < 0) return h;
	double t; if (ds <= r) t = 0; else if (dn < -1e-9) t = (ds - r) / -dn; else return h;
	if (t > len) return h;
	h.bHit = true; h.CenterDist = t; h.Point = s + d * t - V3(0, 0, r); h.Dist = (h.Point - s).Dot(d); h.Normal = V3(0, 0, 1);
	return h;
}
int main()
{
	Vehicle v; v.Init(Trim::Performance); v.Place(V3(), Quat());
	Inputs in; in.Brake = 0.2; for (int i = 0; i < 2000; ++i) v.Step(0.001, in, FlatTrace);
	for (auto& w : v.Wheels) { w.SurfaceTempC = 75; w.CarcassTempC = 55; }
	in.Brake = 1; in.Throttle = 1; for (int i = 0; i < 1500; ++i) v.Step(0.001, in, FlatTrace);
	in.Brake = 0;
	for (int i = 0; i < 9000; ++i)
	{
		v.Step(0.001, in, FlatTrace);
		if (i % 250 == 0)
		{
			const V3 vb = v.Q.Unrotate(v.V), wb = v.Q.Unrotate(v.W);
			std::printf("t=%.2f vx=%.1f vy=%.2f yaw=%.3f up=%.3f ax=%.2f ay=%.2f fan=%.0f wing=%.0f | RL k%.2f a%.2f tc%.2f T%.0f Fz%.0f | RR k%.2f a%.2f tc%.2f T%.0f Fz%.0f | FL a%.3f FR a%.3f probe %d\n",
				i / 1000.0, vb.x, vb.y, wb.z, v.Q.Up().z, v.T.AccelG.x, v.T.AccelG.y, v.T.FanDownforceN, v.T.WingDownforceN,
				v.Wheels[2].Kappa, v.Wheels[2].Alpha, v.Wheels[2].TCScale, v.Wheels[2].DriveTorque, v.Wheels[2].Load,
				v.Wheels[3].Kappa, v.Wheels[3].Alpha, v.Wheels[3].TCScale, v.Wheels[3].DriveTorque, v.Wheels[3].Load,
				v.Wheels[0].Alpha, v.Wheels[1].Alpha, v.T.bBodyContact);
		}
	}
}
