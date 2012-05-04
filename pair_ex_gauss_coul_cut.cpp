/* ----------------------------------------------------------------------
Copyright (2010) Aram Davtyan and Garegin Papoian

Papoian's Group, University of Maryland at Collage Park
http://papoian.chem.umd.edu/

Gaussian Contacts Potential was contributed by Weihua Zheng

Last Update: 05/01/2012
------------------------------------------------------------------------- */

#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "pair_ex_gauss_coul_cut.h"
#include "atom.h"
#include "comm.h"
#include "force.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "memory.h"
#include "error.h"

#include <string>

using std::ifstream;

using namespace LAMMPS_NS;

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

/* ---------------------------------------------------------------------- */

PairExGaussCoulCut::PairExGaussCoulCut(LAMMPS *lmp) : Pair(lmp) {}

/* ---------------------------------------------------------------------- */

PairExGaussCoulCut::~PairExGaussCoulCut()
{
  if (allocated) {
    memory->destroy(setflag);
    memory->destroy(cutsq);

    memory->destroy(cut_ex);
    memory->destroy(cut_exsq);
    memory->destroy(cut_coul);
    memory->destroy(cut_coulsq);
    memory->destroy(offset);
    
    int n = atom->ntypes;
    for (int i=0;i<=n;i++) {
		delete [] par[i];
	}
	delete [] par;
  }
}

/* ---------------------------------------------------------------------- */

void PairExGaussCoulCut::compute(int eflag, int vflag)
{
  int i,j,ii,jj,k,inum,jnum,itype,jtype;
  double qtmp,xtmp,ytmp,ztmp,delx,dely,delz,evdwl,ecoul,fpair;
  double r,rsq,rinv,r2inv,rlinv,force_coul,force_ex,force_gauss,factor_coul,factor_ex,factor_gauss;
  int *ilist,*jlist,*numneigh,**firstneigh;

  evdwl = ecoul = 0.0;
  if (eflag || vflag) ev_setup(eflag,vflag);
  else evflag = vflag_fdotr = 0;

  double **x = atom->x;
  double **f = atom->f;
  double *q = atom->q;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  double *special_coul = force->special_coul;
  double *special_lj = force->special_lj;
  int newton_pair = force->newton_pair;
  double qqrd2e = force->qqrd2e;

  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;
  
  // loop over neighbors of my atoms

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    qtmp = q[i];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      factor_ex = factor_gauss = special_lj[sbmask(j)];
      factor_coul = special_coul[sbmask(j)];
      j &= NEIGHMASK;

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx*delx + dely*dely + delz*delz;
      jtype = type[j];

      if (rsq < cutsq[itype][jtype]) {
	r2inv = 1.0/rsq;
	rinv = sqrt(r2inv);
	
	parameters &pij = par[itype][jtype];

	if (el_flag && rsq < cut_coulsq[itype][jtype])
	  force_coul = qqrd2e * qtmp*q[j]*rinv;
	else force_coul = 0.0;

	if (pij.ex_flag && rsq < cut_exsq[itype][jtype]) {
	  rlinv = pow(rinv,pij.l);
	  force_ex = pij.l*pij.A*rlinv;
//	  force_ex = r6inv * (lj1[itype][jtype]*r6inv - lj2[itype][jtype]);
	} else force_ex = 0.0;
	
	if (pij.ng>0 && rsq < cut_exsq[itype][jtype]) {
		force_gauss = 0.0;
		r = sqrt(rsq);
		for (k=0;k<pij.ng;++k) {
			force_gauss += 2*pij.C[k]*pij.B[k]*(r-pij.R[k])*exp(-pij.C[k]*(r-pij.R[k])*(r-pij.R[k]));
		}
	} else force_gauss = 0.0;

	fpair = (factor_coul*force_coul + factor_ex*force_ex) * r2inv + factor_gauss*force_gauss * rinv;

	f[i][0] += delx*fpair;
	f[i][1] += dely*fpair;
	f[i][2] += delz*fpair;
	if (newton_pair || j < nlocal) {
	  f[j][0] -= delx*fpair;
	  f[j][1] -= dely*fpair;
	  f[j][2] -= delz*fpair;
	}
	
//	double Ft=fpair*sqrt(delx*delx+dely*dely+delz*delz);
	double Ft=(factor_coul*force_coul + factor_ex*force_ex) * rinv + factor_gauss*force_gauss;
    fprintf(fout, "{%f, %f},", sqrt(rsq), Ft);

	if (eflag) {
	  if (el_flag && rsq < cut_coulsq[itype][jtype])
	    ecoul = factor_coul * qqrd2e * qtmp*q[j]*rinv;
	  else ecoul = 0.0;
	  if (pij.ex_flag && rsq < cut_exsq[itype][jtype]) {
	    evdwl = pij.A*rlinv -
	      offset[itype][jtype];
	    evdwl *= factor_ex;
	  } else evdwl = 0.0;
	 
	  if (pij.ng>0 && rsq < cut_exsq[itype][jtype]) {
	  	for (k=0;k<pij.ng;++k) {
	  	  evdwl += pij.B[k]*exp(-pij.C[k]*(r-pij.R[k])*(r-pij.R[k])) * factor_gauss;
	  	}
	  }
	}
	
//	if (eflag) fprintf(fout, "{%f, %f},", sqrt(rsq), evdwl);

	if (evflag) ev_tally(i,j,nlocal,newton_pair,
			     evdwl,ecoul,fpair,delx,dely,delz);
      }
    }
  }

  if (vflag_fdotr) virial_compute();
}

