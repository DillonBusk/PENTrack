/**
 * \file
 * Main program.
 *
 * Create particles to your liking...
 */

#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <chrono>

using namespace std;

#include "particle.h"
#include "neutron.h"
#include "proton.h"
#include "electron.h"
#include "globals.h"
#include "fields.h"
#include "geometry.h"
#include "source.h"
#include "mc.h" 
#include "microroughness.h"

void ConfigInit(TConfig &config); // read config.in
void OutputCodes(const map<string, map<int, int> > &ID_counter); // print simulation summary at program exit
void PrintBFieldCut(const std::string &outfile, const TFieldManager &field); // evaluate fields on given plane and write to outfile
void PrintBField(const std::string &outfile, const TFieldManager &field);
void PrintGeometry(const std::string &outfile, TGeometry &geom); // do many random collisionchecks and write all collisions to outfile
void PrintMROutAngle(const std::string &outfile); // produce a 3d table of the MR-DRP for each outgoing solid angle
void PrintMRThetaIEnergy(const std::string &outfile); // produce a 3d table of the total (integrated) MR-DRP for a given incident angle and energy


double SimTime = 1500.; ///< max. simulation time
int simcount = 1; ///< number of particles for MC simulation (read from config)
simType simtype = PARTICLE; ///< type of particle which shall be simulated (read from config)
int secondaries = 1; ///< should secondary particles be simulated? (read from config)
double BCutPlanePoint[9]; ///< 3 points on plane for field slice (read from config)
double MRSolidAngleDRPParams[5]; ///< params to output the  MR-DRP values for given theta_inc and phi_inc [Fermi potential, incident neutron energy, b (nm), w (nm), theta_i] (read from config)
double MRThetaIEnergyParams[7]; ///< params for which to output the integrated MR-DRP values [Fermi potential, b (nm), w (nm), theta_i_start, theta_i_end, neut_energy_start, neut_energy_end] (read from config.in)
int BCutPlaneSampleCount1; ///< number of field samples in BCutPlanePoint[3..5]-BCutPlanePoint[0..2] direction (read from config)
int BCutPlaneSampleCount2; ///< number of field samples in BCutPlanePoint[6..8]-BCutPlanePoint[0..2] direction (read from config)

/**
 * Catch signals.
 *
 * terminates a program if a specific signal occurs
 *
 * @param sig signalnumber which called the handler; to get the right number
 * 				for corresponding signals have a look "man signal.h".
 * 				e.g: "SIGFPE" is connected to number 8
 */
void catch_alarm (int sig){
	printf("Program was terminated, because Signal %i occured\n", sig);
	exit(1);
}


/**
 * main function.
 *
 * @param argc Number of parameters passed via the command line
 * @param argv Array of parameters passed via the command line (./Track [jobnumber [configpath [outputpath [seed]]]])
 * @return Return 0 on success, value !=0 on failure
 *
 */
