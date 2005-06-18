/******************************************************************
  This file is a part of eco: R Package for Estimating Fitting 
  Bayesian Models of Ecological Inference for 2X2 tables
  by Ying Lu and Kosuke Imai
  Copyright: GPL version 2 or later.
*******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <Rmath.h>
#include <R_ext/Utils.h>
#include <R.h>
#include "vector.h"
#include "subroutines.h"
#include "rand.h"


/* Grid method samping from tomography line*/
void rGrid(
	   double *Sample,         /* W_i sampled from each tomography line */                 
	   double *W1gi,           /* The grid lines of W1[i] */
	   double *W2gi,           /* The grid lines of W2[i] */
	   int ni_grid,            /* number of grids for observation i*/
	   double *mu,             /* mean vector for normal */ 
	   double **InvSigma,      /* Inverse covariance matrix for normal */
	   int n_dim)              /* dimension of parameters */
{
  int j;
  double dtemp;
  double *vtemp=doubleArray(n_dim);
  double *prob_grid=doubleArray(ni_grid);     /* density by grid */
  double *prob_grid_cum=doubleArray(ni_grid); /* cumulative density by grid */
    
  dtemp=0;
  for (j=0;j<ni_grid;j++){
    vtemp[0]=log(W1gi[j])-log(1-W1gi[j]);
    vtemp[1]=log(W2gi[j])-log(1-W2gi[j]);
    prob_grid[j]=dMVN(vtemp, mu, InvSigma, n_dim, 1) -
      log(W1gi[j])-log(W2gi[j])-log(1-W1gi[j])-log(1-W2gi[j]);
    prob_grid[j]=exp(prob_grid[j]);
    dtemp+=prob_grid[j];
    prob_grid_cum[j]=dtemp;
  }
  for (j=0;j<ni_grid;j++)
    prob_grid_cum[j]/=dtemp; /*standardize prob.grid */

  /*2 sample W_i on the ith tomo line */
  j=0;
  dtemp=unif_rand();
  while (dtemp > prob_grid_cum[j]) j++;
  Sample[0]=W1gi[j];
  Sample[1]=W2gi[j];

  free(vtemp);
  free(prob_grid);
  free(prob_grid_cum);

}

/* sample W via MH for 2x2 table */
void rMH(
	 double *W,              /* previous draws */
	 double *XY,             /* X_i and Y_i */
	 double W1min,           /* lower bound for W1 */
	 double W1max,           /* upper bound for W1 */
	 double *mu,            /* mean vector for normal */ 
	 double **InvSigma,     /* Inverse covariance matrix for normal */
	 int n_dim)              /* dimension of parameters */
{
  int j;
  double dens1, dens2, ratio;
  double *Sample=doubleArray(n_dim);
  double *vtemp=doubleArray(n_dim);
  double *vtemp1=doubleArray(n_dim);
  
  /* draw Sample[0] (W_1) from unif(W1min, W1max) */
  Sample[0]=W1min+unif_rand()*(W1max-W1min);
  Sample[1]=XY[1]/(1-XY[0])-Sample[0]*XY[0]/(1-XY[0]);
  for (j=0; j<n_dim; j++) {
    vtemp[j]=log(Sample[j])-log(1-Sample[j]);
    vtemp1[j]=log(W[j])-log(1-W[j]);
  }
  /* acceptance ratio */
  dens1 = dMVN(vtemp, mu, InvSigma, n_dim, 1) -
    log(Sample[0])-log(Sample[1])-log(1-Sample[0])-log(1-Sample[1]);
  dens2 = dMVN(vtemp1, mu, InvSigma, n_dim, 1) -
    log(W[0])-log(W[1])-log(1-W[0])-log(1-W[1]);
  ratio=fmin2(1, exp(dens1-dens2));
  
  /* accept */
  if (unif_rand() < ratio) 
    for (j=0; j<n_dim; j++) 
      W[j]=Sample[j];
  
  free(Sample);
  free(vtemp);
  free(vtemp1);
}