/* ----------------------------------------------------------------------
   allocate all arrays
------------------------------------------------------------------------- */

void PairExGaussCoulCut::allocate()
{
  fprintf(screen, "\nALLOCATE\n");

  allocated = 1;
  int n = atom->ntypes;

  memory->create(setflag,n+1,n+1,"pair:setflag");
  for (int i = 1; i <= n; i++)
    for (int j = i; j <= n; j++)
      setflag[i][j] = 0;

  memory->create(cutsq,n+1,n+1,"pair:cutsq");

  memory->create(cut_ex,n+1,n+1,"pair:cut_ex");
  memory->create(cut_exsq,n+1,n+1,"pair:cut_exsq");
  memory->create(cut_coul,n+1,n+1,"pair:cut_coul");
  memory->create(cut_coulsq,n+1,n+1,"pair:cut_coulsq");
  memory->create(offset,n+1,n+1,"pair:offset");

  par = new parameters*[n+1]; //parameters array
  for (int i=0;i<=n;i++) {
  	par[i] = new parameters[n+1];
  }
}

/* ----------------------------------------------------------------------
   global settings
------------------------------------------------------------------------- */

void PairExGaussCoulCut::settings(int narg, char **arg)
{
  fprintf(screen, "\nSETTINGS\n");

  if (narg < 2 || narg > 3) error->all("Illegal pair_style command");
  
  int len = strlen(arg[0]) + 1;
  parfile = new char[len];
  strcpy(parfile, arg[0]);

  cut_ex_global = force->numeric(arg[1]);
  if (narg == 2) cut_coul_global = cut_ex_global;
  else cut_coul_global = force->numeric(arg[2]);

  // reset cutoffs that have been explicitly set

  if (allocated) {
    int i,j;
    for (i = 1; i <= atom->ntypes; i++)
      for (j = i+1; j <= atom->ntypes; j++)
	if (setflag[i][j]) {
	  cut_ex[i][j] = cut_ex_global;
	  cut_coul[i][j] = cut_coul_global;
	}
  }
  
  el_flag = false;
  
  read_par_flag = true;
  
  fout = fopen("forcesPION.dat","w");
}

/* ----------------------------------------------------------------------
   set coeffs for one or more type pairs
------------------------------------------------------------------------- */