int main(int argc, char **argv){
	if ((argc > 1) && (strcmp(argv[1], "-h") == 0)){
		cout << "Usage:\nPENTrack [jobnumber [path/to/in/files [path/to/out/files [seed]]]]" << endl;
		exit(0);
	}

	//Initialize signal-analizing
	signal (SIGINT, catch_alarm);
	signal (SIGUSR1, catch_alarm);
	signal (SIGUSR2, catch_alarm);
	signal (SIGXCPU, catch_alarm);
	
	jobnumber = 0;
	outpath = "./out";
	string inpath = "./in";
	uint64_t seed = 0;
	if(argc>1) // if user supplied at least 1 arg (jobnumber)
		istringstream(argv[1]) >> jobnumber;
	if(argc>2) // if user supplied 2 or more args (jobnumber, inpath)
		inpath = argv[2]; // input path pointer set
	if(argc>3) // if user supplied 3 or more args (jobnumber, inpath, outpath)
		outpath = argv[3]; // set the output path pointer
	if (argc>4) // if user supplied 4 or more args (jobnumber, inpath, outpath, seed)
		istringstream(argv[4]) >> seed;
	
	TConfig configin;
	ReadInFile(string(inpath + "/config.in").c_str(), configin);
	TConfig geometryin;
	ReadInFile(string(inpath + "/geometry.in").c_str(), geometryin);
	TConfig particlein;
	ReadInFile(string(inpath + "/particle.in").c_str(), particlein); // read particle specific log configuration from particle.in
	for (TConfig::iterator i = particlein.begin(); i != particlein.end(); i++){
		if (i->first != "all"){
			i->second = particlein["all"]; // set all particle specific settings to the "all" settings
		}
	}
	ReadInFile(string(inpath+"/particle.in").c_str(), particlein); // read again to overwrite "all" settings with particle specific settings

	// read config.in
	ConfigInit(configin);

	if (simtype == MR_THETA_OUT_ANGLE){
		PrintMROutAngle(string(outpath+"/MR-SldAngDRP").c_str()); // estimate ramp heating
		return 0;
	}
	else if (simtype == MR_THETA_I_ENERGY){
		PrintMRThetaIEnergy(string(outpath+"/MR-Tot-DRP").c_str()); // print cut through B field
		return 0;
	}


	cout << "Loading fields...\n";
	// load field configuration from geometry.in
	TFieldManager field(geometryin);

	if (simtype == BF_ONLY){
		PrintBField(string(outpath+"/BF.out").c_str(), field); // estimate ramp heating
		return 0;
	}
	else if (simtype == BF_CUT){
		PrintBFieldCut(string(outpath+"/BFCut.out").c_str(), field); // print cut through B field
		return 0;
	}


	cout << "Loading geometry...\n";
	//load geometry configuration from geometry.in
	TGeometry geom(geometryin);
	
	if (simtype == GEOMETRY){
		// print random points on walls in file to visualize geometry
		PrintGeometry(string(outpath+"/geometry.out").c_str(), geom);
		return 0;
	}
	
	cout << "Loading random number generator...\n";
	// load random number generator from all3inone.in
	TMCGenerator mc(string(inpath + "/particle.in").c_str(), seed);
	
	cout << "Loading source...\n";
	// load source configuration from geometry.in
	TSource source(geometryin, mc, geom, &field);

	int ntotalsteps = 0;     // counters to determine average steps per integrator call
	float InitTime = (1.*clock())/CLOCKS_PER_SEC; // time statistics

	// simulation time counter
	chrono::time_point<chrono::steady_clock> simstart = chrono::steady_clock::now();

	printf(
	" ########################################################################\n"
	" ###                      Welcome to PENTrack,                        ###\n"
	" ### a simulation tool for ultra-cold neutrons, protons and electrons ###\n"
	" ########################################################################\n");

	map<string, map<int, int> > ID_counter; // 2D map to store number of each ID for each particle type
	
	/*
	stringstream filename;
	filename << "in/42_0063eout2000m_" << jobnumber << ".out";
	ifstream infile(filename.str().c_str());
	if (!infile.is_open()){
		printf("\ninfile %s not found!\n",filename.str().c_str());
		exit(-1);
	}
	infile.ignore(1024*1024, '\n');
	int i = 0;
	long double r,phi,z,phieuler,thetaeuler,E_n,Ekin,dt,dummy;
	while (infile.good()){
		i++;
		infile >> r >> phi >> z >> phieuler >> thetaeuler >> E_n >> Ekin >> dummy >> dummy >> dummy >>  dummy >> dummy >> dummy >> dummy >> dt;
		infile.ignore(1024*1024, '\n');
		TParticle particle(ELECTRON, i, 0, dt, r, phi*conv, z, Ekin, (phieuler-phi)*conv, thetaeuler*conv, E_n, 0, field);
		particle.Integrate(geom, mc, field, endlog, tracklog, snap, &snapshots, reflectlog);
		ID_counter[particle.protneut % 3][particle.ID]++; // increase ID-counter
		ntotalsteps += particle.nsteps;
		IntegratorTime += particle.comptime;
		ReflTime += particle.refltime;
		infile >> ws;
	}
*/
	if (simtype == PARTICLE){ // if proton or neutron shall be simulated
		for (int iMC = 1; iMC <= simcount; iMC++)
		{
			TParticle *p = source.CreateParticle();
			p->Integrate(SimTime, particlein[p->GetName()]); // integrate particle
			ID_counter[p->GetName()][p->GetStopID()]++; // increment counters
			ntotalsteps += p->GetNumberOfSteps();

			if (secondaries == 1){
				std::vector<TParticle*> secondaries = p->GetSecondaryParticles();
				for (auto i = secondaries.begin(); i != secondaries.end(); i++){
					(*i)->Integrate(SimTime, particlein[(*i)->GetName()]); // integrate secondary particles
					ID_counter[(*i)->GetName()][(*i)->GetStopID()]++;
					ntotalsteps += (*i)->GetNumberOfSteps();
				}
			}

			delete p;
		}
	}
	else{
		printf("\nDon't know simtype %i! Exiting...\n",simtype);
		exit(-1);
	}



	OutputCodes(ID_counter); // print particle IDs
	
	// print statistics
	printf("The integrator made %d steps. \n", ntotalsteps);
	chrono::time_point<chrono::steady_clock> simend = chrono::steady_clock::now();
	float SimulationTime = chrono::duration_cast<chrono::milliseconds>(simend - simstart).count()/1000.;
	printf("Init: %.2fs, Simulation: %.2fs\n",
			InitTime, SimulationTime);
	printf("That's it... Have a nice day!\n");
	

	ostringstream fileprefix;
	fileprefix << outpath << "/" << setw(8) << setfill('0') << jobnumber << setw(0);

	return 0;
}


