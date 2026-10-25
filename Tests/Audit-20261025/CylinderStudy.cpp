#define main prior_diagnostic_main
#include "../PhysicsHarness.cpp"
#undef main

static TraceFn Cylinder(double R) {
    return [R](const V3& s,const V3& d,double len,double r)->Hit {
        const V3 c=s-V3(0,0,R); const double RR=R-r;
        const double a=d.y*d.y+d.z*d.z,b=2*(c.y*d.y+c.z*d.z),cc=c.y*c.y+c.z*c.z-RR*RR;
        if(cc>1e-8 || a<1e-12)return Hit();
        const double disc=b*b-4*a*cc;if(disc<0)return Hit();
        const double t=(-b+std::sqrt(disc))/(2*a);if(t<0 || t>len)return Hit();
        const V3 center=s+d*t,n=V3(0,-center.y,R-center.z).Norm();
        Hit h;h.bHit=true;h.Point=center-n*r;h.Normal=n;h.CenterDist=t;h.Dist=(h.Point-s).Dot(d);return h;
    };
}
int main(){
    for(bool lowered:{false,true})for(double yaw:{0.,15.,25.,30.,35.,40.,45.}){
        Vehicle v;v.Init(Trim::Rocket);v.Set.Aero=AeroPreset::Custom;v.Set.Fan=FanMode::MaxGrip;v.Set.bLowSuspension=lowered;
        v.Place(V3(0,0,0),Quat::AxisAngle(V3(0,0,1),yaw*kPi/180));v.FanRPM=v.S.FanMaxRPM;v.SkirtDeploy=1;
        const auto tr=Cylinder(8);const double floorH=v.S.RideHeight-(lowered?v.S.LowRideDrop:0);
        const V3 offsets[5]={{0,0,0},{-1.35,-.78,0},{-1.35,.78,0},{1.35,-.78,0},{1.35,.78,0}};
        int valid=0;double mindist=1e9,minclear=1e9;
        for(auto o:offsets){const V3 p=v.BodyToWorld(V3(-.2,0,floorH+.25)+o);const auto h=tr(p,-v.Q.Up(),.8,0);if(h.bHit){++valid;mindist=std::min(mindist,h.Dist);minclear=std::min(minclear,h.Dist-.25);}}
        Inputs in;v.Step(.001,in,tr);
        std::printf("lowered%d yaw%.0f probes%d minDist%.4f clearance%.4f seal%.4f fan%.1f\n",lowered,yaw,valid,mindist,minclear,v.T.FanSeal,v.T.FanDownforceN);
    }
}
