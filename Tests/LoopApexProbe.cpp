// Loop-apex adhesion probe: the car parked inverted at the top of a vertical loop, placed the way the in-engine
// RuntimePhysicsVerification places it (Vehicle::Place 3 cm off the surface), with an exact analytic loop surface.
// Prints the first steps in detail so start-up transients can be traced to their source.
#define main existing_diagnostic_main
#include "PhysicsHarness.cpp"
#undef main
#include <cmath>

// Interior of a cylinder with axis Y through (0, *, R): a vertical loop of radius R whose apex is at z = 2R.
// Sphere sweeps that start overlapping the surface report bStartPenetrating like the UWorld trace.
static TraceFn Loop(double R) {
    return [R](const V3& s, const V3& d, double len, double r) -> Hit {
        const V3 c = s - V3(0, 0, R);
        const double RR = R - r, rho = std::sqrt(c.x * c.x + c.z * c.z);
        if (rho > RR + 1e-9) {
            const V3 u = V3(c.x / rho, 0, c.z / rho);
            Hit h; h.bHit = true; h.bStartPenetrating = true; h.PenetrationDepth = rho - RR;
            h.Normal = -u; h.Point = V3(u.x * R, s.y, R + u.z * R); h.Dist = 0; h.CenterDist = 0;
            return h;
        }
        const double a = d.x * d.x + d.z * d.z, b = 2 * (c.x * d.x + c.z * d.z), cc = rho * rho - RR * RR;
        if (a < 1e-12) return Hit();
        const double disc = b * b - 4 * a * cc;
        if (disc < 0) return Hit();
        const double t = (-b + std::sqrt(disc)) / (2 * a);
        if (t < 0 || t > len) return Hit();
        const V3 center = s + d * t, n = V3(-center.x, 0, R - center.z).Norm();
        Hit h; h.bHit = true; h.Point = center - n * r; h.Normal = n; h.CenterDist = t; h.Dist = (h.Point - s).Dot(d);
        return h;
    };
}

static void Probe(const char* name, double R, double placeOffset, int detailSteps) {
    const TraceFn trace = Loop(R);
    Vehicle C; C.Init(Trim::Rocket);
    C.Set.Aero = AeroPreset::Custom; C.Set.Fan = FanMode::MaxGrip;
    C.Set.bRoadstarMode = false; C.Set.bHoverHold = false; C.Set.bFlightAssist = false; C.Set.bLandingAssist = false;
    C.Set.bInfiniteGas = false; C.Set.bInfiniteBattery = false; C.Set.bThrustersArmed = false;
    const V3 apex(0, 0, 2 * R), N(0, 0, -1);
    C.Place(apex + N * placeOffset, Quat::AxisAngle(V3(1, 0, 0), kPi));
    Inputs hold; hold.Brake = .2;
    std::printf("== %s  R=%.1f m  place offset=%.3f m  mass=%.0f kg\n", name, R, placeOffset, C.TotalMass());
    for (int i = 0; i < 4000; ++i) {
        C.Step(.001, hold, trace);
        const bool detail = i < detailSteps, tick = (i + 1) % 250 == 0;
        if (!detail && !tick) continue;
        const double vn = C.V.Dot(N), gap = (C.BodyOriginWorld() - apex).Dot(N);
        std::printf("t=%.3f vN=%+.4f gap=%+.4f up=%+.4f wheels=%d seal=%.3f fan=%7.0f body=%d |",
            (i + 1) * .001, vn, gap, C.Q.Up().Dot(N), C.T.WheelsInContact, C.T.FanSeal, C.T.FanDownforceN, C.T.bBodyContact);
        if (detail)
            for (const auto& w : C.Wheels) std::printf(" L=%.4f v=%+.3f F=%6.0f", w.Length, w.LengthVel, w.Load);
        std::printf("\n");
    }
}

int main() {
    Probe("flat ceiling (R huge)", 1e5, .03, 4);
    Probe("loop gauntlet apex", 20, .03, 8);
    Probe("loop apex, body origin on the curve", 20, .0, 4);
    return 0;
}