/**
 * Read config file.
 *
 * @param config TConfig struct containing [global] options map
 */
void ConfigInit(TConfig &config){
	/* setting default values */
	simtype = PARTICLE;
	simcount = 1;
	/*end default values*/

	/* read variables from map by casting strings in map into istringstreams and extracting value with ">>"-operator */
	int stype;
	istringstream(config["global"]["simtype"])		>> stype;
	simtype = static_cast<simType>(stype);
	

	istringstream(config["global"]["simcount"])		>> simcount;
	istringstream(config["global"]["simtime"])		>> SimTime;
	istringstream(config["global"]["secondaries"])	>> secondaries;
	istringstream(config["global"]["BCutPlane"])	>> BCutPlanePoint[0] >> BCutPlanePoint[1] >> BCutPlanePoint[2]
													>> BCutPlanePoint[3] >> BCutPlanePoint[4] >> BCutPlanePoint[5]
													>> BCutPlanePoint[6] >> BCutPlanePoint[7] >> BCutPlanePoint[8]
													>> BCutPlaneSampleCount1 >> BCutPlaneSampleCount2;

	istringstream(config["global"]["MRSolidAngleDRP"]) >> MRSolidAngleDRPParams[0] >> MRSolidAngleDRPParams[1] >> MRSolidAngleDRPParams[2] >> MRSolidAngleDRPParams[3]
													>> MRSolidAngleDRPParams[4];

	istringstream(config["global"]["MRThetaIEnergy"]) >> MRThetaIEnergyParams[0] >>  MRThetaIEnergyParams[1] >> MRThetaIEnergyParams[2] >> MRThetaIEnergyParams[3]
										     >>	 MRThetaIEnergyParams[4] >> MRThetaIEnergyParams[5] >> MRThetaIEnergyParams[6];
}

/**
 * 
 * Output a table containing the MR diffuse reflection probability for the specified range of solid angles from the config.in file
 *
 * @param outfile The file name of the file to which results will be printed
 *  
 * Other params are read in from the config.in file
*/
void PrintMROutAngle(const std::string &outfile) {
	
	cout << "\nGenerating table of MR diffuse reflection probability for all solid angles ..." << endl;	
	
	/** Create a material struct that defines vacuum (the leaving material) and the material the neutron is being reflected from (the entering material **/
	material matEnter = { "reflection surface material" , MRSolidAngleDRPParams[0], 0, 0, 0, MRSolidAngleDRPParams[2], MRSolidAngleDRPParams[3], true }; 
	material matLeav = { "vacuum material", 0, 0, 0, 0, 0, 0, true };
	
	/** Create a solid object that the neutron is leaving and entering based on the materials created in the previous step **/
	solid solEnter = { "reflection solid", matEnter, 2 }; // no ignore times (priority = 2) 
	solid solLeav = { "vacuum solid", matLeav, 1 }; //no ignore times (priority = 1 )
	
	ostringstream oss;
	oss << outfile << "-F" << MRSolidAngleDRPParams[0] << "-En" << MRSolidAngleDRPParams[1] << "-b" << MRSolidAngleDRPParams[2] << "-w" << MRSolidAngleDRPParams[3] << "-th" << MRSolidAngleDRPParams[4] << ".out"; 
 	string fileName = oss.str();
	
	ofstream mrproboutfile(fileName.c_str());
	if (!mrproboutfile) {
		cout << "Could not open " << fileName.c_str() << "!\n";
		exit (-1);
	} 

	//print file header
	mrproboutfile << "phi_out theta_out mrdrp\n";
	double theta_inc = MRSolidAngleDRPParams[4];
	
	//determine neutron velocity corresponding to the energy and create a state_type vector from it
	double vabs = sqrt(2*MRSolidAngleDRPParams[1]*1e-9/m_n);
	double v[3] = {0, vabs*sin(theta_inc), -vabs*cos(theta_inc)};
	double norm[] = { 0, 0, 1 };

	//write the mrprob values to the output file
	for (double phi=-pi; phi<pi; phi+=(2*pi)/100) {
		
		for (double theta=0; theta<pi/2; theta+=(pi/2)/100) {
			//the sin(theta) factor is needed to normalize for different size of surface elements in spherical coordinates
			double mrprob = MR::MRDist(false, false, v, norm, solLeav, solEnter, theta, phi)*sin(theta);
			mrproboutfile << phi << ' ' << theta << ' ' << mrprob << '\n';
		}
		for (double theta=0; theta<pi/2; theta+=(pi/2)/100) {
			//the sin(theta) factor is needed to normalize for different size of surface elements in spherical coordinates
			double mrprob = MR::MRDist(true, false, v, norm, solLeav, solEnter, theta, phi)*sin(theta);
			mrproboutfile << phi << ' ' << pi - theta << ' ' << mrprob << '\n';
		}
	}

} // end PrintMRThetaIEnergy

