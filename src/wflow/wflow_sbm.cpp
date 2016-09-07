/*
State vars:
FirstZoneDepth
UStoreDepth
CanopyStorage


Forcing:
Precipitation
PotEvap


Parameters:
CanopyGapFraction
Cmax
PathFrac
FirstZoneThickness
*/

#include <math.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))



double sCurve(double X, double a, double b, double c) {
	/*
	double a= 0.0, b = 1.0, c = 1.0) :
	sCurve function :

	Input :
	-X input map
	- C determines the steepness or "stepwiseness" of the curve.
	The higher C the sharper the function.A negative C reverses the function.
	- b determines the amplitude of the curve
	- a determines the centre level(default = 0)

	Output :
	-result
	*/

	double s;

	s = 1.0 / (b + exp(-c * (X - a)));
	
	return (s);
}

void rainfall_interception_modrut(double Precipitation, double PotEvap, double CanopyGapFraction, double Cmax,
	double& NetInterception, double& ThroughFall, double& StemFlow, double& LeftOver, double& Interception, double& CanopyStorage) {
	/*
	Interception according to a modified Rutter model.The model is solved
	explicitly and there is no drainage below Cmax.

	Returns :
	-NetInterception : P - TF - SF(may be different from the actual wet canopy evaporation)
	- ThroughFall :
	-StemFlow :
	-LeftOver : Amount of potential eveporation not used
	- Interception : Actual wet canopy evaporation in this thimestep
	- CanopyStorage : Canopy storage at the end of the timestep
	*/
	double p, pt, Pfrac, DD, dC, D;

	p = CanopyGapFraction;
	pt = 0.1 * p;

	// Amount of P that falls on the canopy
	Pfrac = (1 - p - pt) * Precipitation;

	// S cannot be larger than Cmax, no gravity drainage below that
	DD = CanopyStorage > Cmax ? Cmax - CanopyStorage : 0.0;
	CanopyStorage = CanopyStorage - DD;

	// Add the precipitation that falls on the canopy to the store
	CanopyStorage = CanopyStorage + Pfrac;

	// Now do the Evap, make sure the store does not get negative
	dC = -1 * (CanopyStorage > PotEvap ? PotEvap : CanopyStorage);
	CanopyStorage += dC;

	LeftOver = PotEvap + dC; // Amount of evap not used

	// Now drain the canopy storage again if needed...
	D = CanopyStorage > Cmax ? Cmax - CanopyStorage : 0.0;
	CanopyStorage = CanopyStorage - D;

	// Calculate throughfall
	ThroughFall = DD + D + p * Precipitation;
	StemFlow = Precipitation * pt;

	// Calculate interception, this is NET Interception
	NetInterception = Precipitation - ThroughFall - StemFlow;
	Interception = -dC;
}



double actEvap_SBM(double RootingDepth, double WTable, double& UStoreDepth, double& FirstZoneDepth, double PotTrans, double smoothpar) {
	/*
	
	Actual evaporation function :

	-first try to get demand from the saturated zone, using the rootingdepth as a limiting factor
		- secondly try to get the remaining water from the unsaturated store
		- it uses an S - Curve the make sure roots het wet / dry gradually(basically)
		representing a root - depth distribution

		if ust is True, all ustore is deemed to be avaiable fro the roots a

		Input :

	-RootingDepth, WTable, UStoreDepth, FirstZoneDepth, PotTrans, smoothpar

	Output :

	-ActEvap, FirstZoneDepth, UStoreDepth ActEvapUStore
	*/

	// Step 1 from saturated zone, use rootingDepth as a limiting factor
	/*	#rootsinWater = WTable < RootingDepth
		#ActEvapSat = ifthenelse(rootsinWater, min(PotTrans, FirstZoneDepth), 0.0)
		# new method:
		# use sCurve to determine if the roots are wet.At the moment this ise set
		# to be a 0 - 1 curve
	*/
	int ust = 0;
	double wetroots, ActEvapSat, RestPotEvap, AvailCap, MaxExtr, ActEvap, ActEvapUStore;

	wetroots = sCurve(WTable, RootingDepth, 1.0, smoothpar);
	ActEvapSat = MIN(PotTrans * wetroots, FirstZoneDepth);
	FirstZoneDepth = FirstZoneDepth - ActEvapSat;
	RestPotEvap = PotTrans - ActEvapSat;

	// now try unsat store
	// #AvailCap = min(1.0, max(0.0, (WTable - RootingDepth) / (RootingDepth + 1.0)))

	if (ust >= 1) {
		AvailCap = UStoreDepth * 0.99;
	}else{
		AvailCap = MAX(0.0, WTable < RootingDepth ? 1.0 : RootingDepth / (WTable + 1.0));
	}
	MaxExtr = AvailCap * UStoreDepth;
	ActEvapUStore = MIN(MaxExtr, MIN(RestPotEvap, UStoreDepth));
	UStoreDepth = UStoreDepth - ActEvapUStore;
	ActEvap = ActEvapSat + ActEvapUStore;

	return (ActEvap);
}


