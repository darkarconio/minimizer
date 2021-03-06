#include <iostream>
#include <gsl/gsl_multimin.h>
#include <vector>
#include <point.hpp>
#include <fxn.hpp>
#include <atom.hpp>

using namespace std;

#define debug(x) cout << __LINE__ << ' ' << x << endl; cout.flush();

//The value of f, f(x, y, ...)
double my_f (const gsl_vector *v, void *params)
{
   Point a,b;
   Parameters &p = *(Parameters *)params;
   double fxn = 0;
   const vector<int> &pairs = p.pairs();
   
   for (int i=0; i<(p.pnt()*2*p.cxn()); i+=2)
   {
      a.x( gsl_vector_get(v, pairs[i]*3) );
      a.y( gsl_vector_get(v, pairs[i]*3+1) );
      a.z( gsl_vector_get(v, pairs[i]*3+2) );
      b.x( gsl_vector_get(v, pairs[i+1]*3) );
      b.y( gsl_vector_get(v, pairs[i+1]*3+1) );
      b.z( gsl_vector_get(v, pairs[i+1]*3+2) );
      
      if (a == b)
         continue;
      
      fxn += .25*p.k()*pow( ((p.getRealDiff(a,b)).distance() - p.dist()) ,2);
   }
   return fxn;
}

/* The gradient of f, df = (df/dx, df/dy). */
void my_df (const gsl_vector *v, void *params, gsl_vector *df)
{
   Point a,b;
   Point diffAB, diffBA;
   double distAB, distBA;
   Parameters &p = *(Parameters *)params;
   double dfxnX = 0;
   double dfxnY = 0;
   double dfxnZ = 0;
   const vector<int> &pairs = p.pairs();
   int i,j;
   
   for (i=0; i<(p.pnt()*2*p.cxn()); i+=(2*p.cxn()))
   {
      a.x( gsl_vector_get(v, pairs[i]*3) );
      a.y( gsl_vector_get(v, pairs[i]*3+1) );
      a.z( gsl_vector_get(v, pairs[i]*3+2) );
      
      for (j=0; j<p.cxn(); j++)
      {
         b.x( gsl_vector_get(v, pairs[i+1+(j*2)]*3) );
         b.y( gsl_vector_get(v, pairs[i+1+(j*2)]*3+1) );
         b.z( gsl_vector_get(v, pairs[i+1+(j*2)]*3+2) );
         
         if (a == b)
            continue;
         
         diffAB = p.getRealDiff(a,b);
         diffBA = diffAB*(-1); //getRealDiff(b,a,p);
         distAB = diffAB.distance();
         distBA = -distAB; //diffBA.distance();
         
         dfxnX += 2*(diffAB.x())*(distBA-p.dist())/(distBA) - 2*(diffBA.x())*(distAB-p.dist())/(distAB);
         dfxnY += 2*(diffAB.y())*(distBA-p.dist())/(distBA) - 2*(diffBA.y())*(distAB-p.dist())/(distAB);
         dfxnZ += 2*(diffAB.z())*(distBA-p.dist())/(distBA) - 2*(diffBA.z())*(distAB-p.dist())/(distAB);
      }
      gsl_vector_set(df, pairs[i]*3, dfxnX);
      gsl_vector_set(df, pairs[i]*3+1, dfxnY);
      gsl_vector_set(df, pairs[i]*3+2, dfxnZ);
      dfxnX = 0;
      dfxnY = 0;
      dfxnZ = 0;
   }
}

/* Compute both f and df together. */
void my_fdf (const gsl_vector *x, void *params, double *f, gsl_vector *df)
{
   *f = my_f(x, params);
   my_df(x, params, df);
}

double optimizer(vector<Point> & pos, Parameters & p, double stepSize, double tolerance)
{
   size_t iter = 0;
   int status;
   int i;
   size_t maxCycle = 100;
   
   const gsl_multimin_fdfminimizer_type *T;
   gsl_multimin_fdfminimizer *s;

   gsl_vector *x;
   gsl_multimin_function_fdf my_func;
   
   my_func.n = p.var();  /* number of function components */
   my_func.f = &my_f;
   my_func.df = &my_df;
   my_func.fdf = &my_fdf;
   my_func.params = (void *)&p;
   
   /* Starting point, x = (5,7) */
   x = gsl_vector_alloc (p.var());
  
   for (i=0; i<p.var(); i+=3)
   {
      gsl_vector_set (x, i, pos[i/3].x() );
      gsl_vector_set (x, i+1, pos[i/3].y() );
      gsl_vector_set (x, i+2, pos[i/3].z() );
   }
   
   T = gsl_multimin_fdfminimizer_conjugate_fr;
   s = gsl_multimin_fdfminimizer_alloc (T, p.var());
   
   gsl_multimin_fdfminimizer_set (s, &my_func, x, stepSize, tolerance);
   
   do
   {
      iter++;
      status = gsl_multimin_fdfminimizer_iterate (s);
      
      if (status == GSL_ENOPROG)
      {
         cout << "Numerical Error, iteration: " << iter << endl;
         break;
      }
      
      status = gsl_multimin_test_gradient (s->gradient, tolerance);
      
      if (status == GSL_SUCCESS)
      {
         cout << "Minimum found at:\n";
         cout << iter << "  ";
         for (i=0;i<p.pnt();i++)
         {
            pos[i].x( gsl_vector_get (s->x, i*3) );
            pos[i].y( gsl_vector_get (s->x, i*3+1) );
            pos[i].z( gsl_vector_get (s->x, i*3+2) );
            p.checkPeriodBound(pos[i]);
         }
      }
   }
   while (status == GSL_CONTINUE && iter < maxCycle);

   if (iter == maxCycle)
      cout << "Maximum cycles hit: " << iter << endl;

   gsl_multimin_fdfminimizer_free (s);
   gsl_vector_free (x);
  
   return s->f;
}