/**
 * 
 * Output a table in root format giving the total MR DRP for a set of incident theta angles and neutron energy. 
 *
 * @param outfile The file name to which the results will be printed
*/
void PrintMRThetaIEnergy(const std::string &outfile) {
	
	cout << "\nGenerating table of integrated MR diffuse reflection probability for different incident angle and energy ..." << endl;	

	/** Create a material struct that defines vacuum (the leaving material) and the material the neutron is being reflected from (the entering material **/
	material matEnter = { "reflection surface material", MRThetaIEnergyParams[0], 0, 0, 0, MRThetaIEnergyParams[1], MRThetaIEnergyParams[2], true };
	material matLeav = { "reflection surface material", 0, 0, 0, 0, 0, 0, true  };

	/** Create a solid object that the neutron is leaving and entering based on the materials created in the previous step **/
	solid solEnter = { "reflection solid", matEnter, 2 }; // no ignore times (priority = 2) 
	solid solLeav = { "vacuum solid", matLeav, 1 }; //no ignore times (priority = 1 )
	
	ostringstream oss;
	oss << outfile << "-F" << MRThetaIEnergyParams[0] << "-b" << MRThetaIEnergyParams[1] << "-w" << MRThetaIEnergyParams[2] << ".out"; 
 	string fileName = oss.str();	
	
	ofstream mroutfile(fileName.c_str());
	if (!mroutfile){
		cout << "Could not open " << fileName.c_str() << "!\n";
		exit(-1);
	}

	mroutfile << "theta_i neut_en totmrdrp\n"; //print the header
	
	//define the min and max values of the following for loop
	double theta_start = MRThetaIEnergyParams[3];
	double theta_end = MRThetaIEnergyParams[4];
	double neute_start = MRThetaIEnergyParams[5];
	double neute_end = MRThetaIEnergyParams[6];
	int prevProg=0;
	std::cout << '\n';
	
	double norm[] = { 0, 0, 1 };
 	//write the integrated mrprob values to the output file 
	for (double theta = theta_start; theta<theta_end; theta += (theta_end-theta_start)/100) {
		//since this part can be slow it is helpful to monitor the progress
		PrintPercent((theta - theta_start)/(theta_end - theta_start), prevProg);

		for (double energy = neute_start; energy<neute_end; energy += (neute_end-neute_start)/100) {
			//determine neutron velocity corresponding to the energy and create a state_type vector from it
			double vabs = sqrt(2*energy*1e-9/m_n);
			double v[3] = {0, vabs*sin(theta), -vabs*cos(theta)};
			//the sin(theta) factor is needed to noramlize for different sizes of surface elements in spherical coordinates
			double totmrprob = MR::MRProb(false, v, norm, solLeav, solEnter);
			mroutfile << theta << ' ' << energy << ' ' << totmrprob << '\n';
		}
	}
	std::cout << '\n';
} // end PrintMRThetaIEnergy


/**
 * Print final particles statistics.
 *
 * @param ID_counter A list of counters indicating the numbers of particles with each stopID.
 */