void PairExGaussCoulCut::coeff(int narg, char **arg)
{
  fprintf(screen, "\nCOEFF\n");

  if (narg < 2 || narg > 4) error->all("Incorrect args for pair coefficients2");
  if (!allocated) allocate();

  int ilo,ihi,jlo,jhi;
  force->bounds(arg[0],atom->ntypes,ilo,ihi);
  force->bounds(arg[1],atom->ntypes,jlo,jhi);

  double cut_ex_one = cut_ex_global;
  double cut_coul_one = cut_coul_global;
  if (narg >= 3) cut_coul_one = cut_ex_one = force->numeric(arg[2]);
  if (narg == 4) cut_coul_one = force->numeric(arg[3]);

  int count = 0;
  for (int i = ilo; i <= ihi; i++) {
    for (int j = MAX(jlo,i); j <= jhi; j++) {
//      epsilon[i][j] = epsilon_one;
//      sigma[i][j] = sigma_one;
      cut_ex[i][j] = cut_ex_one;
      cut_coul[i][j] = cut_coul_one;
      setflag[i][j] = 1;
      count++;
    }
  }

  if (count == 0) error->all("Incorrect args for pair coefficients");
  
  if (read_par_flag) read_parameters();
}

char *PairExGaussCoulCut::ltrim(char *s)
{     
  while(isspace(*s)) s++;     
  return s; 
}  

char *PairExGaussCoulCut::rtrim(char *s)
{
  char* back;
  int len = strlen(s);

  if(len == 0)
    return(s); 

  back = s + len;     
  while(isspace(*--back));     
  *(back+1) = '\0';     
  return s; 
}  

char *PairExGaussCoulCut::trim(char *s)
{     
  return rtrim(ltrim(s));  
}

bool PairExGaussCoulCut::isEmptyString(char *str)
{
  int len = strlen(str);
  
  if (len==0) return true;
  
  for (int i=0;i<len;++i) {
    if (str[i]!=' ' && str[i]!='\t' && str[i]!='\n') return false;
  }

  return true;
}