/* sample W via MH for RxC table */
void rMHrc(
	   double *Sample,         /* sample of W_i */                 
	   double *W,              /* previous draws */
	   double *XY,             /* X_i and Y_i */
	   double *Zmin,            /* lower bound for Z */
	   double *Zmax,            /* upper bound for Z */
	   double *mu,            /* mean vector for normal */ 
	   double **InvSigma,     /* Inverse covariance matrix for normal */
	   int n_dim)              /* dimension of parameters */
{
  int j, exceed=1;
  double dens1, dens2, ratio;
  double *param=doubleArray(n_dim);
  double *vtemp=doubleArray(n_dim);
  double *vtemp1=doubleArray(n_dim);
  
  /* set Dirichlet parameter to 1 */
  for (j=0; j<n_dim; j++)
    param[j] = 1.0;

  /* Sample a candidate draw for W */
  while (exceed > 0) {
    exceed = 0;
    rDirich(vtemp, param, n_dim);
    for (j=0; j<n_dim; j++) 
      if (vtemp[j] > Zmax[j] || vtemp[j] < Zmin[j])
	exceed++;
  }

  /* calcualte W and its logit transformation */
  for (j=0; j<n_dim; j++) {
    Sample[j]=vtemp[j]*XY[0]/XY[j+1];
    vtemp[j]=log(Sample[j])-log(1-Sample[j]);
    vtemp1[j]=log(W[j])-log(1-W[j]);
  }
  
  /* acceptance ratio */
  dens1 = dMVN(vtemp, mu, InvSigma, n_dim, 1);
  dens2 = dMVN(vtemp1, mu, InvSigma, n_dim, 1);
  for (j=0; j<n_dim; j++) {
    dens1 -= (log(Sample[j])+log(1-Sample[j]));
    dens2 -= (log(W[j])+log(1-W[j]));
  }
  ratio=fmin2(1, exp(dens1-dens2));
  
  /* reject */
  if (ratio < unif_rand()) 
    for (j=0; j<n_dim; j++)
      Sample[j]=W[j];
  
  free(param);
  free(vtemp);
  free(vtemp1);
}

/* Normal-InvWishart updating 
     Y|mu, Sigma ~ N(mu, Sigma) 
        mu|Sigma ~ N(mu0, Sigma/tau0) 
           Sigma ~ InvWish(nu0, S0^{-1}) */
void NIWupdate(
	       double **Y,         /* data */
	       double *mu,         /* mean */
	       double **Sigma,     /* variance */
	       double **InvSigma,  /* precision */
	       double *mu0,        /* prior mean */
	       double tau0,        /* prior scale */
	       int nu0,            /* prior df */
	       double **S0,        /* prior scale */
	       int n_samp,         /* sample size */
	       int n_dim)          /* dimension */
{
  int i,j,k;
  double *Ybar = doubleArray(n_dim);
  double *mun = doubleArray(n_dim);
  double **Sn = doubleMatrix(n_dim, n_dim);
  double **mtemp = doubleMatrix(n_dim, n_dim);

  for (j=0; j<n_dim; j++) {
    Ybar[j] = 0;
    for (i=0; i<n_samp; i++)
      Ybar[j] += Y[i][j];
    Ybar[j] /= n_samp;
    for (k=0; k<n_dim; k++)
      Sn[j][k] = S0[j][k];
  }
  for (j=0; j<n_dim; j++) {
    mun[j] = (tau0*mu0[j]+n_samp*Ybar[j])/(tau0+n_samp);
    for (k=0; k<n_dim; k++) {
      Sn[j][k] += (tau0*n_samp)*(Ybar[j]-mu0[j])*(Ybar[k]-mu0[k])/(tau0+n_samp);
      for (i=0; i<n_samp; i++)
	Sn[j][k] += (Y[i][j]-Ybar[j])*(Y[i][k]-Ybar[k]);
      /* conditioning on mu:
	 Sn[j][k]+=tau0*(mu[j]-mu0[j])*(mu[k]-mu0[k]); 
	 Sn[j][k]+=(Y[i][j]-mu[j])*(Y[i][k]-mu[k]); */
    }
  }
  dinv(Sn, n_dim, mtemp);
  rWish(InvSigma, mtemp, nu0+n_samp, n_dim);
  dinv(InvSigma, n_dim, Sigma);
 
  for (j=0; j<n_dim; j++)
    for (k=0; k<n_dim; k++)
      mtemp[j][k] = Sigma[j][k]/(tau0+n_samp);
  rMVN(mu, mun, mtemp, n_dim);

  free(Ybar);
  free(mun);
  FreeMatrix(Sn, n_dim);
  FreeMatrix(mtemp, n_dim);
}
