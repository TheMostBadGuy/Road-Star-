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
	Vehicle v; v.Init(Trim::Rocket);
	std::printf("lift pos: FL(%.3f,%.3f) FR(%.3f,%.3f) RL(%.3f,%.3f) RR(%.3f,%.3f) cg.x %.3f\n", v.S.LiftNozzlePos[0].x, v.S.LiftNozzlePos[0].y, v.S.LiftNozzlePos[1].x, v.S.LiftNozzlePos[1].y,
		v.S.LiftNozzlePos[2].x, v.S.LiftNozzlePos[2].y, v.S.LiftNozzlePos[3].x, v.S.LiftNozzlePos[3].y, v.CGBody().x);
	v.Place(V3(0, 0, 8), Quat::AxisAngle(V3(1, 0, 0), kPi * 0.95));
	Inputs in;
	for (int i = 0; i < 4000; ++i)
	{
		v.Step(0.001, in, FlatTrace);
		if (i % 100 == 0)
		{
			const V3 wb = v.Q.Unrotate(v.W);
			std::printf("t=%.1f z=%.2f up.z=%.2f w=(%.2f,%.2f,%.2f) corner=(%.2f %.2f %.2f %.2f) land=%d contacts=%d tank=%.0f%%\n", i / 1000.0, v.BodyOriginWorld().z, v.Q.Up().z, wb.x, wb.y, wb.z,
				v.T.ThrustCorner[0], v.T.ThrustCorner[1], v.T.ThrustCorner[2], v.T.ThrustCorner[3], v.Landing.Intervening ? 1 : 0, v.T.WheelsInContact, v.T.TankPercent * 100);
		}
	}
}