void PairExGaussCoulCut::read_parameters()
{
  // Read parameter file
  
  fprintf(screen, "\nREAD_PARAMETERS\n");
  
  read_par_flag = false;
  
  int numG=-1, i, j, iG, count;
//  char ity[10], jty[10];
  int ilo,ihi,jlo,jhi;
  double A_val,B_val,C_val,R_val,l_val;
  
  FILE *file;
  int file_state, narg;
  char ln[255], *line, *arg[10];
  enum File_States{FS_NONE=0, FS_GAUSS, FS_GAUSS_BCR, FS_EXCL};
  
  file = fopen(parfile,"r");
  if (!file) error->all("Pair_style Ex/Gauss/Coul/Cut: Error opening coefficient file!");
  file_state = FS_NONE;
  while ( fgets ( ln, sizeof ln, file ) != NULL ) {
  	line = trim(ln);
  	
  	if (numG>0 && file_state!=FS_GAUSS_BCR) error->all("Pair_style Ex/Gauss/Coul/Cut: Error reading coefficient file");
  	
  	if (line[0]=='#') continue;
    if (line[0]=='[') file_state = FS_NONE;
    if (isEmptyString(line)) { file_state = FS_NONE; continue; }
    
    switch (file_state) {
    case FS_GAUSS:
    	numG = -1;
    case FS_GAUSS_BCR:
    	narg = 0;
		arg[narg] = strtok(line," \t\n");
		while ( arg[narg]!=NULL ) {
			narg++;
			if (narg>3) break;
			arg[narg] = strtok(NULL," \t\n");
		}
		fprintf(screen, "Gauss narg=%d\n", narg);
		if (narg!=3) error->all("Pair_style Ex/Gauss/Coul/Cut: Wrong format in coefficient file (Gauss coeff).");
		
		if (file_state==FS_GAUSS) {
			force->bounds(arg[0],atom->ntypes,ilo,ihi);
        	force->bounds(arg[1],atom->ntypes,jlo,jhi);
        	numG = atoi(arg[2]);
        	iG = 0;
        	
        	// Allocate B, C and R arrays
			count = 0;
			for (i = ilo; i <= ihi; i++) {
				for (j = MAX(jlo,i); j <= jhi; j++) {
				  par[i][j].ng = numG;
				  if (!par[i][j].B) {
					par[i][j].B = new double[numG];
					par[i][j].C = new double[numG];
					par[i][j].R = new double[numG];
				  }
				  
				  count++;
				}
			}
			if (count == 0) error->all("Pair_style Ex/Gauss/Coul/Cut: Incorrect atom type indexes for Gauss coefficients");

        	file_state=FS_GAUSS_BCR;
        	
        	fprintf(screen, ">>> %d %d %d %d\n", ilo, ihi, jlo, jhi);
	        fprintf(screen, ">>> %s %s %d %d\n", arg[0], arg[1], numG, atom->ntypes);
		} else if (file_state==FS_GAUSS_BCR) {
			if (numG<=0) error->all("Pair_style Ex/Gauss/Coul/Cut: Error reading coefficient file");
			
			B_val = atof(arg[0]);
			C_val = atof(arg[1]);
			R_val = atof(arg[2]);
			
			for (i = ilo; i <= ihi; i++) {
    	    	for (j = MAX(jlo,i); j <= jhi; j++) {
	              par[i][j].B[iG] = B_val;
    	    	  par[i][j].C[iG] = C_val;
			      par[i][j].R[iG] = R_val;
		    	}
		    }
			
			numG--;
			iG++;
			if (numG==0) file_state=FS_GAUSS;
		}
		
    	break;
    case FS_EXCL:
    	narg = 0;
		arg[narg] = strtok(line," \t\n");
		while ( arg[narg]!=NULL ) {
			narg++;
			if (narg>4) break;
			arg[narg] = strtok(NULL," \t\n");
		}
		if (narg!=4) error->all("Pair_style Ex/Gauss/Coul/Cut: Wrong format in coefficient file (Excluded Volume coeff).");
		fprintf(screen, "EX narg=%d\n", narg);
    	
    	force->bounds(arg[0],atom->ntypes,ilo,ihi);
        force->bounds(arg[1],atom->ntypes,jlo,jhi);
        
        A_val = atof(arg[2]);
        l_val = atof(arg[3]);
        
        count = 0;
		for (i = ilo; i <= ihi; i++) {
			for (j = MAX(jlo,i); j <= jhi; j++) {
				par[i][j].ex_flag = true;
				par[i][j].A = A_val;
				par[i][j].l = l_val;
			}
		}
    	
    	break;
    case FS_NONE:
    	if (strcmp(line, "[Gauss]")==0)
			file_state = FS_GAUSS;
		else if (strcmp(line, "[Excluded_Volume]")==0)
			file_state = FS_EXCL;
    	break;
    }
  }
  
  fprintf(screen, "\nEL_FLAG=%d\n\n", el_flag);
  
  for (i = 1; i <= atom->ntypes; i++) {
      for (j = 1; j <= atom->ntypes; j++) {
      		fprintf(screen, "> %d %d\n", i, j);
      		fprintf(screen, "A=%f l=%d ng=%d\n", par[i][j].A, par[i][j].l, par[i][j].ng);
      		for (int k=0;k<par[i][j].ng;k++) {
      			fprintf(screen, "%f %f %f\n", par[i][j].B[k], par[i][j].C[k], par[i][j].R[k]);
      		}
      		fprintf(screen, "\n");
      }
  }
}