void OutputCodes(const map<string, map<int, int> > &ID_counter){
	cout << "\nThe simulated particles suffered following fates:\n";
	for (auto i = ID_counter.begin(); i != ID_counter.end(); i++){
		map<int, int> counts = i->second;
		const char *name = i->first.c_str();
		printf("%4i: %6i %10s(s) were absorbed on a surface\n",	 2, counts[ 2], name);
		printf("%4i: %6i %10s(s) were absorbed in a material\n", 1, counts[ 1], name);
		printf("%4i: %6i %10s(s) were not categorized\n",		 0, counts[ 0], name);
		printf("%4i: %6i %10s(s) did not finish\n",				-1, counts[-1], name);
		printf("%4i: %6i %10s(s) hit outer boundaries\n",		-2, counts[-2], name);
		printf("%4i: %6i %10s(s) produced integration error\n", -3, counts[-3], name);
		printf("%4i: %6i %10s(s) decayed\n",					-4, counts[-4], name);
		printf("%4i: %6i %10s(s) found no initial position\n",	-5, counts[-5], name);
		printf("%4i: %6i %10s(s) encountered CGAL error\n",		-6, counts[-6], name);
		printf("%4i: %6i %10s(s) encountered geometry error\n",	-7, counts[-7], name);
		printf("\n");
	}
}


/**
 * Print planar slice of fields into a file.
 *
 * The slice plane is given by three points BCutPlayPoint[0..8] on the plane
 *
 * @param outfile filename of result file
 * @param field TFieldManager structure which should be evaluated
 */
void PrintBFieldCut(const std::string &outfile, const TFieldManager &field){
	// get directional vectors from points on plane by u = p2-p1, v = p3-p1
	double u[3] = {BCutPlanePoint[3] - BCutPlanePoint[0], BCutPlanePoint[4] - BCutPlanePoint[1], BCutPlanePoint[5] - BCutPlanePoint[2]};
	double v[3] = {BCutPlanePoint[6] - BCutPlanePoint[0], BCutPlanePoint[7] - BCutPlanePoint[1], BCutPlanePoint[8] - BCutPlanePoint[2]};
	
	// open output file
	ofstream cutfile(outfile);
	if (!cutfile){
		std::cout << "Could not open " << outfile << "!\n";
		exit(-1);
	}
	// print file header
	cutfile << "x y z Bx dBxdx dBxdy dBxdz By dBydx dBydy dBydz Bz dBzdx dBzdy dBzdz Ex Ey Ez V\n";
	
	double Pp[3];
	double B[3], dBidxj[3][3],Ei[3],V;
	float start = clock(); // do some time statistics
	// sample field BCutPlaneSmapleCount1 times in u-direction and BCutPlaneSampleCount2 time in v-direction
	for (int i = 0; i < BCutPlaneSampleCount1; i++) {
		for (int j = 0; j < BCutPlaneSampleCount2; j++){
			for (int k = 0; k < 3; k++)
				Pp[k] = BCutPlanePoint[k] + i*u[k]/BCutPlaneSampleCount1 + j*v[k]/BCutPlaneSampleCount2;
			// print B-/E-Field to file
			cutfile << Pp[0] << " " << Pp[1] << " " << Pp[2] << "\n";
			
			field.BField(Pp[0], Pp[1], Pp[2], 0, B, dBidxj);
			for (int k = 0; k < 3; k++){
				cutfile << B[k] << " ";
				for (int l = 0; l < 3; l++)
					cutfile << dBidxj[k][l] << " ";
			}

			field.EField(Pp[0], Pp[1], Pp[2], 0, V, Ei);
			cutfile << Ei[0] << " " << Ei[1] << " " << Ei[2] << " " << V << "\n";
		}
	}
	start = (clock() - start)/CLOCKS_PER_SEC;
	//close file
	cutfile.close();
	// print time statistics
	printf("Called BFeld and EFeld %u times in %fs (%fms per call)\n",BCutPlaneSampleCount1*BCutPlaneSampleCount2, start, start/BCutPlaneSampleCount1/BCutPlaneSampleCount2*1000);
}


/**
 * Ramp Heating Analysis.
 *
 * "Count" phase space for each energy bin and calculate "heating" of the neutrons due to
 * phase space compression by magnetic field ramping
 *
 * @param outfile Filename of output file
 * @param field TField structure which should be evaluated
 */
