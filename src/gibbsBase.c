#include <stddef.h>
#include <stdio.h>      
#include <math.h>
#include <Rmath.h>
#include <R_ext/Utils.h>
#include "vector.h"
#include "subroutines.h"
#include "rand.h"

void cBaseeco(
	      /*data input */
	      double *pdX,     /* data (X, Y) */
	      int *pin_samp,   /* sample size */
	      /*MCMC draws */
	      int *n_gen,      /* number of gibbs draws */
	      int *burn_in,    /* number of draws to be burned in */
	      int *pinth,        /* keep every nth draw */
	      int *verbose,    /* 1 for output monitoring */
	      /* prior specification*/
	      int *pinu0,      /* prior df parameter for InvWish */
	      double *pdtau0,  /* prior scale parameter for Sigma under G0*/
	      double *mu0,     /* prior mean for mu under G0 */
	      double *pdS0,    /* prior scale for Sigma */

	      /*incorporating survey data */
	      int *survey,      /*1 if survey data available (set of W_1, W_2) */
	                       /*0 not*/
	      int *sur_samp,     /*sample size of survey data*/
	      double *sur_W,    /*set of known W_1, W_2 */ 
				  
	      /*incorporating homeogenous areas */
	      int *x1,       /* 1 if X=1 type areas available W_1 known, W_2 unknown */
	      int *sampx1,  /* number X=1 type areas */
	      double *x1_W1, /* values of W_1 for X1 type areas */

	      int *x0,       /* 1 if X=0 type areas available W_2 known, W_1 unknown */
	      int *sampx0,  /* number X=0 type areas */
	      double *x0_W2, /* values of W_2 for X0 type areas */

	      /* storage */
	      int *pred,       /* 1 if draw posterior prediction */
	      int *parameter,   /* 1 if save population parameter */

	      /* storage for Gibbs draws of mu/sigmat*/
	      double *pdSMu0, double *pdSMu1, 
	      double *pdSSig00, double *pdSSig01, double *pdSSig11,           
	      /* storage for Gibbs draws of W*/
	      double *pdSW1, double *pdSW2,
	      /* storage for posterior predictions of W */
	      double *pdSWt1, double *pdSWt2

	      ){	   
  
  int n_samp = *pin_samp;    /* sample size */
  int nu0 = *pinu0;          /* prior parameters */ 
  double tau0 = *pdtau0;   
  int nth=*pinth;  

  int keep=1;            /* keeps every #num draw */ 
  int n_cov=2;           /* The number of covariates */

  double **X;	    	 /* The Y and covariates */
  double **S0;           /* The prior S parameter for InvWish */
  
  int s_samp= *sur_samp;   /* sample size of survey data */ 
  double **S_W;            /*The known W1 and W2 matrix*/
  double **S_Wstar;        /*The inverse logit transformation of S_W*/

  int x1_samp=*sampx1;
  int x0_samp=*sampx0;

  int t_samp;              /* total effective sample size =n_samp+s_samp;*/

  /*bounds condition variables */
  double **W;            /* The W1 and W2 matrix */
  double *minW1, *maxW1; /* The lower and upper bounds of W_1i */
  int n_step=1000;    /* 1/The default size of grid step */  
  int *n_grid;           /* The number of grids for sampling on tomoline */
  double **W1g, **W2g;   /* The grids taken for W1 and W2 on tomoline */
  double *prob_grid;     /* The projected density on tomoline */
  double *prob_grid_cum; /* The projected cumulative density on tomoline */
  double *resid;         /* The centralizing vector for grids */

  /* ordinary model variables */
  double **Sigma_ord;   /* The posterior covariance matrix of psi (oridinary)*/
  double **InvSigma_ord;
  double *mu_ord;        /* The posterior mean of psi (ordinary)*/

  double **Wstar;        /* The pseudo data  */
  double *Wstar_bar;

  /*posterior variables */
  double *mun;           /* The posterior mean */
  double **Sn;           /* The posterior S parameter for InvWish */

  /* misc variables */
  int i, j, k, l, main_loop;   /* used for various loops */
  int itemp, itempS, itempC, itempA;
  double dtemp, dtemp1;
  double *vtemp;
  double **mtemp;

  /* get random seed */
  GetRNGstate();

  /* defining vectors and matricies */
  /* data */
  X=doubleMatrix(n_samp,n_cov);
  W=doubleMatrix((n_samp+s_samp+x0_samp+x1_samp),n_cov);
  Wstar=doubleMatrix((n_samp+s_samp+x0_samp+x1_samp),n_cov);

  S_W=doubleMatrix(s_samp, n_cov);
  S_Wstar=doubleMatrix(s_samp, n_cov);


  /* bounds */
  minW1=doubleArray(n_samp);
  maxW1=doubleArray(n_samp);
  n_grid=intArray(n_samp);
  resid=doubleArray(n_samp);

  /*priors*/
  S0=doubleMatrix(n_cov,n_cov);

  /*posteriors*/
  mun=doubleArray(n_cov);
  Sn=doubleMatrix(n_cov,n_cov);

  /*bounds condition */
  W1g=doubleMatrix(n_samp, n_step);
  W2g=doubleMatrix(n_samp, n_step);
  prob_grid=doubleArray(n_step);
  prob_grid_cum=doubleArray(n_step);

  /*ordinary model */
  mu_ord=doubleArray(n_cov);
  Sigma_ord=doubleMatrix(n_cov,n_cov);
  InvSigma_ord=doubleMatrix(n_cov,n_cov);

  Wstar_bar=doubleArray(n_cov);

  vtemp=doubleArray(n_cov);
  mtemp=doubleMatrix(n_cov,n_cov);

  /* priors under G0*/
  itemp=0;
  for(k=0;k<n_cov;k++)
    for(j=0;j<n_cov;j++) S0[j][k]=pdS0[itemp++];

  t_samp=n_samp+s_samp+x1_samp+x0_samp;  



  /* read the data set */
  /** Packing Y, X  **/
  itemp = 0;
  for (j = 0; j < n_cov; j++) 
    for (i = 0; i < n_samp; i++) {
      X[i][j] = pdX[itemp++];
    }

  /* initialize W, Wstar for n_samp*/
  for (j=0; j<n_cov; j++)
    for (i=0; i< n_samp; i++) {
      W[i][j]=0;
      Wstar[i][j]=0;
      if (X[i][1]==0) W[i][j]=0.0001;
      else if (X[i][1]==1) W[i][j]=0.9999;

    }


  /*read homeogenous areas information */
  if (*x1==1) 
    for (i=0; i<x1_samp; i++) {
      W[(n_samp+i)][0]=x1_W1[i];
      if (W[(n_samp+i)][0]==0) W[(n_samp+i)][0]=0.0001;
      if (W[(n_samp+i)][0]==1) W[(n_samp+i)][0]=0.9999;
      Wstar[(n_samp+i)][0]=log(W[(n_samp+i)][0])-log(1-W[(n_samp+i)][0]);
    }

  if (*x0==1) 
    for (i=0; i<x0_samp; i++) {
      W[(n_samp+x1_samp+i)][1]=x0_W2[i];
      if (W[(n_samp+x1_samp+i)][1]==0) W[(n_samp+x1_samp+i)][1]=0.0001;
      if (W[(n_samp+x1_samp+i)][1]==1) W[(n_samp+x1_samp+i)][1]=0.9999;
      Wstar[(n_samp+x1_samp+i)][1]=log(W[(n_samp+x1_samp+i)][1])-log(1-W[(n_samp+x1_samp+i)][1]);
    }


  /*read the survey data */

  if (*survey==1) {
    itemp = 0;
    for (j=0; j<n_cov; j++)
      for (i=0; i<s_samp; i++) {
	S_W[i][j]=sur_W[itemp++];
	if (S_W[i][j]==0) S_W[i][j]=0.0001;
	if (S_W[i][j]==1) S_W[i][j]=0.9999;
	S_Wstar[i][j]=log(S_W[i][j])-log(1-S_W[i][j]);
	W[(n_samp+x1_samp+x0_samp+i)][j]=S_W[i][j];
	Wstar[(n_samp+x1_samp+x0_samp+i)][j]=S_Wstar[i][j];
      }
  }

  itempA=0; /* counter for alpha */
  itempS=0; /* counter for storage */
  itempC=0; /* counter to control nth draw */

  /*initialize W and Wstar */

  
  /*initialize W1g and W2g */
  for(i=0; i<n_samp; i++)
    for (j=0; j<n_step; j++){
      W1g[i][j]=0;
      W2g[i][j]=0;
    }

  /*** calculate bounds and grids ***/
  for(i=0;i<n_samp;i++) {
    if (X[i][1]!=0 && X[i][1]!=1) {
      /* min and max for W1 */ 
      minW1[i]=fmax2(0.0, (X[i][0]+X[i][1]-1)/X[i][0]);
      maxW1[i]=fmin2(1.0, X[i][1]/X[i][0]);
      /* number of grid points */
      /* note: 1/n_step is the length of the grid */
      dtemp=(double)1/n_step;
      if ((maxW1[i]-minW1[i]) > (2*dtemp)) { 
	n_grid[i]=ftrunc((maxW1[i]-minW1[i])*n_step);
	resid[i]=(maxW1[i]-minW1[i])-n_grid[i]*dtemp;
	/*if (maxW1[i]-minW1[i]==1) resid[i]=dtemp/4;*/
	j=0; 
	while (j<n_grid[i]) {
	  W1g[i][j]=minW1[i]+(j+1)*dtemp-(dtemp+resid[i])/2;
	  if ((W1g[i][j]-minW1[i])<resid[i]/2) W1g[i][j]+=resid[i]/2;
	  if ((maxW1[i]-W1g[i][j])<resid[i]/2) W1g[i][j]-=resid[i]/2;
	  W2g[i][j]=(X[i][1]-X[i][0]*W1g[i][j])/(1-X[i][0]);
	  /*if (i<20) printf("\n%5d%5d%14g%14g", i, j, W1g[i][j], W2g[i][j]);*/
	  j++;
	}
      }
      else {
	W1g[i][0]=minW1[i]+(maxW1[i]-minW1[i])/3;
	W2g[i][0]=(X[i][1]-X[i][0]*W1g[i][0])/(1-X[i][0]);
	W1g[i][1]=minW1[i]+2*(maxW1[i]-minW1[i])/3;
	W2g[i][1]=(X[i][1]-X[i][0]*W1g[i][1])/(1-X[i][0]);
	n_grid[i]=2;
	
      }
      /*    if (i<n_samp){
	    printf("grids\n");
	    printf("minW1 maxW1 resid\n");
	    printf("%5d%14g%14g%14g\n", i, minW1[i], maxW1[i], resid[i]);
	    for (j=0;j<n_grid[i];j++){
	    if (j<5 | j>(n_grid[i]-5))
	    printf("%5d%5d%14g%14g\n", i, j, W1g[i][j], W2g[i][j]);
	    }
	    }*/
    }
  }
    
  /* initialize vales of mu_ord and Sigma_ord */
  for(j=0;j<n_cov;j++){
    mu_ord[j]=mu0[j];
    for(k=0;k<n_cov;k++)
      Sigma_ord[j][k]=S0[j][k];
  }
  dinv(Sigma_ord, n_cov, InvSigma_ord);
  
  /***Gibbs for  normal prior ***/
  for(main_loop=0; main_loop<*n_gen; main_loop++){
    /**update W, Wstar given mu, Sigma in regular areas**/
    for (i=0;i<n_samp;i++){
      if ( X[i][1]!=0 && X[i][1]!=1 ) {
	/*1 project BVN(mu_ord, Sigma_ord) on the inth tomo line */
	dtemp=0;
	for (j=0;j<n_grid[i];j++){
	  /*  if (*link==1){ */
	    vtemp[0]=log(W1g[i][j])-log(1-W1g[i][j]);
	    vtemp[1]=log(W2g[i][j])-log(1-W2g[i][j]);
	    prob_grid[j]=dMVN(vtemp, mu_ord, InvSigma_ord, 2, 1) -
	      log(W1g[i][j])-log(W2g[i][j])-log(1-W1g[i][j])-log(1-W2g[i][j]);
	    /* }
	  else if (*link==2){
	    vtemp[0]=qnorm(W1g[i][j], 0, 1, 1, 0);
	    vtemp[1]=qnorm(W2g[i][j], 0, 1, 1, 0);
	    prob_grid[j]=dMVN(vtemp, mu_ord, InvSigma_ord, 2, 1) -
	      dnorm(vtemp[0], 0, 1, 1)-dnorm(vtemp[1], 0, 1, 1);
	  }
	  else if (*link==3) {
	    vtemp[0]=-log(-log(W1g[i][j]));
	    vtemp[1]=-log(-log(W2g[i][j]));
	    prob_grid[j]=dMVN(vtemp, mu_ord, InvSigma_ord, 2, 1) -
	      log(W1g[i][j])-log(W2g[i][j])-log(-log(W1g[i][j]))-log(-log(W2g[i][j])); 
	      }*/
	  prob_grid[j]=exp(prob_grid[j]);
	  dtemp+=prob_grid[j];
	  prob_grid_cum[j]=dtemp;
	}
	for (j=0;j<n_grid[i];j++)
	  prob_grid_cum[j]/=dtemp; /*standardize prob.grid */ 
	
	/*2 sample W_i on the ith tomo line */
	/*3 compute Wsta_i from W_i*/
	j=0;
	dtemp=unif_rand();
	while (dtemp > prob_grid_cum[j]) j++;
	W[i][0]=W1g[i][j];
	W[i][1]=W2g[i][j];
      } /* end of *1 */
      /*   if (*link==1) {*/
	Wstar[i][0]=log(W[i][0])-log(1-W[i][0]);
	Wstar[i][1]=log(W[i][1])-log(1-W[i][1]);
	/* }
      else if (*link==2) {
	Wstar[i][0]=qnorm(W[i][0],0 ,1, 1, 0);
	Wstar[i][1]=qnorm(W[i][1],0 ,1, 1, 0);
      }
      else if (*link==3) {
	Wstar[i][0]=-log(-log(W[i][0]));
	Wstar[i][1]=-log(-log(W[i][1]));
	}*/
    }

    
    /*update W2 given W1, mu_ord and Sigma_ord in x1 homeogeneous areas */
    /*printf("W2 draws\n");*/
    if (*x1==1)
      for (i=0; i<x1_samp; i++) {
	dtemp=mu_ord[1]+Sigma_ord[0][1]/Sigma_ord[0][0]*(Wstar[n_samp+i][0]-mu_ord[0]);
	dtemp1=Sigma_ord[1][1]*(1-Sigma_ord[0][1]*Sigma_ord[0][1]/(Sigma_ord[0][0]*Sigma_ord[1][1]));
	/* printf("\n%14g%14g\n", dtemp, dtemp1);*/
	dtemp1=sqrt(dtemp1);
	Wstar[n_samp+i][1]=rnorm(dtemp, dtemp1);
	W[n_samp+i][1]=exp(Wstar[n_samp+i][1])/(1+exp(Wstar[n_samp+i][1]));
	/* printf("\n%5d%14g%14g\n", i, Wstar[n_samp+i][1], W[n_samp+i][1]);*/ 
      }
    
    /*update W1 given W2, mu_ord and Sigma_ord in x0 homeogeneous areas */
    /*printf("W1 draws\n");*/
    if (*x0==1)
      for (i=0; i<x0_samp; i++) {
	dtemp=mu_ord[0]+Sigma_ord[0][1]/Sigma_ord[1][1]*(Wstar[n_samp+x1_samp+i][1]-mu_ord[1]);
	dtemp1=Sigma_ord[0][0]*(1-Sigma_ord[0][1]*Sigma_ord[0][1]/(Sigma_ord[0][0]*Sigma_ord[1][1]));
	/* printf("\n%14g%14g\n", dtemp, dtemp1);*/
	dtemp1=sqrt(dtemp1);
	Wstar[n_samp+x1_samp+i][0]=rnorm(dtemp, dtemp1);
	W[n_samp+x1_samp+i][0]=exp(Wstar[n_samp+x1_samp+i][0])/(1+exp(Wstar[n_samp+x1_samp+i][0]));
	/*printf("\n%5d%14g%14g\n", i, Wstar[n_samp+x1_samp+i][0], W[n_samp+x1_samp+i][0]);*/ 
      }
    
    /*        printf("\n W data Wstar data \n");
      for (i=0; i<t_samp;i++)
	printf("\n%14g%14g%14g%14g", W[i][0], W[i][1], Wstar[i][0], Wstar[i][1]);
    */


    /*update mu_ord, Sigma_ord given wstar using effective sample of Wstar*/
    for (j=0;j<n_cov;j++) {
      Wstar_bar[j]=0;
      for (k=0;k<n_cov;k++)
	Sn[j][k]=S0[j][k];
    }



    for (j=0;j<n_cov;j++) 
      for (i=0;i<t_samp;i++)
	Wstar_bar[j]+=Wstar[i][j]/t_samp;
      

    for (j=0;j<n_cov;j++)
      for (k=0;k<n_cov;k++)
	for (i=0;i<t_samp;i++)
	  Sn[j][k]+=(Wstar[i][j]-Wstar_bar[j])*(Wstar[i][k]-Wstar_bar[k]);
      
      for (j=0;j<n_cov;j++){
	mun[j]=(tau0*mu0[j]+t_samp*Wstar_bar[j])/(tau0+t_samp);
	for (k=0;k<n_cov;k++)
	  Sn[j][k]+=(tau0*t_samp)*(Wstar_bar[j]-mu0[j])*(Wstar_bar[k]-mu0[k])/(tau0+t_samp);
      }
      dinv(Sn, n_cov, mtemp); 
      /*    printf("\n mun0  mun1  Sigma00  sigmat01 Sigma11");*/
      
      rWish(InvSigma_ord, mtemp, nu0+t_samp, n_cov);
      dinv(InvSigma_ord, n_cov, Sigma_ord);
      
      for(j=0;j<n_cov;j++)
	for(k=0;k<n_cov;k++) mtemp[j][k]=Sigma_ord[j][k]/(tau0+t_samp);
      
      rMVN(mu_ord, mun, mtemp, n_cov);


     

      /*    printf("\n%5d%14g%14g%14g%14g%14g", main_loop, mu_ord[0], mu_ord[1], Sigma_ord[0][0], Sigma_ord[0][1], Sigma_ord[1][1]); */
  
    /*store Gibbs draw after burn-in and every nth draws */      
    if (main_loop>=*burn_in){
      itempC++;
      if (itempC==nth){
	/*	printf("%5d\n", main_loop);*/
	fflush(stdout);
	pdSMu0[itempA]=mu_ord[0];
	pdSMu1[itempA]=mu_ord[1];
	pdSSig00[itempA]=Sigma_ord[0][0];
	pdSSig01[itempA]=Sigma_ord[0][1];
	pdSSig11[itempA]=Sigma_ord[1][1];
	itempA++;
	for(i=0; i<(n_samp+x1_samp+x0_samp); i++){
	  pdSW1[itempS]=W[i][0];
	  pdSW2[itempS]=W[i][1];
	  /*Wstar prediction */
	  if (*pred) {
	    rMVN(vtemp, mu_ord, Sigma_ord, n_cov);
	    /*  if (*link==1){*/
	      pdSWt1[itempS]=exp(vtemp[0])/(exp(vtemp[0])+1);
	      pdSWt2[itempS]=exp(vtemp[1])/(exp(vtemp[1])+1);
	      /* }
	    else if (*link==2){
	      pdSWt1[itempS]=pnorm(vtemp[0], 0, 1, 1, 0);
	      pdSWt2[itempS]=pnorm(vtemp[1], 0, 1, 1, 0);
	    }
	    else if (*link==3){
	      pdSWt1[itempS]=exp(-exp(-vtemp[0]));
	      pdSWt2[itempS]=exp(-exp(-vtemp[1]));
	      }	*/      
	  }
	  itempS++;
	}
	itempC=0;
      }
    } /*end of stroage *burn_in*/
    if ((*verbose==1) && (ftrunc(main_loop/10000)*10000==main_loop))
      {
        Rprintf("iteration  ");
        Rprintf("%5d\n", main_loop);
	R_FlushConsole();
      }

  } /*end of MCMC for normal */ 



  /** write out the random seed **/
  PutRNGstate();

  /* Freeing the memory */
  FreeMatrix(X, n_samp);
  FreeMatrix(W, t_samp);
  FreeMatrix(Wstar, t_samp);
  free(minW1);
  free(maxW1);
  free(n_grid);
  free(resid);
  FreeMatrix(S0, n_cov);
  FreeMatrix(Sn, n_cov);
  FreeMatrix(W1g, n_samp);
  FreeMatrix(W2g, n_samp);
  free(prob_grid);
  free(prob_grid_cum);
  free(mu_ord);
  FreeMatrix(Sigma_ord,n_cov);
  FreeMatrix(InvSigma_ord, n_cov);
  free(Wstar_bar);
  free(vtemp);
  FreeMatrix(mtemp, n_cov);
  
} /* main */

