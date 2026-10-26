#pragma once
// Engine-independent spherical Earth environment, SI units, C++17.
// Frame: nonrotating Earth-centred Cartesian axes TRANSLATED to the launch site:
// launch surface is (0,0,0), Earth's centre is (0,0,-MeanRadiusM), local +Z up.
// Velocity is inertial in these fixed axes. No Earth rotation, atmospheric wind,
// oblateness, terrain, other gravitating bodies, or solar/geomagnetic weather.
// Altitude is radial distance minus radius, never simply position.Z.
//
// Gravity/orbital constants are explicit model assumptions requested for this
// simulation: R=6371000 m and mu=3.986004418e14 m^3/s^2. Gravity is a=-mu*r/|r|^3.
// Atmosphere separately follows the U.S. Standard Atmosphere 1976 conventions
// (effective geopotential radius 6356766 m; g0=9.80665 m/s^2).
// Primary source: NOAA/NASA/USAF, U.S. Standard Atmosphere, 1976,
// https://ntrs.nasa.gov/api/citations/19770009539/downloads/19770009539.pdf
// Lower atmosphere: equations 18,22,23,33; Table 8 correction at 80-86 km.
// Upper temperature: equations 25,27,29,31. Upper pressure/density: sparse
// Table I knots, printed pp.68-73 (PDF pages83-88), log-interpolated approximation.
// Above 1000 km pressure/density continue the last tabular log slopes, explicitly
// an unvalidated extrapolation, while temperature follows its 1000 K asymptote.
// This is an environment model, not a claim that a finite-gas car can reach orbit.

#include <cmath>
#include <limits>
#include <cstddef>

namespace Earth
{
    inline constexpr double MeanRadiusM = 6371000.0;
    inline constexpr double MuM3PerS2 = 3.986004418e14;
    inline constexpr double AtmosphereReferenceCeilingM = 1000000.0;

    struct Vec3
    {
        double X = 0, Y = 0, Z = 0;
        constexpr Vec3() = default;
        constexpr Vec3(double x, double y, double z) : X(x), Y(y), Z(z) {}
        constexpr Vec3 operator+(const Vec3& b) const { return {X+b.X,Y+b.Y,Z+b.Z}; }
        constexpr Vec3 operator-(const Vec3& b) const { return {X-b.X,Y-b.Y,Z-b.Z}; }
        constexpr Vec3 operator-() const { return {-X,-Y,-Z}; }
        constexpr Vec3 operator*(double s) const { return {X*s,Y*s,Z*s}; }
        constexpr Vec3 operator/(double s) const { return {X/s,Y/s,Z/s}; }
        Vec3& operator+=(const Vec3& b) { X+=b.X; Y+=b.Y; Z+=b.Z; return *this; }
        Vec3& operator-=(const Vec3& b) { X-=b.X; Y-=b.Y; Z-=b.Z; return *this; }
        Vec3& operator*=(double s) { X*=s; Y*=s; Z*=s; return *this; }
    };
    inline Vec3 operator*(double s, const Vec3& a) { return a*s; }
    inline double Dot(const Vec3& a, const Vec3& b) { return a.X*b.X+a.Y*b.Y+a.Z*b.Z; }
    inline Vec3 Cross(const Vec3& a, const Vec3& b)
    { return {a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X}; }
    inline double Length(const Vec3& a) { return std::sqrt(Dot(a,a)); }
    inline Vec3 EarthCenteredPosition(const Vec3& p) { return {p.X,p.Y,p.Z+MeanRadiusM}; }
    inline double Altitude(const Vec3& p) { return Length(EarthCenteredPosition(p))-MeanRadiusM; }

    struct AtmosphereSample
    {
        double AltitudeM = 0;
        double TemperatureK = 0;
        double PressurePa = 0;
        double DensityKgM3 = 0;
    };

    namespace Detail
    {
        inline constexpr double StandardRadiusM = 6356766.0;
        inline constexpr double StandardGravityMps2 = 9.80665;
        inline constexpr double SpecificGasConstant = 8314.32 / 28.9644;