/*
	WaterFrac - fraction of open water in this unit
*/
void wfhydro_sbm_update(double Precipitation, double PotEvap, double WaterFrac, double WaterLevel) {

	double MporeFrac = 0.0;

	rainfall_interception_modrut(Precipitation, PotEvap, CanopyGapFraction, Cmax, NetInterception, ThroughFall, StemFlow, LeftOver,
		Interception, CanopyStorage);

	PotTransSoil = MAX(0.0, LeftOver); //  # now in mm
	Interception = NetInterception
	// set FirstZoenDepth -> this extra only neede to be able to set zi from the outside
	FirstZoneDepth = (thetaS - thetaR) * (FirstZoneThickness - zi);
	UStoreCapacity = FirstZoneCapacity - FirstZoneDepth - UStoreDepth;
	AvailableForInfiltration = ThroughFall + StemFlow;

	// Runoff from water bodies and river network
	RunoffOpenWater = WaterFrac * AvailableForInfiltration;
	AvailableForInfiltration -= self.RunoffOpenWater;

	// TODO: add runoff subgrid generation. We ignore subgrid runoff generation for now!

	// First determine if the soil infiltration capacity can deal with the
	// amount of water
	// split between infiltration in undisturbed soil and compacted areas(paths)
	SoilInf = AvailableForInfiltration * (1 - self.PathFrac);
	PathInf = AvailableForInfiltration * PathFrac;
	
	// TODO: add soil infiltration reduction and snow modelling
	MaxInfiltSoil = MIN(SoilInf, InfiltCapSoil * soilInfRedu); 
	InfiltSoil = MIN(MaxInfiltSoil, UStoreCapacity);
	UStoreDepth = UStoreDepth + InfiltSoil;
	UStoreCapacity = UStoreCapacity - InfiltSoil;
	AvailableForInfiltration = AvailableForInfiltration - InfiltSoil;
	MaxInfiltPath = MIN(self.InfiltCapPath * soilInfRedu, PathInf);
	InfiltPath = MIN(MaxInfiltPath, UStoreCapacity);
	UStoreDepth = UStoreDepth + InfiltPath;
	UStoreCapacity = UStoreCapacity - InfiltPath;
	AvailableForInfiltration = AvailableForInfiltration - InfiltPath;
	ActInfilt = InfiltPath + InfiltSoil;
	InfiltExcess = UStoreCapacity > 0.0 ? self.AvailableForInfiltration : 0.0;
	ExcessWater = AvailableForInfiltration; // Saturation overland flow
	
	// Limit rootingdepth(if set externally)
	ActRootingDepth = MIN(FirstZoneThickness * 0.99, ActRootingDepth);
	// Determine transpiration
	// Split between bare soil / open water and vegetation
	potsoilopenwaterevap = (1.0 - CanopyGapFraction) * PotTransSoil;
	PotTrans = PotTransSoil - potsoilopenwaterevap;
	
	SaturationDeficit = self.FirstZoneCapacity - self.FirstZoneDepth;
	// Linear reduction of soil moisture evaporation based on deficit
	// Determine transpiration
		
	// Determine Open Water EVAP.Later subtract this from water that
	// enters the Kinematic wave
	RestEvap = potsoilopenwaterevap;
	ActEvapOpenWater = MIN(WaterLevel * 1000.0 * self.WaterFrac, self.WaterFrac * self.RestEvap);
	RestEvap = RestEvap - ActEvapOpenWater;
	// Next the rest is used for soil evaporation
	soilevap = RestEvap * MAX(0.0, MIN(1.0, SaturationDeficit / FirstZoneCapacity));
	soilevap = MIN(self.soilevap, self.UStoreDepth);
	UStoreDepth = UStoreDepth - soilevap;
	// rest is used for transpiration
	PotTrans = PotTransSoil - soilevap - ActEvapOpenWater;
	
	Transpiration = actEvap_SBM(double ActRootingDepth, double.zi, double& UStoreDepth, double& FirstZoneDepth, double PotTrans, 3000.0);


	ActEvap = Transpiration + soilevap + ActEvapOpenWater;
	//##########################################################################
	//# Transfer of water from unsaturated to saturated store...################
	//##########################################################################
	//# Determine saturation deficit.NB, as noted by Vertessy and Elsenbeer 1997
	//# this deficit does NOT take into account the water in the unsaturated zone

	//# Optional Macrco - Pore transfer
	MporeTransfer = ActInfilt * MporeFrac;
	FirstZoneDepth += MporeTransfer;
	UStoreDepth -= MporeTransfer;
	
	SaturationDeficit = self.FirstZoneCapacity - self.FirstZoneDepth;
	
	zi = MAX(0.0, FirstZoneThickness - FirstZoneDepth / (thetaS - thetaR));//  # Determine actual water depth
	Ksat = FirstZoneKsatVer * exp(-f * zi);
	DeepKsat = FirstZoneKsatVer * exp(-f * FirstZoneThickness);

	//# now the actual transfer to the saturated store..
	Transfer = MIN(UStoreDepth, self.SaturationDeficit <= 0.00001 ? 0.0 : Ksat * UStoreDepth / (SaturationDeficit + 1)));

	MaxCapFlux = MAX(0.0, MIN(Ksat, MIN(ActEvapUStore, MIN(UStoreCapacity, FirstZoneDepth))));

	//# No capilary flux is roots are in water, max flux if very near to water, lower flux if distance is large
	CapFluxScale = zi > ActRootingDepth ? CapScale / (CapScale + zi - ActRootingDepth) * timestepsecs / basetimestep : 0.0;
	CapFlux = MaxCapFlux * CapFluxScale;
	// Determine Ksat at base
	DeepTransfer = MIN(FirstZoneDepth, DeepKsat);
	//#ActLeakage = 0.0
	//# Now add leakage.to deeper groundwater
	ActLeakage = MAX(0.0, MIN(self.MaxLeakage, self.DeepTransfer));
	Percolation = MAX(0.0, MIN(MaxPercolation, DeepTransfer));
	
	ActLeakage = Seepage > 0.0 ? -1.0 * Seepage : ActLeakage;
	FirstZoneDepth = FirstZoneDepth + Transfer - CapFlux - ActLeakage - Percolation;
	UStoreDepth = UStoreDepth - Transfer + CapFlux;
	//##########################################################################
	// # Horizontal(downstream) transport of water #############################
	// ##########################################################################
	zi = MAX(0.0, FirstZoneThickness - FirstZoneDepth / (thetaS - thetaR)); //  # Determine actual water depth

	//# Re - Determine saturation deficit.NB, as noted by Vertessy and Elsenbeer 1997
	//# this deficit does NOT take into account the water in the unsaturated zone
	SaturationDeficit = FirstZoneCapacity - FirstZoneDepth;

	// TODO: lateral transport
	//##########################################################################
	//# Determine returnflow from first zone          ##########################
	//##########################################################################
	ExfiltWaterFrac = sCurve(FirstZoneDepth, FirstZoneCapacity, 1.0, 5.0);
	ExfiltWater = ExfiltWaterFrac * (FirstZoneDepth - FirstZoneCapacity);
	//#self.ExfiltWater = ifthenelse(self.FirstZoneDepth - self.FirstZoneCapacity > 0, self.FirstZoneDepth - self.FirstZoneCapacity, 0.0)
	FirstZoneDepth -= ExfiltWater;

	//# Re - determine UStoreCapacity
	zi = MAX(0.0, FirstZoneThickness - FirstZoneDepth / (thetaS - thetaR));//  # Determine actual water depth

	ExfiltFromUstore = zi == 0.0 ? (self.UStoreDepth > 0.0 ? self.UStoreDepth : self.ZeroMap) : self.ZeroMap;
	
	ExfiltWater += ExfiltFromUstore;
	UStoreDepth -= ExfiltFromUstore;
	UStoreCapacity = FirstZoneCapacity - FirstZoneDepth - UStoreDepth;
	Ksat = FirstZoneKsatVer * exp(-f * zi);


}