//Checks if two Points are bonded across cell walls, returns the corrected vector difference between them if they are
//Otherwise, simply returns the difference between them
Point Parameters::getRealDiff (Point & a, Point & b)
{
   Point c = (a-b);
   double sign;
   Point del = c.changeBasis(dim());
//   double xDel = c.scalarProj(dim(0));
//   double yDel = c.scalarProj(dim(1));
//   double zDel = c.scalarProj(dim(2));

   if (del.x() < -1/2 || del.x() > 1/2)
   {
      sign = c.x() < 0 ? 1 : -1;
      c += dim(0)*sign;
   }
   if (del.y() < -1/2 || del.y() > 1/2)
   {
      sign = c.y() < 0 ? 1 : -1;
      c += dim(1)*sign;
   }
   if (del.z() < -1/2 || del.z() > 1/2)
   {
      sign = c.z() < 0 ? 1 : -1;
      c += dim(2)*sign;
   }

   return c;
}

Point Parameters::checkPeriodBound (Point & c)
{
   double sign;
   Point fract = c.changeBasis(dim());
//   double xDel = c.scalarProj(dim(0));
//   double yDel = c.scalarProj(dim(1));
//   double zDel = c.scalarProj(dim(2));

   if (fract.x() < 0 || fract.x() >= 1)
   {
      sign = fract.x() < 0 ? 1 : -1;
      c = (c+(dim(0)*sign));
   }
   if (fract.y() < 0 || fract.y() >= 1)
   {
      sign = fract.y() < 0 ? 1 : -1;
      c = (c+(dim(1)*sign));
   }
   if (fract.z() < 0 || fract.z() >= 1)
   {
      sign = fract.z() < 0 ? 1 : -1;
      c = (c+(dim(2)*sign));
   }
   return c;
}

void Parameters::genBondList(vector<Atom> & atoms)
{
   for (int i=0; i<mpnt; i++)
   {
      for (int j=0; j<mcxn; j++)
      {
         mpairs.resize(mpnt*2*mcxn);
         mpairs[(i*mcxn*2)+(j*2)] = atoms[i].getIndex();
         mpairs[(i*mcxn*2)+(j*2)+1] = atoms[i].getNeighbourIndex(j);
      }
   }
}

Parameters::Parameters()
{
   mpnt = 0;
   mvar = 0;
   mdist = 1;
   mcxn = 0;
   mpairs = vector<int>();
   mk = 1;
}

void Parameters::copy(const Parameters & other)
{
   pnt(other.mpnt);
   mcxn = other.mcxn;
   mdist = other.mdist;
   mpairs = other.mpairs;
   mdim = other.mdim;
   mlen = other.mlen;
   mk = other.mk;
}

void Parameters::strain(const Point & strain)
{
   Matrix3 strainTensor;
   strainTensor.setDiag(strain);
   mdim *= strainTensor; 
   for (int i=0;i<3;i++)
      mlen.setCoord(dim(i).distance(), i);
}

void Parameters::strain(const Point & strain, const Point& shear)
{
   Matrix3 strainTensor;
   strainTensor.setDiag(strain);
   strainTensor.setSym(1,2,shear.x());
   strainTensor.setSym(0,2,shear.y());
   strainTensor.setSym(0,1,shear.z());
   mdim *= strainTensor; 
   for (int i=0;i<3;i++)
      mlen.setCoord(dim(i).distance(), i);
}

void Parameters::strain(const Matrix3& strainTensor)
{
   mdim *= strainTensor;
   for (int i=0;i<3;i++)
      mlen.setCoord(dim(i).distance(), i);
}

void Parameters::setDim (const Matrix3& other) 
{
   mdim = other;
   for (int i=0; i<3; i++)
      mlen.setCoord( other.getPoint(i).distance(), i );
}