        inline AtmosphereSample LowerAtmosphere(double altitude)
        {
            const double z = altitude > 0 ? altitude : 0;
            const double h = StandardRadiusM*z/(StandardRadiusM+z);
            constexpr double baseH[] = {0,11000,20000,32000,47000,51000,71000};
            constexpr double lapse[] = {-0.0065,0,0.001,0.0028,0,-0.0028,-0.002};
            double temperature = 288.15, pressure = 101325.0;
            for (int i=0; i<7; ++i)
            {
                const double top = i<6 && h>baseH[i+1] ? baseH[i+1] : h;
                const double dh = top-baseH[i];
                const double nextT = temperature+lapse[i]*dh;
                if (lapse[i]==0)
                    pressure *= std::exp(-StandardGravityMps2*dh/(SpecificGasConstant*temperature));
                else
                    pressure *= std::pow(temperature/nextT,StandardGravityMps2/(SpecificGasConstant*lapse[i]));
                temperature = nextT;
                if (i==6 || h<=baseH[i+1]) break;
            }
            // Hydrostatic integration uses molecular-scale T. Table 8 corrects
            // the returned kinetic T; density still uses molecular-scale T.
            double massRatio = 1;
            if (z>80000)
            {
                constexpr double ratios[] = {1.000000,0.999996,0.999989,0.999971,
                    0.999941,0.999909,0.999870,0.999829,0.999786,0.999741,
                    0.999694,0.999641,0.9995788};
                const double index = (z-80000)/500;
                const int i = index<12 ? static_cast<int>(index) : 11;
                const double f = index-i;
                massRatio = ratios[i]+f*(ratios[i+1]-ratios[i]);
            }
            return {altitude,temperature*massRatio,pressure,pressure/(SpecificGasConstant*temperature)};
        }

        inline double UpperTemperature(double altitude)
        {
            const double z = altitude/1000.0; // published equations use km
            if (z<=91) return 186.8673;
            if (z<110)
            {
                const double q = (z-91)/19.9429;
                return 263.1905-76.3232*std::sqrt(1-q*q);
            }
            if (z<120) return 240.0+12.0*(z-110);
            const double xi = (z-120)*((StandardRadiusM/1000)+120)/((StandardRadiusM/1000)+z);
            return 1000.0-640.0*std::exp(-0.01875*xi);
        }

        struct UpperNode { double Z, PressurePa, DensityKgM3; };
        // Visually checked against the original scanned table. The pressure
        // column is in millibars in the PDF; every value here is converted to Pa.
        inline constexpr UpperNode UpperNodes[] = {
            {86000, .37338,6.958e-6}, {90000,.18359,3.416e-6},
            {95000,.075966,1.393e-6}, {100000,.032011,5.604e-7},
            {105000,.014477,2.325e-7}, {110000,.0071042,9.708e-8},
            {115000,.0040096,4.281e-8}, {120000,.0025382,2.222e-8},
            {125000,.0017354,1.291e-8}, {130000,.0012505,8.152e-9},
            {135000,.00093568,5.465e-9}, {140000,.00072028,3.831e-9},
            {145000,.00056691,2.781e-9}, {150000,.00045422,2.076e-9},
            {160000,.00030395,1.233e-9}, {170000,.00021210,7.815e-10},
            {180000,.00015271,5.194e-10}, {190000,.00011266,3.581e-10},
            {200000,.000084736,2.541e-10}, {250000,.000024767,6.073e-11},
            {300000,.0000087704,1.916e-11}, {350000,.0000034498,7.014e-12},
            {400000,.0000014518,2.803e-12}, {450000,.00000064468,1.184e-12},
            {500000,.00000030236,5.215e-13}, {550000,.00000015137,2.384e-13},
            {600000,.000000082130,1.137e-13}, {650000,.000000048865,5.712e-14},
            {700000,.000000031908,3.070e-14}, {750000,.000000022598,1.788e-14},
            {800000,.000000017036,1.136e-14}, {850000,.000000013415,7.824e-15},
            {900000,.000000010873,5.759e-15}, {950000,.0000000089816,4.453e-15},
            {1000000,.0000000075138,3.561e-15}
        };
    }

