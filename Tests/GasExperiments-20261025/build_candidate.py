"""Generate a reviewable candidate; never writes the shared live kernel."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
old = (ROOT / 'ColdGasSystem.before.h').read_text()

def terms(gas):
    eos = json.loads((ROOT / f'{gas}.source.json').read_text())['EOS'][0]
    rows = []
    for group in eos['alphar']:
        for i, n in enumerate(group['n']):
            values = [n, group['t'][i], group['d'][i]]
            values += [group.get(key, [0]*len(group['n']))[i] for key in ['l','eta','beta','gamma','epsilon']]
            rows.append('\t\t\t\t{ ' + ', '.join(format(x, '.17g') for x in values) + ' },')
    return '\n'.join(rows)

model = r'''
    // Species are per System, never process-global. Pure gases only: mixtures require mixture EOS and phase handling.
    enum class GasSpecies : uint8_t { Nitrogen = 0, Helium = 1, Argon = 2 };
    inline bool IsValidGasSpecies(GasSpecies s) { return s == GasSpecies::Nitrogen || s == GasSpecies::Helium || s == GasSpecies::Argon; }
    struct GasProperties
    {
        const char* Name;
        double MolarMassKgMol, GasConstantJkgK, CriticalK, CriticalPa, CriticalDensityKgM3, TripleK, Acentric;
        double IdealCpJkgK, IdealGamma;
    };
    inline const GasProperties& Properties(GasSpecies species)
    {
        // Preserve each reference EOS's fitted R, rather than substituting a modern R into fitted coefficients.
        static const GasProperties n{ "Nitrogen", kMolarMass, kR, kTc, kPc, kRhoc, kTtriple, kOmega, 3.5*kR, 1.4 };
        static const GasProperties h{ "Helium", .004002602, 8.314472/.004002602, 5.1953, .22832e6, 17.3837e3*.004002602, 2.1768, -.38354, 2.5*8.314472/.004002602, 5.0/3.0 };
        static const GasProperties a{ "Argon", .039948, 8.31451/.039948, 150.687, 4.863e6, 535.6, 83.806, -.00219, 2.5*8.31451/.039948, 5.0/3.0 };
        return species == GasSpecies::Helium ? h : species == GasSpecies::Argon ? a : n;
    }

    // Reference Helmholtz EOS for the added monatomic gases; nitrogen deliberately retains its existing PR path.
    // He: Ortiz Vega et al., NIST IR 8474 (2023), Tables 1/2, doi:10.6028/NIST.IR.8474.
    // Ar: Tegeler, Span & Wagner, JPCRD 28 (1999) 779, doi:10.1063/1.556037.
    // Coefficient transcription cross-checked against the official CoolProp fluid JSON snapshots in the test folder.
    // All p,u,h,s,cv,cp,w,JT derivatives below come from ONE Helmholtz potential, not independent lookup fits.
    class Thermodynamics
    {
    public:
        explicit Thermodynamics(GasSpecies species = GasSpecies::Nitrogen) : Species(species) {}
        const GasProperties& Data() const { return Properties(Species); }
        GasSpecies Species;
        double R() const { return Data().GasConstantJkgK; }
        double MinVolume() const { return Species == GasSpecies::Nitrogen ? Eos::kVMin : 1e-8; }
        double CvIdeal(double T) const { return Species == GasSpecies::Nitrogen ? Eos::CvIdeal(T) : 1.5*R(); }
        double Floor(double p) const
        {
            if (Species == GasSpecies::Helium) return 20.0; // No helium cryogenic liquid/superfluid model in this simulator.
            return p < .999*Data().CriticalPa ? TSat(p)+1.0 : Data().CriticalK+1.0;
        }
        Eos::Props Evaluate(double T, double v) const
        {
            if (Species == GasSpecies::Nitrogen) return Eos::Evaluate(T, v);
            const double tau = Data().CriticalK/T, delta = 1.0/(v*Data().CriticalDensityKgM3), r = R();
            const Derivs f = Residual(tau, delta);
            // Absolute reference offsets do not affect energy conservation or thrust; use the published He reference.
            const double a1 = Species == GasSpecies::Helium ? .1733487932835764 : 8.31666243;
            const double a2 = Species == GasSpecies::Helium ? .4674522201550815 : -4.94651164;
            const double a0 = a1+a2*tau+std::log(delta)+1.5*std::log(tau);
            const double at = a2+1.5/tau+f.t;
            Eos::Props o{}; o.T=T; o.v=v; o.rho=1.0/v;
            o.p = o.rho*r*T*(1.0+delta*f.d);
            o.u = r*T*tau*at; o.h=o.u+o.p*v; o.s=r*(tau*at-a0-f.a);
            o.cv = r*(1.5-tau*tau*f.tt);
            const double den = 1.0+2.0*delta*f.d+delta*delta*f.dd;
            const double num = 1.0+delta*f.d-delta*tau*f.dt;
            o.dpdT = o.rho*r*num; o.dpdv = -o.rho*o.rho*r*T*den;
            const double dvdT = -o.dpdT/std::min(o.dpdv,-1e-30);
            o.cp=o.cv+T*o.dpdT*dvdT;
            o.w=std::sqrt(std::max(1.0,-v*v*o.dpdv*o.cp/o.cv));
            o.muJT=(T*dvdT-v)/o.cp; o.beta=dvdT/v;
            return o;
        }
        double Pressure(double T, double v) const { return Species == GasSpecies::Nitrogen ? Eos::Pressure(T,v) : Evaluate(T,v).p; }
        double U(double T, double v) const { return Species == GasSpecies::Nitrogen ? Eos::U(T,v) : Evaluate(T,v).u; }
        double S(double T, double v) const { return Species == GasSpecies::Nitrogen ? Eos::S(T,v) : Evaluate(T,v).s; }
        double Cv(double T, double v) const { return Species == GasSpecies::Nitrogen ? Eos::Cv(T,v) : Evaluate(T,v).cv; }
        double DPDv(double T, double v) const { return Species == GasSpecies::Nitrogen ? Eos::DPDv(T,v) : Evaluate(T,v).dpdv; }
        double VFromPT(double p, double T, bool liquid=false) const
        {
            if (Species == GasSpecies::Nitrogen) return Eos::VFromPT(p,T,liquid);
            // Bracket the correct phase before safeguarded Newton in density. Config fills are supercritical (200-350 K).
            double lo=1e-12, hi=std::max(2.0*p/(R()*T),Data().CriticalDensityKgM3*2.0);
            if (Species == GasSpecies::Argon && T < Data().CriticalK)
            {
                if (!liquid && p <= PSat(T)) hi=RhoVapSat(T)*1.001;
                else { lo=RhoLiqSat(T)*.999; hi=std::max(hi,lo*1.5); }
            }
            for (int i=0;i<25 && Pressure(T,1.0/hi)<p;++i) hi*=1.5;
            double rho=Clampd(p/(R()*T),lo,hi);
            for(int i=0;i<70;++i)
            {
                const Eos::Props q=Evaluate(T,1.0/rho);
                if (std::fabs(q.p-p)<1e-11*std::max(p,1.0)) break;
                (q.p>p ? hi : lo)=rho;
                const double slope=-q.dpdv/(rho*rho), next=rho+(p-q.p)/slope;
                rho=std::isfinite(next)&&slope>0&&next>lo&&next<hi ? next : .5*(lo+hi);
            }
            return 1.0/rho;
        }
        Eos::Props EvaluatePT(double p,double T) const { return Evaluate(T,VFromPT(p,T)); }
        double TFromUV(double u,double v,double guess) const
        {
            if (Species == GasSpecies::Nitrogen) return Eos::TFromUV(u,v,guess);
            double T=Clampd(guess,5.0,3000.0);
            for(int i=0;i<50;++i)
            {
                const auto q=Evaluate(T,v); const double dT=(u-q.u)/std::max(q.cv,.05*R());
                T=Clampd(T+Clampd(dT,-.3*T,.3*T),2.0,5000.0);
                if(std::fabs(dT)<1e-10*T) break;
            }
            return T;
        }
        double PSat(double T) const
        {
            if(Species==GasSpecies::Nitrogen) return Eos::PSat(T);
            if(Species==GasSpecies::Helium) return Data().CriticalPa; // outside simulated helium liquid range
            return Ancillary(T,0);
        }
        double RhoLiqSat(double T) const { return Species==GasSpecies::Nitrogen ? Eos::RhoLiqSat(T) : Ancillary(T,1); }
        double RhoVapSat(double T) const { return Species==GasSpecies::Nitrogen ? Eos::RhoVapSat(T) : Ancillary(T,2); }
        double TSat(double p) const
        {
            if(Species==GasSpecies::Nitrogen) return Eos::TSat(p);
            double lo=Data().TripleK,hi=Data().CriticalK;
            for(int i=0;i<45;++i) { const double m=.5*(lo+hi); (PSat(m)<p ? lo:hi)=m; }
            return .5*(lo+hi);
        }
        double PSatExtended(double T) const
        {
            if(Species==GasSpecies::Nitrogen) return Eos::PSatExtended(T);
            if(T>=Data().TripleK) return PSat(T);
            // Ar sublimation reference only for a qualitative supersaturation indicator; no solid particles simulated.
            return PSat(Data().TripleK)*std::exp(-189000.0/R()*(1.0/std::max(T,5.0)-1.0/Data().TripleK));
        }
        double Viscosity(double T,double rho) const
        {
            if(Species==GasSpecies::Nitrogen) return Eos::Viscosity(T,rho);
            return (Species==GasSpecies::Helium ? 19.9e-6 : 22.8e-6)*std::pow(std::max(T,20.0)/300.0,.70);
        }
        double Conductivity(double T,double rho) const
        {
            if(Species==GasSpecies::Nitrogen) return Eos::Conductivity(T,rho);
            // Dilute-gas transport approximation (NIST semiconductor-gas tables at 300 K); dense corrections omitted.
            return (Species==GasSpecies::Helium ? .1559 : .0178)*std::pow(std::max(T,20.0)/300.0,.70);
        }
    private:
        struct Term { double n,t,d,l,eta,beta,gamma,epsilon; };
        struct Derivs { double a=0,d=0,t=0,dd=0,tt=0,dt=0; };
        Derivs Residual(double tau,double delta) const
        {
            static constexpr Term helium[] = {
HELIUM_TERMS
            };
            static constexpr Term argon[] = {
ARGON_TERMS
            };
            const Term* terms=Species==GasSpecies::Helium ? helium:argon;
            const int count=Species==GasSpecies::Helium ? 23:41;
            Derivs f;
            for(int i=0;i<count;++i)
            {
                const Term& q=terms[i]; const double dl=q.l>0 ? std::pow(delta,q.l):0.0;
                const double val=q.n*std::pow(delta,q.d)*std::pow(tau,q.t)*std::exp(-dl-q.eta*(delta-q.epsilon)*(delta-q.epsilon)-q.beta*(tau-q.gamma)*(tau-q.gamma));
                const double ld=q.d/delta-(q.l>0 ? q.l*dl/delta:0.0)-2*q.eta*(delta-q.epsilon);
                const double lt=q.t/tau-2*q.beta*(tau-q.gamma);
                const double ldd=-q.d/(delta*delta)-(q.l>0 ? q.l*(q.l-1)*dl/(delta*delta):0.0)-2*q.eta;
                const double ltt=-q.t/(tau*tau)-2*q.beta;
                f.a+=val; f.d+=val*ld; f.t+=val*lt; f.dd+=val*(ld*ld+ldd); f.tt+=val*(lt*lt+ltt); f.dt+=val*ld*lt;
            }
            return f;
        }
        double Ancillary(double T,int which) const
        {
            // CoolProp EOS-derived ancillary fit; max error 0.008% p, 0.235% liquid / 0.213% vapor density.
            static constexpr double n[3][6] = {
                {1.274555697445016,-6.537350082824229,2.3992572216415304,-2.853795528373302,-1.4260480922698553,-1.2865724148178292},
                {.35549443267848707,1.7740534055789645,1.6692402958603285,-1326.972748274678,3248.4827767066654,-4246.807908907579},
                {1.1883246645329462,-3.0995804610072724,-2.5097036910932267,.6796303921537137,-21.203752823276112,418.9717120034111} };
            static constexpr double e[3][6] = { {.841,.944,2.449,2.797,6.335,10.499}, {.272,.417,2.551,7.589,8.392,10.436}, {.29,.329,.839,2.946,6.496,11.57} };
            T=Clampd(T,Data().TripleK,Data().CriticalK); const double theta=1-T/Data().CriticalK;
            double sum=0;for(int i=0;i<6;++i)sum+=n[which][i]*std::pow(theta,e[which][i]);
            return which==0 ? Data().CriticalPa*std::exp(Data().CriticalK/T*sum) : which==1 ? Data().CriticalDensityKgM3*(1+sum) : Data().CriticalDensityKgM3*std::exp(Data().CriticalK/T*sum);
        }
    };
'''.replace('HELIUM_TERMS',terms('Helium')).replace('ARGON_TERMS',terms('Argon'))

marker='\t// Lowest stagnation temperature'
candidate=old.replace(marker,model+'\n'+marker)

# Limit adaptive replacements to the two consumers, never mutate the nitrogen compatibility namespace.
idx=candidate.index('\tclass IsentropeTable')
pre,tail=candidate[:idx],candidate[idx:]
for name in ['VFromPT','EvaluatePT','Evaluate','CvIdeal','Cv','S','U','DPDv','Pressure','TFromUV','RhoVapSat','RhoLiqSat','PSatExtended','PSat','Conductivity','Viscosity']:
    tail=tail.replace('Eos::'+name+'(', 'Thermo.'+name+'(')
tail=tail.replace('Eos::kVMin','Thermo.MinVolume()')
tail=tail.replace('TFloor(', 'Thermo.Floor(')
# These scoped constants belong to the selected gas. Nitrogen R remains exactly the old value.
import re
for name,new in [('kR','Thermo.R()'),('kTc','Thermo.Data().CriticalK'),('kRhoc','Thermo.Data().CriticalDensityKgM3'),('kTtriple','Thermo.Data().TripleK')]:
    tail=re.sub(r'\b'+name+r'\b',new,tail)

tail=tail.replace('static const IsentropeTable& Get() { static const IsentropeTable t; return t; }', '''static const IsentropeTable& Get(GasSpecies species = GasSpecies::Nitrogen)
        {
            // Lazily build independent immutable tables; no species contamination across systems or threads.
            if(species==GasSpecies::Helium) { static const IsentropeTable h(GasSpecies::Helium); return h; }
            if(species==GasSpecies::Argon) { static const IsentropeTable a(GasSpecies::Argon); return a; }
            static const IsentropeTable n(GasSpecies::Nitrogen); return n;
        }''')
tail=tail.replace('IsentropeTable()\n\t\t{', 'explicit IsentropeTable(GasSpecies species) : Thermo(species), Tmin(species==GasSpecies::Helium ? 20.0:kTmin), Tmax(species==GasSpecies::Helium ? 500.0:kTmax)\n\t\t{')
tail=tail.replace('double LnPmin = 0, DLnP = 0, DT = 0;', 'Thermodynamics Thermo;\n        double Tmin=70, Tmax=380;\n\t\tdouble LnPmin = 0, DLnP = 0, DT = 0;')
# Preserve public kTmin legacy value but use a lower table grid for helium blowdown.
table_end=tail.index('\tenum class ValveMode')
table,rest=tail[:table_end],tail[table_end:]
table=table.replace('kTmin, kTmax','Tmin, Tmax').replace('(T0 - kTmin)','(T0 - Tmin)').replace('(kTmax - kTmin)','(Tmax - Tmin)').replace('std::max(kTmin +','std::max(Tmin +')
tail=table+rest
tail=tail.replace('\tstruct Config\n\t{','\tstruct Config\n\t{\n        GasSpecies Propellant = GasSpecies::Nitrogen;')
tail=tail.replace('explicit System(const Config& cfg = Config::Thrust()) : C(cfg)','explicit System(const Config& cfg = Config::Thrust()) : C(cfg), Thermo(IsValidGasSpecies(cfg.Propellant) ? cfg.Propellant : GasSpecies::Nitrogen)')
tail=tail.replace('(void)IsentropeTable::Get();','C.Propellant=Thermo.Species;\n            (void)IsentropeTable::Get(C.Propellant);')
tail=tail.replace('const IsentropeTable& tab = IsentropeTable::Get();','const IsentropeTable& tab = IsentropeTable::Get(C.Propellant);')
tail=tail.replace('\t\tConfig C;','\t\tConfig C;\n        Thermodynamics Thermo;')
tail=tail.replace('static void Solve(Gas& g)','void Solve(Gas& g) const')
tail=tail.replace('if (T1 < Thermo.Data().CriticalK &&','if (C.Propellant != GasSpecies::Helium && T1 < Thermo.Data().CriticalK &&')

# Monatomic nozzle subsonic/choking formulas must use 5/3, not nitrogen's hard-coded 1.4.
tail=tail.replace('const double eps = n.AreaRatio();','const double eps = n.AreaRatio(), gamma=Thermo.Data().IdealGamma;')
tail=tail.replace('const double ar = (1.0 / M) * std::pow((2.0 / 2.4) * (1.0 + 0.2 * M * M), 3.0);','const double ar = C.Propellant==GasSpecies::Nitrogen ? (1.0 / M) * std::pow((2.0 / 2.4) * (1.0 + 0.2 * M * M), 3.0) : (1.0 / M) * std::pow((2.0 / (gamma+1.0)) * (1.0 + .5*(gamma-1.0) * M * M), (gamma+1.0)/(2.0*(gamma-1.0)));')
tail=tail.replace('std::pow(1.0 + 0.2 * Ms * Ms, -3.5)','(C.Propellant==GasSpecies::Nitrogen ? std::pow(1.0 + 0.2 * Ms * Ms, -3.5) : std::pow(1.0 + .5*(gamma-1.0) * Ms * Ms, -gamma/(gamma-1.0)))')
tail=tail.replace('const double xT = nz.Valve.xT;','const double xT = nz.Valve.xT;\n            const double gamma=Thermo.Data().IdealGamma, exponent=C.Propellant==GasSpecies::Nitrogen ? 2.0/7.0:(gamma-1.0)/gamma, twiceCp=C.Propellant==GasSpecies::Nitrogen ? 7.0*Thermo.R():2.0*gamma/(gamma-1.0)*Thermo.R();')
tail=tail.replace('std::pow(pa / pc, 2.0 / 7.0)','std::pow(pa / pc, exponent)')
tail=tail.replace('7.0 * Thermo.R() *','twiceCp *')
tail=tail.replace('1.4 * Thermo.R() *','gamma * Thermo.R() *')
tail=tail.replace('o.Supersaturation = Te < Thermo.Data().CriticalK ?', 'o.Supersaturation = C.Propellant != GasSpecies::Helium && Te < Thermo.Data().CriticalK ?')
# IEC 60534 F_gamma scales the tested xT for gases whose ideal cp/cv differs from nitrogen/air.
idx_system=tail.index('\tclass System')
table_and_config,system=tail[:idx_system],tail[idx_system:]
system=system.replace('Orifice(', 'Restriction(')
restriction='''
        double Restriction(double area,double p1,double rho1,double p2,double xT) const
        {
            const double limit=C.Propellant==GasSpecies::Nitrogen ? xT : std::min(.99,xT*Thermo.Data().IdealGamma/1.4);
            return Orifice(area,p1,rho1,p2,limit);
        }
'''
system=system.replace('\t\tstatic int Clampi(',restriction+'\n\t\tstatic int Clampi(')
tail=table_and_config+system

api=r'''
        // A pit experiment resets burst state and refills every installed vessel at the common (p,T).
        // Hardware rating is independent of gas species; temperature is bounded to the tested fill range.
        bool ConfigureExperiment(GasSpecies species, double pressureBar, double temperatureK = kAmbientK)
        {
            if(!IsValidGasSpecies(species) || !std::isfinite(pressureBar) || !std::isfinite(temperatureK)
                || pressureBar < kPatm/kBarPa || pressureBar > MaxFillPressureBar() || temperatureK < 200.0 || temperatureK > 350.0) return false;
            Config cfg=C;cfg.Propellant=species; System fresh(cfg); fresh.SetArmed(bArmed);
            fresh.Fill(pressureBar,temperatureK); *this=fresh; return true;
        }
        // Tank amount is an alternate input: volume and T determine density, and EOS determines pressure.
        // No mass is added while retaining an incompatible pressure. Total excludes the separate feed manifold.
        bool FillByMassKg(double tankMassKg, double temperatureK = kAmbientK)
        {
            if(!std::isfinite(tankMassKg) || tankMassKg<=0 || !std::isfinite(temperatureK) || temperatureK<200 || temperatureK>350) return false;
            double volume=0;for(int t=0;t<NumTanks;++t)volume+=C.Tank[t].VolumeL*1e-3;
            const double pressure=Thermo.Pressure(temperatureK,volume/tankMassKg)/kBarPa;
            if(!std::isfinite(pressure) || pressure<kPatm/kBarPa-1e-8 || pressure>MaxFillPressureBar()+1e-8) return false;
            return ConfigureExperiment(C.Propellant,Clampd(pressure,kPatm/kBarPa,MaxFillPressureBar()),temperatureK);
        }
        GasSpecies Propellant() const { return C.Propellant; }
        const GasProperties& PropellantProperties() const { return Thermo.Data(); }
        double MaxFillPressureBar() const
        {
            double rating=690.0;for(int t=0;t<NumTanks;++t)rating=std::min(rating,C.Tank[t].MEOPBar);return rating;
        }
'''
tail=tail.replace('\t\tvoid ResetCounters()',api+'\n\t\tvoid ResetCounters()')
candidate=pre+tail
candidate=candidate.replace('valve-level cold-gas (nitrogen) propulsion','valve-level cold-gas propulsion')
candidate=candidate.replace('// Pure C++17, header-only', '// Gas experiments: N2 existing PR; He and Ar reference Helmholtz EOS; see Tests/GasExperiments-20261025/README.md.\n// Pure C++17, header-only')
(ROOT/'ColdGasSystem.candidate.h').write_text(candidate)
print(f'Candidate: {len(candidate.splitlines())} lines; shared header untouched')
