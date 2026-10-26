#pragma once
// Four +Z lift jets. SI units, body frame x forward/y right/z up.
// Geometric moment map (r x F): roll=sum(y[k]*f[k]), pitch=-sum(x[k]*f[k]).
// This is a linear actuator-allocation calculation, not a Tesla hardware model.
// It maximizes s in [0,1] subject to A*f=s*(roll,pitch), 0<=f[k]<=cap[k].
// At that scale it chooses the feasible mean thrust closest to the request.
// Assumption: each supplied cap is the current physically deliverable force;
// feed pressure, valve response and gas inventory remain the gas model's job.
#include <algorithm>
#include <cmath>
#include <limits>

namespace RoadsterLift
{
    namespace Detail
    {
        constexpr double Eps = 128.0 * std::numeric_limits<double>::epsilon();
        inline double Clamp(double x,double lo,double hi) { return std::max(lo,std::min(hi,x)); }
        struct Slice
        {
            bool Valid=false;
            double Minimum=std::numeric_limits<double>::infinity();
            double Maximum=-std::numeric_limits<double>::infinity();
            double Low[4]{},High[4]{};
            void Add(const double f[4])
            {
                const double sum=f[0]+f[1]+f[2]+f[3];
                if(!std::isfinite(sum))return;
                if(sum<Minimum){Minimum=sum;for(int k=0;k<4;++k)Low[k]=f[k];}
                if(sum>Maximum){Maximum=sum;for(int k=0;k<4;++k)High[k]=f[k];}
                Valid=true;
            }
            void Choose(double mean,double out[4]) const
            {
                const double target=Clamp(mean*4.0,Minimum,Maximum);
                const double blend=Maximum>Minimum?(target-Minimum)/(Maximum-Minimum):0.0;
                for(int k=0;k<4;++k)out[k]=Low[k]+blend*(High[k]-Low[k]);
            }
        };
        inline Slice TwoAxisSlice(double tx,double ty,const double a[4],const double b[4],const double cap[4],double tolerance)
        {
            Slice slice;
            for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)
            {
                const double det=a[i]*b[j]-a[j]*b[i];
                const double detScale=std::hypot(a[i],b[i])*std::hypot(a[j],b[j]);
                if(std::fabs(det)<=Eps*detScale)continue;
                int other[2],n=0;for(int k=0;k<4;++k)if(k!=i&&k!=j)other[n++]=k;
                for(int mask=0;mask<4;++mask)
                {
                    double f[4]{},rx=tx,ry=ty;
                    for(int k=0;k<2;++k){const int q=other[k];f[q]=(mask&(1<<k))?cap[q]:0.0;rx-=a[q]*f[q];ry-=b[q]*f[q];}
                    const double fi=(rx*b[j]-a[j]*ry)/det,fj=(a[i]*ry-rx*b[i])/det;
                    if(!std::isfinite(fi)||!std::isfinite(fj)||fi<-tolerance||fi>cap[i]+tolerance||fj<-tolerance||fj>cap[j]+tolerance)continue;
                    f[i]=Clamp(fi,0.0,cap[i]);f[j]=Clamp(fj,0.0,cap[j]);slice.Add(f);
                }
            }
            return slice;
        }
        inline Slice OneAxisSlice(double target,const double a[4],const double cap[4],double tolerance)
        {
            Slice slice;
            for(int i=0;i<4;++i)
            {
                if(a[i]==0)continue;
                int other[3],n=0;for(int k=0;k<4;++k)if(k!=i)other[n++]=k;
                for(int mask=0;mask<8;++mask)
                {
                    double f[4]{},r=target;
                    for(int k=0;k<3;++k){const int q=other[k];f[q]=(mask&(1<<k))?cap[q]:0.0;r-=a[q]*f[q];}
                    const double fi=r/a[i];
                    if(!std::isfinite(fi)||fi<-tolerance||fi>cap[i]+tolerance)continue;
                    f[i]=Clamp(fi,0.0,cap[i]);slice.Add(f);
                }
            }
            return slice;
        }
    }

    // x/y are nozzle positions ABOUT THE CG, in metres; force outputs are N.
    // Negative capacities are treated as zero. Any nonfinite input or numerical
    // overflow fails closed: return0 and zero force. Geometry may be rank0/1/2.
    // Results are maximum scale and nearest collective to floating-point precision.
    inline double Allocate(double tx,double ty,double requestedMeanN,const double x[4],const double y[4],const double capacityN[4],double outN[4])
    {
        for(int k=0;k<4;++k)outN[k]=0.0;
        if(!std::isfinite(tx)||!std::isfinite(ty)||!std::isfinite(requestedMeanN))return 0.0;
        double a[4],b[4],cap[4],capSum=0.0,maxCap=0.0,maxColumn=0.0;int axis=0;
        for(int k=0;k<4;++k)
        {
            if(!std::isfinite(x[k])||!std::isfinite(y[k])||!std::isfinite(capacityN[k]))return 0.0;
            a[k]=y[k];b[k]=-x[k];cap[k]=std::max(0.0,capacityN[k]);capSum+=cap[k];maxCap=std::max(maxCap,cap[k]);
            if(!std::isfinite(a[k]*cap[k])||!std::isfinite(b[k]*cap[k]))return 0.0;
            const double column=std::hypot(a[k],b[k]);if(column>maxColumn){maxColumn=column;axis=k;}
        }
        if(!std::isfinite(capSum)||!std::isfinite(maxColumn)||!std::isfinite(requestedMeanN*4.0))return 0.0;
        const double requestedNorm=std::hypot(tx,ty),tolerance=Detail::Eps*(1.0+maxCap);
        if(!std::isfinite(requestedNorm))return 0.0;
        if(maxColumn==0.0)
        {
            const double fraction=capSum>0.0?Detail::Clamp(requestedMeanN*4.0/capSum,0.0,1.0):0.0;
            for(int k=0;k<4;++k)outN[k]=cap[k]*fraction;
            return requestedNorm==0.0?1.0:0.0;
        }
        // Detect an actually one-dimensional moment map before solving 2x2 systems.
        double independent=0.0;
        for(int k=0;k<4;++k)independent=std::max(independent,std::fabs(a[axis]*b[k]-b[axis]*a[k]));
        double scale=1.0;
        Detail::Slice slice;
        if(independent<=Detail::Eps*maxColumn*maxColumn)
        {
            const double ux=a[axis]/maxColumn,uy=b[axis]/maxColumn;
            double scalar[4],lower=0.0,upper=0.0;
            for(int k=0;k<4;++k){scalar[k]=a[k]*ux+b[k]*uy;const double extent=scalar[k]*cap[k];lower+=std::min(0.0,extent);upper+=std::max(0.0,extent);}
            const double target=tx*ux+ty*uy,perpendicular=tx*uy-ty*ux;
            if(std::fabs(perpendicular)>Detail::Eps*requestedNorm)scale=0.0;
            else if(target>0.0)scale=Detail::Clamp(upper/target,0.0,1.0);
            else if(target<0.0)scale=Detail::Clamp(lower/target,0.0,1.0);
            slice=Detail::OneAxisSlice(scale*target,scalar,cap,tolerance);
        }
        else
        {
            if(requestedNorm>0.0)
            {
                // With s unconstrained, a maximum of the 5-variable/2-equality
                // LP has three nozzle bounds active. Enumerate those32 vertices.
                double maximum=0.0;
                const double directionX=tx/requestedNorm,directionY=ty/requestedNorm;
                for(int free=0;free<4;++free)
                {
                    const double det=directionX*b[free]-directionY*a[free];
                    if(std::fabs(det)<=Detail::Eps*std::hypot(a[free],b[free]))continue;
                    int other[3],n=0;for(int k=0;k<4;++k)if(k!=free)other[n++]=k;
                    for(int mask=0;mask<8;++mask)
                    {
                        double bx=0.0,by=0.0;
                        for(int k=0;k<3;++k){const int q=other[k];const double force=(mask&(1<<k))?cap[q]:0.0;bx+=a[q]*force;by+=b[q]*force;}
                        const double f=(bx*directionY-by*directionX)/det,s=(bx*b[free]-by*a[free])/det;
                        if(std::isfinite(f)&&std::isfinite(s)&&f>=-tolerance&&f<=cap[free]+tolerance&&s>maximum)maximum=s;
                    }
                }
                scale=maximum>=requestedNorm?1.0:Detail::Clamp(maximum/requestedNorm,0.0,1.0);
            }
            slice=Detail::TwoAxisSlice(scale*tx,scale*ty,a,b,cap,tolerance);
            // Retreat by a few ULPs only if a boundary rounded outward.
            for(int retry=0;!slice.Valid&&scale>0.0&&retry<4;++retry)
            {
                scale=std::nextafter(scale,0.0);
                slice=Detail::TwoAxisSlice(scale*tx,scale*ty,a,b,cap,tolerance);
            }
        }
        if(!slice.Valid)return 0.0;
        slice.Choose(requestedMeanN,outN);
        for(int k=0;k<4;++k)outN[k]=Detail::Clamp(outN[k],0.0,cap[k]);
        return scale;
    }
}