    inline AtmosphereSample SampleAltitude(double geometricAltitudeM)
    {
        // Below the surface retain actual AltitudeM but clamp atmospheric
        // thermodynamics to sea level. Solid Earth/contact is the caller's job.
        if (!std::isfinite(geometricAltitudeM)) return {geometricAltitudeM,0,0,0};
        if (geometricAltitudeM<=86000) return Detail::LowerAtmosphere(geometricAltitudeM);
        constexpr std::size_t n = sizeof(Detail::UpperNodes)/sizeof(Detail::UpperNodes[0]);
        std::size_t i=0;
        while (i+2<n && geometricAltitudeM>Detail::UpperNodes[i+1].Z) ++i;
        Detail::UpperNode a = Detail::UpperNodes[i];
        const Detail::UpperNode& b = Detail::UpperNodes[i+1];
        if (i==0)
        {
            // Bridge the rounded table's 86 km anchor to the exact lower layer
            // limit; prevents a tiny artificial upward pressure/density step.
            const auto lower = Detail::LowerAtmosphere(86000);
            a.PressurePa=lower.PressurePa; a.DensityKgM3=lower.DensityKgM3;
        }
        const double f = (geometricAltitudeM-a.Z)/(b.Z-a.Z);
        const double p = std::exp(std::log(a.PressurePa)+f*std::log(b.PressurePa/a.PressurePa));
        const double rho = std::exp(std::log(a.DensityKgM3)+f*std::log(b.DensityKgM3/a.DensityKgM3));
        return {geometricAltitudeM,Detail::UpperTemperature(geometricAltitudeM),p,rho};
    }
    inline AtmosphereSample Sample(const Vec3& position) { return SampleAltitude(Altitude(position)); }

    inline Vec3 Gravity(const Vec3& position)
    {
        const Vec3 r = EarthCenteredPosition(position);
        const double r2 = Dot(r,r);
        // A point-mass field is singular at its centre. Invalid input returns
        // zero, and OrbitalElements marks the same input invalid.
        if (!(r2>0) || !std::isfinite(r2)) return {};
        return r*(-MuM3PerS2/(r2*std::sqrt(r2)));
    }

    struct OrbitSample
    {
        bool Valid=false, Bound=false, Impacting=false;
        double RadiusM=0, AltitudeM=0;
        double SpecificEnergyJPerKg=0, SpecificAngularMomentumM2PerS=0;
        double Eccentricity=0, PeriapsisAltitudeM=0;
        double ApoapsisAltitudeM=std::numeric_limits<double>::infinity();
        double CircularSpeedMps=0, EscapeSpeedMps=0;
    };

    inline OrbitSample OrbitalElements(const Vec3& position, const Vec3& velocity)
    {
        OrbitSample out;
        const Vec3 r = EarthCenteredPosition(position);
        const double radius=Length(r), speed2=Dot(velocity,velocity);
        if (!(radius>0) || !std::isfinite(radius) || !std::isfinite(speed2)) return out;
        const Vec3 h=Cross(r,velocity);
        const Vec3 e=Cross(velocity,h)/MuM3PerS2-r/radius;
        out.Valid=true;
        out.RadiusM=radius;
        out.AltitudeM=radius-MeanRadiusM;
        out.SpecificEnergyJPerKg=speed2/2-MuM3PerS2/radius;
        out.SpecificAngularMomentumM2PerS=Length(h);
        out.Eccentricity=Length(e);
        out.Bound=out.SpecificEnergyJPerKg<0;
        const double semilatus=Dot(h,h)/MuM3PerS2;
        const double periapsis=semilatus/(1+out.Eccentricity);
        out.PeriapsisAltitudeM=periapsis-MeanRadiusM;
        if (out.Bound)
        {
            const double a=-MuM3PerS2/(2*out.SpecificEnergyJPerKg);
            out.ApoapsisAltitudeM=a*(1+out.Eccentricity)-MeanRadiusM;
        }
        out.Impacting=periapsis<=MeanRadiusM;
        out.CircularSpeedMps=std::sqrt(MuM3PerS2/radius);
        out.EscapeSpeedMps=std::sqrt(2*MuM3PerS2/radius);
        return out;
    }
}