/*void PairExGaussCoulCut::read_parameters()
{
  // Read parameter file
  
  fprintf(screen, "\nREAD_PARAMETERS\n");
  
  read_par_flag = false;
  
  int numG, i, j, k, count;
  char line[255], ity[10], jty[10];
  int ilo,ihi,jlo,jhi;
  double A_val,B_val,C_val,R_val,l_val;
  
  ifstream in(parfile);
  if (!in) error->all("Pair_style Ex/Gauss/Coul/Cut: Coefficient file was not found!");
  while(!in.eof()) {
    in >> line;
    fprintf(screen, "LINE: %s", line);
    if(strcmp(line,"[Gauss]")==0) {
      in.ignore(255,'\n');
      while(in.peek() != '\n' && !in.eof()) {
        if(in.peek() == '#') in.ignore(255,'\n');
        
        numG = 0;
        in >> ity >> jty >> numG;
        force->bounds(ity,atom->ntypes,ilo,ihi);
        force->bounds(jty,atom->ntypes,jlo,jhi);
        fprintf(screen, ">>> %d %d %d %d\n", ilo, ihi, jlo, jhi);
        fprintf(screen, ">>> %s %s %d %d\n", ity, jty, numG, atom->ntypes);
        
        // Allocate B, C and R arrays
        count = 0;
  		for (i = ilo; i <= ihi; i++) {
    		for (j = MAX(jlo,i); j <= jhi; j++) {
    		  par[i][j].ng = numG;
    		  if (!par[i][j].B) {
    		  	par[i][j].B = new double[numG];
    		  	par[i][j].C = new double[numG];
    		  	par[i][j].R = new double[numG];
    		  }
    		  
		      count++;
	    	}
  		}
		if (count == 0) error->all("Incorrect args for pair coefficients1");
		
		// Read B, C and R values
		for(k=0;k<numG;k++) {
          in >> B_val >> C_val >> R_val;
          in.ignore(255,'\n');
	      for (i = ilo; i <= ihi; i++) {
    	    for (j = MAX(jlo,i); j <= jhi; j++) {
              par[i][j].B[k] = B_val;
        	  par[i][j].C[k] = C_val;
		      par[i][j].R[k] = R_val;
		    }
		  }
        }
      }
    } else if(strcmp(line,"[Excluded_Volume]")==0) {
      in.ignore(255,'\n');
      while(in.peek() != '\n' && !in.eof()) {
        if(in.peek() == '#') in.ignore(255,'\n');
        in >> ity >> jty >> A_val >> l_val;
        in.ignore(255,'\n');
        
        // Read Excluded Volume parameters
        in >> B_val >> C_val >> R_val;
        in.ignore(255,'\n');
        
        for (i = ilo; i <= ihi; i++) {
    	  for (j = MAX(jlo,i); j <= jhi; j++) {
            par[i][j].ex_flag = true;    	  
            par[i][j].A = A_val;
  	        par[i][j].l = l_val;
          }
        }    
      }
    } else if(line[0] == '#') {
      in.ignore(255,'\n'); //skipping everything on a line after #
    }
  }
  in.close();
  
  for (i = 1; i <= atom->ntypes; i++) {
      for (j = 1; j <= atom->ntypes; j++) {
      		fprintf(screen, "> %d %d\n", i, j);
      		fprintf(screen, "A=%f l=%d ng=%d\n", par[i][j].A, par[i][j].l, par[i][j].ng);
      		for (k=0;k<par[i][j].ng;k++) {
      			fprintf(screen, "%f %f %f\n", par[i][j].B[k], par[i][j].C[k], par[i][j].R[k]);
      		}
      		fprintf(screen, "\n");
      }
  }
}*/

/* ----------------------------------------------------------------------
   init specific to this pair style
------------------------------------------------------------------------- */

void PairExGaussCoulCut::init_style()
{
  fprintf(screen, "\nINIT_STYLE\n");

  if (!atom->q_flag)
    error->all("Pair style ex/gauss/coul/cut requires atom attribute q");

  int irequest = neighbor->request(this);
}

/* ----------------------------------------------------------------------
   init for one type pair i,j and corresponding j,i
------------------------------------------------------------------------- */