void PrintBField(const std::string &outfile, const TFieldManager &field){
	// print BField to file
	ofstream bfile(outfile);
	if (!bfile){
		std::cout << "Could not open " << outfile << "!\n";
		exit(-1);
	}

	bfile << "r phi z Bx By Bz 0 0 Babs\n";
	double rmin = 0.12, rmax = 0.5, zmin = 0, zmax = 1.2;
	int E;
	const int Emax = 108;
	double dr = 0.1, dz = 0.1;
	double VolumeB[Emax + 1];
	for (E = 0; E <= Emax; E++) VolumeB[E] = 0;
	
	double EnTest;
	double B[3];
	// sample space in cylindrical pattern
	for (double r = rmin; r <= rmax; r += dr){
		for (double z = zmin; z <= zmax; z += dz){
			field.BField(r, 0, z, 500.0, B); // evaluate field
			double Babs = sqrt(B[0]*B[0] + B[1]*B[1] + B[2]*B[2]);
			// print field values
			bfile << r << " " << 0.0 << " " << z << " " << B[0] << " " << B[1] << " " << B[2] << " " << 0.0 << " " << 0.0 << " " << Babs << "\n";
			std::cout << "r=" << r << ", z=" << z << ", Br=" << B[0] << " T, Bz=" << B[2] << " T\n";
			
			// Ramp Heating Analysis
			for (E = 0; E <= Emax; E++){
				EnTest = E*1.0e-9 - m_n*gravconst*z - mu_nSI/ele_e * Babs;
				if (EnTest >= 0){
					// add the volume segment to the volume that is accessible to a neutron with energy Energie
					VolumeB[E] = VolumeB[E] + pi * dz * ((r+0.5*dr)*(r+0.5*dr) - (r-0.5*dr)*(r-0.5*dr));
				}
			}
		}
	}
	bfile.close();

	// for investigating ramp heating of neutrons, volume accessible to neutrons with and
	// without B-field is calculated and the heating approximated by thermodynamical means
	printf("\nEnergie [neV], Volumen ohne B-Feld, mit B-Feld, 'Erwaermung'");
	double Volume;
	for (E = 0; E <= Emax; E++) 
	{
		Volume = ((E * 1.0e-9 / (m_n * gravconst))) * pi * (rmax*rmax-rmin*rmin);
		// isentropische zustandsnderung, kappa=5/3
		printf("\n%i %.17g %.17g %.17g",E,Volume,VolumeB[E],E * pow((Volume/VolumeB[E]),(2.0/3.0)) - E);
	}
}


/**
 * Sample geometry randomly to visualize it.
 *
 * Creates random line segments and prints every intersection point with a surface
 * into outfile
 *
 * @param outfile File name of output file
 * @param geom TGeometry structure which shall be sampled
 */
void PrintGeometry(const std::string &outfile, TGeometry &geom){
    double p1[3], p2[3];
    double theta, phi;
    // create count line segments with length raylength
    unsigned count = 1000000, collcount = 0, raylength = 1;

    ofstream f(outfile);
    f << "x y z ID" << '\n'; // print file header

    srand(time(NULL));
	chrono::time_point<chrono::steady_clock> collstart = chrono::steady_clock::now();
	for (unsigned i = 0; i < count; i++){
    	// random segment start point
        for (int j = 0; j < 3; j++)
        	p1[j] = (double)rand()/RAND_MAX * (geom.mesh.tree.bbox().max(j) - geom.mesh.tree.bbox().min(j)) + geom.mesh.tree.bbox().min(j);
		// random segment direction
        theta = (double)rand()/RAND_MAX*pi;
		phi = (double)rand()/RAND_MAX*2*pi;
		// translate direction and length into segment end point
		p2[0] = p1[0] + raylength*sin(theta)*cos(phi);
		p2[1] = p1[1] + raylength*sin(theta)*sin(phi);
		p2[2] = p1[2] + raylength*cos(theta);

	    set<TCollision> c;
		if (geom.mesh.Collision(p1,p2,c)){ // check if segment intersected with surfaces
			collcount++;
			for (set<TCollision>::iterator i = c.begin(); i != c.end(); i++){ // print all intersection points into file
				f << p1[0] + i->s*(p2[0]-p1[0]) << " " << p1[1] + i->s*(p2[1] - p1[1]) << " " << p1[2] + i->s*(p2[2] - p1[2]) << " " << geom.solids[i->sldindex].ID << '\n';
			}
		}
    }
	chrono::time_point<chrono::steady_clock> collend = chrono::steady_clock::now();
	float colltimer = chrono::duration_cast<chrono::nanoseconds>(collend - collstart).count();
    // print some time statistics
    printf("%u tests, %u collisions in %fms (%fms per Test, %fms per Collision)\n",count,collcount,colltimer/1e6,colltimer/count/1e6,colltimer/collcount/1e6);
    f.close();	
}
