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
	for (int steerCase = 0; steerCase < 2; ++steerCase)
	{
		Vehicle v; v.Init(Trim::Rocket); v.Place(V3(), Quat());
		Inputs in; for (int i = 0; i < 1000; ++i) v.Step(0.001, in, FlatTrace);
		v.Place(v.BodyOriginWorld(), v.Q, 74.0);
		in.Brake = 1.0; in.Steer = steerCase ? 0.05 : 0.0;
		for (int i = 0; i <= 5000; ++i)
		{
			v.Step(0.001, in, FlatTrace);
			if (i % 250 == 0)
			{
				const V3 vb = v.Q.Unrotate(v.V), wb = v.Q.Unrotate(v.W);
				std::printf("steer=%.2f t=%.2f vx=%.1f vy=%.2f yaw=%.3f ax=%.2f fan=%.0f wing=%.0f F:%.0f/%.0f R:%.0f/%.0f absR=%.2f kR=%.2f aR=%.3f aF=%.3f\n", in.Steer, i / 1000.0, vb.x, vb.y, wb.z, v.T.AccelG.x, v.T.FanDownforceN, v.T.WingDownforceN,
					v.Wheels[0].Load, v.Wheels[1].Load, v.Wheels[2].Load, v.Wheels[3].Load, v.Wheels[2].ABSScale, v.Wheels[2].Kappa, v.Wheels[2].Alpha, v.Wheels[0].Alpha);
			}
			if (v.V.Len() < 0.5) break;
		}
	}
}