double PairExGaussCoulCut::init_one(int i, int j)
{
  fprintf(screen, "\nINIT_ONE\n");

  if (setflag[i][j] == 0) {
//    epsilon[i][j] = mix_energy(epsilon[i][i],epsilon[j][j],
	//		       sigma[i][i],sigma[j][j]);
//    sigma[i][j] = mix_distance(sigma[i][i],sigma[j][j]);
    cut_ex[i][j] = mix_distance(cut_ex[i][i],cut_ex[j][j]);
    cut_coul[i][j] = mix_distance(cut_coul[i][i],cut_coul[j][j]);
  }

  double cut = MAX(cut_ex[i][j],cut_coul[i][j]);
  cut_exsq[i][j] = cut_ex[i][j] * cut_ex[i][j];
  cut_coulsq[i][j] = cut_coul[i][j] * cut_coul[i][j];
    
  if (offset_flag) {
    double ratio = par[i][j].A / cut_ex[i][j];
    offset[i][j] = pow(ratio,par[i][j].l);
//    offset[i][j] = epsilon[i][j] * (pow(ratio,12.0) - pow(ratio,6.0));
  } else offset[i][j] = 0.0;
  
  cut_exsq[j][i] = cut_exsq[i][j];
  cut_coulsq[j][i] = cut_coulsq[i][j];
  offset[j][i] = offset[i][j];
//  par[j][i] = par[i][j]

  // compute I,J contribution to long-range tail correction
  // count total # of atoms of type I and J via Allreduce

/*  if (tail_flag) {
    int *type = atom->type;
    int nlocal = atom->nlocal;

    double count[2],all[2];
    count[0] = count[1] = 0.0;
    for (int k = 0; k < nlocal; k++) {
      if (type[k] == i) count[0] += 1.0;
      if (type[k] == j) count[1] += 1.0;
    }
    MPI_Allreduce(count,all,2,MPI_DOUBLE,MPI_SUM,world);
        
    double PI = 4.0*atan(1.0);
    double sig2 = sigma[i][j]*sigma[i][j];
    double sig6 = sig2*sig2*sig2;
    double rc3 = cut_lj[i][j]*cut_lj[i][j]*cut_lj[i][j];
    double rc6 = rc3*rc3;
    double rc9 = rc3*rc6;
    etail_ij = 8.0*PI*all[0]*all[1]*epsilon[i][j] * 
      sig6 * (sig6 - 3.0*rc6) / (9.0*rc9); 
    ptail_ij = 16.0*PI*all[0]*all[1]*epsilon[i][j] * 
      sig6 * (2.0*sig6 - 3.0*rc6) / (9.0*rc9); 
  } */

  return cut;
}

/* ----------------------------------------------------------------------
  proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairExGaussCoulCut::write_restart(FILE *fp)
{
  write_restart_settings(fp);

  int i,j;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      fwrite(&setflag[i][j],sizeof(int),1,fp);
      if (setflag[i][j]) {
//	fwrite(&epsilon[i][j],sizeof(double),1,fp);
//	fwrite(&sigma[i][j],sizeof(double),1,fp);
	fwrite(&cut_ex[i][j],sizeof(double),1,fp);
	fwrite(&cut_coul[i][j],sizeof(double),1,fp);
      }
    }
}

/* ----------------------------------------------------------------------
  proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairExGaussCoulCut::read_restart(FILE *fp)
{
  read_restart_settings(fp);
  allocate();

  int i,j;
  int me = comm->me;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      if (me == 0) fread(&setflag[i][j],sizeof(int),1,fp);
      MPI_Bcast(&setflag[i][j],1,MPI_INT,0,world);
      if (setflag[i][j]) {
	if (me == 0) {
//	  fread(&epsilon[i][j],sizeof(double),1,fp);
//	  fread(&sigma[i][j],sizeof(double),1,fp);
	  fread(&cut_ex[i][j],sizeof(double),1,fp);
	  fread(&cut_coul[i][j],sizeof(double),1,fp);
	}
//	MPI_Bcast(&epsilon[i][j],1,MPI_DOUBLE,0,world);
//	MPI_Bcast(&sigma[i][j],1,MPI_DOUBLE,0,world);
	MPI_Bcast(&cut_ex[i][j],1,MPI_DOUBLE,0,world);
	MPI_Bcast(&cut_coul[i][j],1,MPI_DOUBLE,0,world);
      }
    }
}

/* ----------------------------------------------------------------------
  proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairExGaussCoulCut::write_restart_settings(FILE *fp)
{
  fwrite(&cut_ex_global,sizeof(double),1,fp);
  fwrite(&cut_coul_global,sizeof(double),1,fp);
  fwrite(&offset_flag,sizeof(int),1,fp);
  fwrite(&mix_flag,sizeof(int),1,fp);
}

/* ----------------------------------------------------------------------
  proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairExGaussCoulCut::read_restart_settings(FILE *fp)
{
  if (comm->me == 0) {
    fread(&cut_ex_global,sizeof(double),1,fp);
    fread(&cut_coul_global,sizeof(double),1,fp);
    fread(&offset_flag,sizeof(int),1,fp);
    fread(&mix_flag,sizeof(int),1,fp);
  }
  MPI_Bcast(&cut_ex_global,1,MPI_DOUBLE,0,world);
  MPI_Bcast(&cut_coul_global,1,MPI_DOUBLE,0,world);
  MPI_Bcast(&offset_flag,1,MPI_INT,0,world);
  MPI_Bcast(&mix_flag,1,MPI_INT,0,world);
}

/* ---------------------------------------------------------------------- */

double PairExGaussCoulCut::single(int i, int j, int itype, int jtype,
				double rsq,
				double factor_coul, double factor_ex,
				double &fforce)
{
  int k;
  double r,rinv,r2inv,rlinv,force_coul,force_ex,force_gauss,phicoul,phiex;
  parameters &pij = par[itype][jtype];

  r2inv = 1.0/rsq;
  rinv = sqrt(r2inv);
  if (el_flag && rsq < cut_coulsq[itype][jtype])
    force_coul = force->qqrd2e * atom->q[i]*atom->q[j]*rinv;
  else force_coul = 0.0;
  if (pij.ex_flag && rsq < cut_exsq[itype][jtype]) {
    rlinv = pow(rinv,pij.l);
    force_ex = pij.l*pij.A*rlinv;
  } else force_ex = 0.0;
  if (pij.ng>0 && rsq < cut_exsq[itype][jtype]) {
  	force_gauss = 0.0;
  	r = sqrt(rsq);
  	for (k=0;k<pij.ng;++k) {
  		force_gauss += 2*pij.C[k]*pij.B[k]*(r-pij.R[k])*exp(-pij.C[k]*(r-pij.R[k])*(r-pij.R[k]));
  	}
  } else force_gauss = 0.0;
  fforce = (factor_coul*force_coul + factor_ex*force_ex) * r2inv + factor_ex*force_gauss * rinv;

  double eng = 0.0;
  if (el_flag && rsq < cut_coulsq[itype][jtype]) {
    phicoul = force->qqrd2e * atom->q[i]*atom->q[j]*rinv;
    eng += factor_coul*phicoul;
  }
  if (pij.ex_flag && rsq < cut_exsq[itype][jtype]) {
    phiex = pij.A*rlinv - offset[itype][jtype];
    eng += factor_ex*phiex;
  }
  if (pij.ng>0 && rsq < cut_exsq[itype][jtype]) {
  	for (k=0;k<pij.ng;++k) {
  		eng += pij.B[k]*exp(-pij.C[k]*(r-pij.R[k])*(r-pij.R[k])) * factor_ex;
  	}
  }

  return eng;
}