/*
 *      EGADS: Electronic Geometry Aircraft Design System
 *
 *             Manipulate the Tessellation of a Face
 *
 *      Copyright 2011-2022, Massachusetts Institute of Technology
 *      Licensed under The GNU Lesser General Public License, version 2.1
 *      See http://www.opensource.org/licenses/lgpl-2.1.php
 *
 */
 
#include "egads.h"
#include <math.h>
#include <float.h>      /* Needed in some systems for DBL_MAX definition */

#include "egadsTris.h"
#ifndef LITE
#include "prm.h"
#endif


#define FLOODEPTH        6	/* flood depth for marking tri neighbors */
#define NOTFILLED       -1      /* Not yet filled flag */
#define TOBEFILLED      -2
#define PI              3.1415926535897931159979635
#define ANGTOL          1.e-6
#define DEVANG          2.65
#define CUTANG          3.10
#define MAXANG          3.13
#define MAXORCNT        500


#define AREA2D(a,b,c)   ((a[0]-c[0])*(b[1]-c[1]) - (a[1]-c[1])*(b[0]-c[0]))
#define CROSS(a,b,c)      a[0] = (b[1]*c[2]) - (b[2]*c[1]);\
                          a[1] = (b[2]*c[0]) - (b[0]*c[2]);\
                          a[2] = (b[0]*c[1]) - (b[1]*c[0])
#define DOT(a,b)         (a[0]*b[0] + a[1]*b[1] + a[2]*b[2])
#define DIST2(a,b)       ((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + \
                          (a[2]-b[2])*(a[2]-b[2]))
#define MAX(a,b)        (((a) > (b)) ? (a) : (b))
#define MIN(a,b)        (((a) < (b)) ? (a) : (b))

#ifdef __HOST_AND_DEVICE__
#undef __HOST_AND_DEVICE__
#endif
#ifdef __PROTO_H_AND_D__
#undef __PROTO_H_AND_D__
#endif

#ifdef __CUDACC__
#define __HOST_AND_DEVICE__ extern "C" __host__ __device__
#define __PROTO_H_AND_D__   extern "C" __host__ __device__
#else
#define __HOST_AND_DEVICE__
#define __PROTO_H_AND_D__ extern
#endif

__PROTO_H_AND_D__ int  EG_sameThread(const egObject *object);
__PROTO_H_AND_D__ int  EG_outLevel(const egObject *object);
__PROTO_H_AND_D__ int  EG_makeNeighbors(triStruct *ts, int f);
__PROTO_H_AND_D__ void EG_makeConnect(int k1, int k2, int *tri, int *kedge,
                                      int *ntable, connect *etable, int face);
__PROTO_H_AND_D__ int  EG_quad2tris3(long tID, const egObject *face,
                                     double *parms, int *elens, double *uv,
                                     int *npts, double **uvs,
                                     int *ntris, int **tris, int *flag);
__PROTO_H_AND_D__ int  EG_quad2tris (long tID, const egObject *face,
                                     double *parms, int *elens, double *uv,
                                     int *npts, double **uvs,
                                     int *ntris, int **tris, int *tfi);

__PROTO_H_AND_D__ double EG_orienTri(double *t0, double *t1, double *t2);


/*
 * table of primes just below 2^n, n=2..31 for use in finding the right prime
 * number to be the table size.
 */

#ifdef __CUDA_ARCH__
  __device__
#endif
  static unsigned int primetab[] = { 127, 251, 509, 1021, 2039, 4093, 8191, 
                                     16381, 32749, 65521, 131071, 262139, 
                                     524287, 1048573, 2097143, 4194301, 
                                     8388593, 16777213, 33554393,
                                     67108859, 134217689, 268435399, 
                                     536870909, 1073741789, 2147483647 };

/*
 * reference triangle side definition
 */

#ifdef __CUDA_ARCH__
  __device__
#endif
  static int sides[3][2] = {{1,2}, {2,0}, {0,1}};


#ifdef WRITETRIS
static void
EG_writeTris(triStruct *ts, const char *filename)
{
  int  k;
  FILE *fp;
  
  fp = fopen(filename, "w");
  if (fp == NULL) {
    printf(" EGADS ERROR: Opening %s!\n", filename);
    return;
  }
  fprintf(fp, "1\n");  /* number of bodies */
  fprintf(fp, "1\n");  /* number of Faces */
  fprintf(fp, "%d %d\n", ts->nverts, ts->ntris);
  for (k = 0; k < ts->nverts; k++)
    fprintf(fp, "%lf %lf %lf\n", ts->verts[k].xyz[0], ts->verts[k].xyz[1],
            ts->verts[k].xyz[2]);
  for (k = 0; k < ts->ntris; k++)
    fprintf(fp, "%d %d %d\n", ts->tris[k].indices[0], ts->tris[k].indices[1],
            ts->tris[k].indices[2]);
  
  fclose(fp);
}
#endif


#ifdef DEBUG
__HOST_AND_DEVICE__ static void
EG_checkTess(triStruct *ts)
{
  int i, j, n, n1, n2, side, hit = 0;

  for (i = 1; i <= ts->ntris; i++)
    for (j = 0; j < 3; j++) {
      if ((ts->tris[i-1].indices[j] > ts->nverts) || 
          (ts->tris[i-1].indices[j] <= 0)) {
        printf(" checkTess: Tri indices[%d][%d] = %d\n",
               i, j, ts->tris[i-1].indices[j]);
        hit++;
      }
      n = ts->tris[i-1].neighbors[j];
      if ((n > ts->ntris) || (n == 0)) {
        printf(" checkTess: Tri neighbors[%d][%d] = %d\n", i, j, n);
        hit++;
      } else if (n > 0) {
        side = -1;
        if (ts->tris[n-1].neighbors[0] == i) side = 0;
        if (ts->tris[n-1].neighbors[1] == i) side = 1;
        if (ts->tris[n-1].neighbors[2] == i) side = 2;
        if (side == -1) {
          printf(" checkTess: Tri neighbors[%d][%d] = %d\n", i, j, n);
          printf("            Tri neighbors[%d][0] = %d\n",
                  n, ts->tris[n-1].neighbors[0]);
          printf("            Tri neighbors[%d][1] = %d\n",
                  n, ts->tris[n-1].neighbors[1]);
          printf("            Tri neighbors[%d][2] = %d\n",
                  n, ts->tris[n-1].neighbors[2]);
          hit++;
        } else {
          n1 = ts->tris[i-1].indices[sides[j][0]];
          n2 = ts->tris[i-1].indices[sides[j][1]];
          if (((n1 != ts->tris[n-1].indices[sides[side][0]]) ||
               (n2 != ts->tris[n-1].indices[sides[side][1]])) &&
              ((n1 != ts->tris[n-1].indices[sides[side][1]]) ||
               (n2 != ts->tris[n-1].indices[sides[side][0]]))) {
            printf(" checkTess: Tri neighbors[%d][%d] = %d\n", i, j, n);
            printf("            nodes = %d %d, %d %d\n", n1, n2, 
                   ts->tris[n-1].indices[sides[side][0]],
                   ts->tris[n-1].indices[sides[side][1]]);
            hit++;
          }
        }
      }
    }

  if (hit == 0) {
    printf(" checkTess: OK!\n");
  } else {
    printf(" checkTess: nTris = %d,  nNodes = %d  NOT OK!\n",
           ts->ntris, ts->nverts);
  }
}
#endif


/* simple hash function based on mod of number of elems in hash table */

__HOST_AND_DEVICE__ static int
EG_hashit(KEY key, triStruct *ts)
{
  return (key.keys[0]+key.keys[1]+key.keys[2]) % ts->numElem;
}


/* idestroy --- destroy a single element on a chain */

__HOST_AND_DEVICE__ static void
EG_idestroy(/*@null@*/ ELEMENT *elem)
{
  if (elem != NULL) {
    EG_idestroy(elem->next);
    EG_free(elem);
  }
}


/* hdestroy --- nuke the existing hash table */

__HOST_AND_DEVICE__ static void
EG_hdestroy(triStruct *ts)
{
  int i;

  if (ts->hashTab != NULL) {
    /* free all the chains */
    for (i = 0; i < ts->numElem; i++) EG_idestroy(ts->hashTab[i]);

    /* now the table itself */
    EG_free(ts->hashTab);
    ts->numElem = -1;
    ts->hashTab = NULL;
  }
}


/* hcreate --- create a hash table at least how_many big */

__HOST_AND_DEVICE__ static int
EG_hcreate(int how_many, triStruct *ts)
{
  int i, j;

  /*
   * find first prime number >= how_many, and use it for table size
   */

  if (ts->numElem != -1)	/* already a table out there */
    EG_hdestroy(ts);		/* remove it */
  
  j = sizeof(primetab) / sizeof(primetab[0]);
  for (i = 0; i < j; i++) if (primetab[i] >= how_many) break;
  
  if (i >= j)		/* how_many bigger than any prime we have, use it */
    ts->numElem = how_many;
  else
    ts->numElem = primetab[i];

  if ((ts->hashTab = (ELEMENT **) EG_calloc(ts->numElem, 
                                            sizeof(ELEMENT *))) == NULL)
    return 0;
  else
    return 1;
}


/* hmakeKEY -- make the key for hash table usage */

__HOST_AND_DEVICE__ static KEY
EG_hmakeKEY(int i0, int i1, int i2)
{
  KEY key;

  key.keys[0] = MIN(i0, MIN(i1, i2));
  key.keys[2] = MAX(i0, MAX(i1, i2));
  key.keys[1] = i0+i1+i2 - key.keys[0] - key.keys[2];
  return key;
}


/* hfind --- lookup an item in the hash table */

__HOST_AND_DEVICE__ static int
EG_hfind(int i0, int i1, int i2, int *close, double *xyz, triStruct *ts)
{
  ELEMENT *ep;
  ENTRY   entry;
  int     hindex;

  if (ts->hashTab == NULL) return NOTFILLED;

  entry.key = EG_hmakeKEY(i0, i1, i2);
  hindex    = EG_hashit(entry.key, ts);

  if (ts->hashTab[hindex] == NULL) return NOTFILLED;

  for (ep = ts->hashTab[hindex]; ep != NULL; ep = ep->next)
    if ((ep->item.key.keys[0] == entry.key.keys[0]) &&
        (ep->item.key.keys[1] == entry.key.keys[1]) && 
        (ep->item.key.keys[2] == entry.key.keys[2])) {
       /* ...yes, its on the chain */
      *close = ep->item.data.close;
      xyz[0] = ep->item.data.xyz[0];
      xyz[1] = ep->item.data.xyz[1];
      xyz[2] = ep->item.data.xyz[2];
      return 0;
    }

  return NOTFILLED;
}


/* hadd --- enter an item in the hash table */

__HOST_AND_DEVICE__ static int
EG_hadd(int i0, int i1, int i2, int close, double *xyz, triStruct *ts)
{
  ELEMENT e, *ep = NULL, *ep2 = NULL;
  ENTRY   entry;
  int     hindex;

  if (ts->hashTab == NULL) return NOTFILLED;

  entry.key         = EG_hmakeKEY(i0, i1, i2);
  entry.data.close  = close;
  entry.data.xyz[0] = xyz[0];
  entry.data.xyz[1] = xyz[1];
  entry.data.xyz[2] = xyz[2];
  hindex            = EG_hashit(entry.key, ts);

  if (ts->hashTab[hindex] == NULL) {	/* nothing there */
    /* add it to the table */
    e.item = entry;
    e.next = NULL;
    if ((ts->hashTab[hindex] = (ELEMENT *) EG_calloc(1, sizeof(ELEMENT))) == NULL)
      return NOTFILLED;
    *ts->hashTab[hindex] = e;
    return 0;
  } else {
    /* something in bucket, see if already on chain */
    ep2 = ts->hashTab[hindex];
    for (ep = ts->hashTab[hindex]; ep != NULL; ep = ep->next)
      if ((ep->item.key.keys[0] == entry.key.keys[0]) &&
          (ep->item.key.keys[1] == entry.key.keys[1]) && 
          (ep->item.key.keys[2] == entry.key.keys[2])) {
        /* ...yes, its on the chain */
        return 1;		/* indicate found */
      } else {
        ep2 = ep;
      }

    /* at this point, item was not in table */
    /* ep2 points at last element on the list */
    if ((ep2->next = (ELEMENT *) EG_calloc(1, sizeof(ELEMENT))) == NULL)
        return NOTFILLED;
    ep2->next->item = entry;
    ep2->next->next = NULL;
    return 0;
  }
}


__HOST_AND_DEVICE__ static double
EG_getIntersect(double *pt0, double *pt1, double *pt2)
{
  double x0[3], x1[3], x2[2], n0[3], n1[3], n2[3];
  double angle, cosan, sinan, dist, len;

  /* map to 2D */
  x0[0] = pt0[0] - pt2[0];
  x1[0] = pt1[0] - pt2[0];
  x0[1] = pt0[1] - pt2[1];
  x1[1] = pt1[1] - pt2[1];
  x0[2] = pt0[2] - pt2[2];
  x1[2] = pt1[2] - pt2[2];
  CROSS(n2, x0, x1);
  dist   = DOT(n2, n2);
  if (dist == 0.0) return 1.e20;
  dist   = 1.0/sqrt(dist);
  n2[0] *= dist;
  n2[1] *= dist;
  n2[2] *= dist;
  dist   = DOT(x1, x1);
  if (dist == 0.0) return 1.e20;
  dist   = 1.0/sqrt(dist);
  n0[0]  = x1[0]*dist;
  n0[1]  = x1[1]*dist;
  n0[2]  = x1[2]*dist;
  CROSS(n1, n0, n2);

  /* transformed space in tri */
  x0[0] = DOT(n0, pt0);
  x0[1] = DOT(n1, pt0);
  x1[0] = DOT(n0, pt1);
  x1[1] = DOT(n1, pt1);
  x2[0] = DOT(n0, pt2);
  x2[1] = DOT(n1, pt2);

  angle = atan2(x1[1]-x0[1], x1[0]-x0[0]);
  cosan = cos(angle);
  sinan = sin(angle);
  len   = sqrt((x1[1]-x0[1])*(x1[1]-x0[1]) + (x1[0]-x0[0])*(x1[0]-x0[0]));
  dist  = ((x2[1]-x0[1])*cosan - (x2[0]-x0[0])*sinan)/len;
  if ((dist < -0.01) || (dist > 1.01)) return 1.e40;

  x0[0] = pt0[0] + dist*(pt1[0] - pt0[0]);
  x0[1] = pt0[1] + dist*(pt1[1] - pt0[1]);
  x0[2] = pt0[2] + dist*(pt1[2] - pt0[2]);
  
  return (x0[0]-pt2[0])*(x0[0]-pt2[0]) + (x0[1]-pt2[1])*(x0[1]-pt2[1]) +
         (x0[2]-pt2[2])*(x0[2]-pt2[2]);
}


__HOST_AND_DEVICE__ static int
EG_recClose2Edge(int t0, double *xyz, int depth, triStruct *ts)
{
  int    i0, i1, i2, tn;
  double dist;

  if (depth <= 0) return 0;

  i0 = ts->tris[t0].indices[0]-1;
  i1 = ts->tris[t0].indices[1]-1;
  i2 = ts->tris[t0].indices[2]-1;

  tn = ts->tris[t0].neighbors[0];
  if (tn <= 0) {
    if ((dist = EG_getIntersect(ts->verts[i1].xyz, ts->verts[i2].xyz, xyz)) < 
        ts->edist2) {
#ifdef DEBUG
      printf(" dist = %le, edist = %le\n", dist, ts->edist2);
#endif
      return 1;
    }
  } else {
    if (EG_recClose2Edge(tn-1, xyz, depth-1, ts) == 1) return 1;
  }

  tn = ts->tris[t0].neighbors[1];
  if (tn <= 0) {
    if ((dist = EG_getIntersect(ts->verts[i0].xyz, ts->verts[i2].xyz, xyz)) <
        ts->edist2) {
#ifdef DEBUG
      printf(" dist = %le, edist = %le\n", dist, ts->edist2);
#endif
      return 1;
    }
  } else {
    if (EG_recClose2Edge(tn-1, xyz, depth-1, ts) == 1) return 1;
  }

  tn = ts->tris[t0].neighbors[2];
  if (tn <= 0) {
    if ((dist = EG_getIntersect(ts->verts[i0].xyz, ts->verts[i1].xyz, xyz)) <
        ts->edist2) {
#ifdef DEBUG
      printf(" dist = %le, edist = %le\n", dist, ts->edist2);
#endif
      return 1;
    }
  } else {
    if (EG_recClose2Edge(tn-1, xyz, depth-1, ts) == 1) return 1;
  }

  return 0;
}


__HOST_AND_DEVICE__ static int
EG_close2Edge(int t0, double *xyz, triStruct *ts)
{
  /* look down as many as 6 neighbors for an edge */

  return EG_recClose2Edge(t0, xyz, 6, ts);
}


__HOST_AND_DEVICE__ static double
EG_rayIntersect(double *pt0, double *pt1, double *pt2)
{
  double dx[3], dp[3], px[3], dist, d;

  dx[0] = pt1[0] - pt0[0];
  dx[1] = pt1[1] - pt0[1];
  dx[2] = pt1[2] - pt0[2];
  d     = sqrt(dx[0]*dx[0] + dx[1]*dx[1] + dx[2]*dx[2]);
  if (d == 0.0) {
    printf(" EGADS Internal: Edge Segment w/ Zero len (EG_rayIntersect)\n");
    return 100.0;
  }
  dx[0] /= d;
  dx[1] /= d;
  dx[2] /= d;

  dp[0] = pt2[0] - pt0[0];
  dp[1] = pt2[1] - pt0[1];
  dp[2] = pt2[2] - pt0[2];
  dist  = sqrt(dp[0]*dp[0] + dp[1]*dp[1] + dp[2]*dp[2]);
  if (dist != 0) {
    dp[0] /= dist;
    dp[1] /= dist;
    dp[2] /= dist;
  }

  dist *= (dx[0]*dp[0] + dx[1]*dp[1] + dx[2]*dp[2])/d;
  px[0] = pt0[0] + dist*(pt1[0] - pt0[0]) - pt2[0];
  px[1] = pt0[1] + dist*(pt1[1] - pt0[1]) - pt2[1];
  px[2] = pt0[2] + dist*(pt1[2] - pt0[2]) - pt2[2];

  return sqrt(px[0]*px[0] + px[1]*px[1] + px[2]*px[2])/d;
}


__HOST_AND_DEVICE__ static int
EG_recCloseEdge(int t0, double *xyz, int depth, triStruct *ts)
{
  int    i0, i1, i2, tn;
  double rayfac = 0.125;

  if (depth <= 0) return 0;

  i0 = ts->tris[t0].indices[0]-1;
  i1 = ts->tris[t0].indices[1]-1;
  i2 = ts->tris[t0].indices[2]-1;

  tn = ts->tris[t0].neighbors[0];
  if (tn <= 0) {
    if ((ts->verts[i1].type == NODE) && (ts->verts[i2].type == NODE))
      if (ts->verts[i1].index == ts->verts[i2].index) return 1;
    if (EG_rayIntersect(ts->verts[i1].xyz, ts->verts[i2].xyz, xyz) < rayfac)
      return 1;
  } else {
    if (EG_recCloseEdge(tn-1, xyz, depth-1, ts) == 1) return 1;
  }

  tn = ts->tris[t0].neighbors[1];
  if (tn <= 0) {
    if ((ts->verts[i0].type == NODE) && (ts->verts[i2].type == NODE))
      if (ts->verts[i0].index == ts->verts[i2].index) return 1;
    if (EG_rayIntersect(ts->verts[i0].xyz, ts->verts[i2].xyz, xyz) < rayfac)
      return 1;
  } else {
    if (EG_recCloseEdge(tn-1, xyz, depth-1, ts) == 1) return 1;
  }

  tn = ts->tris[t0].neighbors[2];
  if (tn <= 0) {
    if ((ts->verts[i0].type == NODE) && (ts->verts[i1].type == NODE))
      if (ts->verts[i0].index == ts->verts[i1].index) return 1;
    if (EG_rayIntersect(ts->verts[i0].xyz, ts->verts[i1].xyz, xyz) < rayfac)
      return 1;
  } else {
    if (EG_recCloseEdge(tn-1, xyz, depth-1, ts) == 1) return 1;
  }

  return 0;
}


__HOST_AND_DEVICE__ static int
EG_closeEdge(int t0, double *xyz, triStruct *ts)
{
  /* look down as many as 4 neighbors for an edge */

  return EG_recCloseEdge(t0, xyz, 4, ts);
}


__HOST_AND_DEVICE__ static int
EG_addVert(int type, int edge, int index, double *xyz, double *uv,
           triStruct *ts)
{
  int     n;
  triVert *tmp;

  if (ts->nverts >= ts->mverts) {
    n   = ts->mverts + CHUNK;
    tmp = (triVert *) EG_reall(ts->verts, n*sizeof(triVert));
    if (tmp == NULL) return EGADS_MALLOC;
    ts->verts  = tmp;
    ts->mverts = n;
#ifdef DEBUG
    printf(" Realloc Nodes: now %d (%d)\n", n, ts->nverts);
#endif
  }

  ts->verts[ts->nverts].type   = type;
  ts->verts[ts->nverts].edge   = edge;
  ts->verts[ts->nverts].index  = index;
  ts->verts[ts->nverts].xyz[0] = xyz[0];
  ts->verts[ts->nverts].xyz[1] = xyz[1];
  ts->verts[ts->nverts].xyz[2] = xyz[2];
  ts->verts[ts->nverts].uv[0]  = uv[0];
  ts->verts[ts->nverts].uv[1]  = uv[1];
  ts->nverts++;
  return ts->nverts;
}


__HOST_AND_DEVICE__ static int
EG_inTri(int t1, double *xyz, double fuzz, triStruct *ts)
{
  int    i0, i1, i2;
  double x0[2], x1[3], x2[3], xp[2], n0[3], n1[3], n2[3];
  double dx1, dy1, dx2, dy2, dist, dxx, dyy, w0, w1, w2;

  /* check to see if the point (XYZ) is in the tri (by projection) */

  i0 = ts->tris[t1].indices[0]-1;
  i1 = ts->tris[t1].indices[1]-1;
  i2 = ts->tris[t1].indices[2]-1;

  x1[0] = ts->verts[i1].xyz[0] - ts->verts[i0].xyz[0];
  x2[0] = ts->verts[i2].xyz[0] - ts->verts[i0].xyz[0];
  x1[1] = ts->verts[i1].xyz[1] - ts->verts[i0].xyz[1];
  x2[1] = ts->verts[i2].xyz[1] - ts->verts[i0].xyz[1];
  x1[2] = ts->verts[i1].xyz[2] - ts->verts[i0].xyz[2];
  x2[2] = ts->verts[i2].xyz[2] - ts->verts[i0].xyz[2];
  CROSS(n2, x1, x2);
  dist   = DOT(n2, n2);
  if (dist == 0.0) return 1;
  dist   = 1.0/sqrt(dist);
  n2[0] *= dist;
  n2[1] *= dist;
  n2[2] *= dist;

  dist  = DOT(x2, x2);
  if (dist == 0.0) return 1;
  dist  = 1.0/sqrt(dist);
  n0[0] = x2[0]*dist;
  n0[1] = x2[1]*dist;
  n0[2] = x2[2]*dist;

  CROSS(n1, n0, n2);

  x0[0] = DOT(n0, ts->verts[i0].xyz);
  x0[1] = DOT(n1, ts->verts[i0].xyz);
  x1[0] = DOT(n0, ts->verts[i1].xyz);
  x1[1] = DOT(n1, ts->verts[i1].xyz);
  x2[0] = DOT(n0, ts->verts[i2].xyz);
  x2[1] = DOT(n1, ts->verts[i2].xyz);
  xp[0] = DOT(n0, xyz);
  xp[1] = DOT(n1, xyz);

  dx1  = x0[0] - x2[0];
  dy1  = x0[1] - x2[1];
  dx2  = x1[0] - x2[0];
  dy2  = x1[1] - x2[1];
  dist = dx1*dy2-dy1*dx2;
  if (dist == 0.0) return 1;
  dxx  =  xp[0] - x2[0];
  dyy  =  xp[1] - x2[1];
  w0   =  (dxx*dy2-dyy*dx2) / dist;
  w1   = -(dxx*dy1-dyy*dx1) / dist;
  w2   =  1.0 - w0 - w1;
  if ((w0 <= fuzz) || (w1 <= fuzz) || (w2 <= fuzz)) return 1;

  return 0;
}


__HOST_AND_DEVICE__ static int
EG_checkOr(int t1, int side, int t2, triStruct *ts)
{
  int    i0, i1, i2, i3;
  double a1, a2;

  /* check if the orientation for the swapped pair of tris is OK */

  i0 = ts->tris[t1].indices[side];
  i1 = ts->tris[t1].indices[sides[side][0]];
  i2 = ts->tris[t1].indices[sides[side][1]];
  i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
       ts->tris[t2].indices[2] - i1 - i2;
  if ((i3 < 1) || (i3 > ts->nverts)) return 0;
 
  a1 = AREA2D(ts->verts[i0-1].uv, ts->verts[i1-1].uv, ts->verts[i3-1].uv);
  a2 = AREA2D(ts->verts[i0-1].uv, ts->verts[i3-1].uv, ts->verts[i2-1].uv);

  if (a1*a2       <= 0.0) return 0;
  if (a1*ts->orUV >  0.0) return 1;
  ts->orCnt++;
  printf(" Face %d: wrong OR = %le, %le (%d), phase = %d -- %d\n",
         ts->fIndex, a1, a2, ts->orUV, ts->phase, ts->orCnt);
  return 0;
}


#ifdef CHECKUV
__HOST_AND_DEVICE__ static void
EG_checkUVs(triStruct *ts)
{
  int    t1, side, t2, i0, i1, i2, i3;
  double a1, a2;
  
  for (t1 = 0; t1 < ts->ntris; t1++) {
    for (side = 0; side < 3; side++) {
      t2 = ts->tris[t1].neighbors[side]-1;
      if (t2 < 0) continue;
      i0 = ts->tris[t1].indices[side];
      i1 = ts->tris[t1].indices[sides[side][0]];
      i2 = ts->tris[t1].indices[sides[side][1]];
      i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
           ts->tris[t2].indices[2] - i1 - i2;
      if ((i3 < 1) || (i3 > ts->nverts)) {
        printf(" EG_checkUVs: Face %d -> %d/%d -- BAD index!\n",
               ts->fIndex, t1, t2);
        continue;
      }
      
      a1 = AREA2D(ts->verts[i0-1].uv, ts->verts[i1-1].uv, ts->verts[i2-1].uv);
      a2 = AREA2D(ts->verts[i1-1].uv, ts->verts[i3-1].uv, ts->verts[i2-1].uv);
      if ((a1*a2 <= 0.0) || (a1*ts->orUV < 0.0)) {
        printf(" EG_checkUVs: Face %d -> %d/%d inverted UVs %lf %lf  %d\n",
               ts->fIndex, t1, t2, a1, a2, ts->orUV);
      }
    }
  }
}
#endif


__HOST_AND_DEVICE__ static double
EG_maxXYZangle(int i1, int i2, int i3, triStruct *ts)
{
  double cosa, sina, ang0, ang1, ang2, vec1[3], vec2[3], n[3];

  vec1[0] = ts->verts[i2].xyz[0] - ts->verts[i1].xyz[0];
  vec1[1] = ts->verts[i2].xyz[1] - ts->verts[i1].xyz[1];
  vec1[2] = ts->verts[i2].xyz[2] - ts->verts[i1].xyz[2];
  vec2[0] = ts->verts[i3].xyz[0] - ts->verts[i1].xyz[0];
  vec2[1] = ts->verts[i3].xyz[1] - ts->verts[i1].xyz[1];
  vec2[2] = ts->verts[i3].xyz[2] - ts->verts[i1].xyz[2];
  cosa    = DOT(vec1, vec2);
  CROSS(n, vec1, vec2);
  sina = sqrt(DOT(n,n));
  ang0 = atan2(sina, cosa);

  vec1[0] = -vec1[0];
  vec1[1] = -vec1[1];
  vec1[2] = -vec1[2];
  vec2[0] = ts->verts[i3].xyz[0] - ts->verts[i2].xyz[0];
  vec2[1] = ts->verts[i3].xyz[1] - ts->verts[i2].xyz[1];
  vec2[2] = ts->verts[i3].xyz[2] - ts->verts[i2].xyz[2];
  cosa    = DOT(vec1, vec2);
  CROSS(n, vec1, vec2);
  sina = sqrt(DOT(n,n));
  ang1 = atan2(sina, cosa);

  ang2 = PI - ang1 - ang0;
  ang0 = MAX(ang0, ang1);
  return MAX(ang0, ang2);
}


__HOST_AND_DEVICE__ static double
EG_maxUVangle(int i1, int i2, int i3, triStruct *ts)
{
  double cosa, sina, ang0, ang1, ang2, vec1[2], vec2[2];

  vec1[0] =  ts->verts[i2].uv[0] - ts->verts[i1].uv[0];
  vec1[1] = (ts->verts[i2].uv[1] - ts->verts[i1].uv[1])*ts->VoverU;
  vec2[0] =  ts->verts[i3].uv[0] - ts->verts[i1].uv[0];
  vec2[1] = (ts->verts[i3].uv[1] - ts->verts[i1].uv[1])*ts->VoverU;
  cosa    = vec1[0]*vec2[0] + vec1[1]*vec2[1];
  sina    = vec1[0]*vec2[1] - vec1[1]*vec2[0];
  if (sina < 0.0) sina = -sina;
  ang0 = atan2(sina, cosa);

  vec1[0] = -vec1[0];
  vec1[1] = -vec1[1];
  vec2[0] =  ts->verts[i3].uv[0] - ts->verts[i2].uv[0];
  vec2[1] = (ts->verts[i3].uv[1] - ts->verts[i2].uv[1])*ts->VoverU;
  cosa    = vec1[0]*vec2[0] + vec1[1]*vec2[1];
  sina    = vec1[0]*vec2[1] - vec1[1]*vec2[0];
  if (sina < 0.0) sina = -sina;
  ang1 = atan2(sina, cosa);

  ang2 = PI - ang1 - ang0;
  ang0 = MAX(ang0, ang1);
  return MAX(ang0, ang2);
}


__HOST_AND_DEVICE__ static int
EG_angXYZTest(int t1, int iedg, int t2, triStruct *ts)
{
  int    i0, i1, i2, i3;
  double angle, angle_now, angle_swap, dist, dot, x1[3], x2[3], n1[3], n2[3];

  i0 = ts->tris[t1].indices[iedg];
  i1 = ts->tris[t1].indices[sides[iedg][0]];
  i2 = ts->tris[t1].indices[sides[iedg][1]];
  i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
       ts->tris[t2].indices[2] - i1 - i2;
  if ((i3 < 1) || (i3 > ts->nverts)) return 0;

  angle_now  = EG_maxXYZangle(i0-1, i1-1, i2-1, ts);
  angle      = EG_maxXYZangle(i1-1, i3-1, i2-1, ts);
  angle_now  = MAX(angle, angle_now);

  /* are the proposed orientations OK? */

  if (ts->phase != -3) {
    x1[0] = ts->verts[i1-1].xyz[0] - ts->verts[i0-1].xyz[0];
    x2[0] = ts->verts[i3-1].xyz[0] - ts->verts[i0-1].xyz[0];
    x1[1] = ts->verts[i1-1].xyz[1] - ts->verts[i0-1].xyz[1];
    x2[1] = ts->verts[i3-1].xyz[1] - ts->verts[i0-1].xyz[1];
    x1[2] = ts->verts[i1-1].xyz[2] - ts->verts[i0-1].xyz[2];
    x2[2] = ts->verts[i3-1].xyz[2] - ts->verts[i0-1].xyz[2];
    CROSS(n1, x1, x2);
    dist = DOT(n1, n1);
    if (dist == 0.0) goto noswap;
    dist   = 1.0/sqrt(dist);
    n1[0] *= dist;
    n1[1] *= dist;
    n1[2] *= dist;
    x1[0] = ts->verts[i2-1].xyz[0] - ts->verts[i3-1].xyz[0];
    x2[0] = ts->verts[i0-1].xyz[0] - ts->verts[i3-1].xyz[0];
    x1[1] = ts->verts[i2-1].xyz[1] - ts->verts[i3-1].xyz[1];
    x2[1] = ts->verts[i0-1].xyz[1] - ts->verts[i3-1].xyz[1];
    x1[2] = ts->verts[i2-1].xyz[2] - ts->verts[i3-1].xyz[2];
    x2[2] = ts->verts[i0-1].xyz[2] - ts->verts[i3-1].xyz[2];
    CROSS(n2, x1, x2);
    dist = DOT(n2, n2);
    if (dist == 0.0) goto noswap;
    dist   = 1.0/sqrt(dist);
    n2[0] *= dist;
    n2[1] *= dist;
    n2[2] *= dist;
    dot = DOT(n1, n2);
    if (dot < ts->dotnrm) goto noswap;
  }

  /* compare XYZ angles of proposed tris */

  angle_swap = EG_maxXYZangle(i0-1, i1-1, i3-1, ts);
  angle      = EG_maxXYZangle(i0-1, i3-1, i2-1, ts);
  angle_swap = MAX(angle, angle_swap);
  if (angle_swap + ANGTOL >=  angle_now) goto noswap;

  /* mark for swapping */
  ts->accum = MAX(ts->accum, angle_swap);
  return 1;

noswap:
  ts->accum = MAX(ts->accum, angle_now);
  return 0;
}


__HOST_AND_DEVICE__ static int
EG_angUVTest(int t1, int iedg, int t2, triStruct *ts)
{
  int    i0, i1, i2, i3;
  double angle, angle_now, angle_swap;

  /* compare UV angles */

  i0 = ts->tris[t1].indices[iedg];
  i1 = ts->tris[t1].indices[sides[iedg][0]];
  i2 = ts->tris[t1].indices[sides[iedg][1]];
  i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
       ts->tris[t2].indices[2] - i1 - i2;
  if ((i3 < 1) || (i3 > ts->nverts)) return 0;

  angle_now  = EG_maxUVangle(i0-1, i1-1, i2-1, ts);
  angle      = EG_maxUVangle(i1-1, i3-1, i2-1, ts);
  angle_now  = MAX(angle, angle_now);
  angle_swap = EG_maxUVangle(i0-1, i1-1, i3-1, ts);
  angle      = EG_maxUVangle(i0-1, i3-1, i2-1, ts);
  angle_swap = MAX(angle, angle_swap);

  if (angle_swap + ANGTOL < angle_now) {
    ts->accum = MAX(ts->accum, angle_swap);
    return 1;
  }
  ts->accum = MAX(ts->accum, angle_now);
  return 0;
}


__HOST_AND_DEVICE__ static int
EG_areaTest(int t1, int iedg, int t2, triStruct *ts)
{
  int    i0, i1, i2, i3;
  double a1, a2;

  /* is this area ok? */

  i0 = ts->tris[t1].indices[iedg];
  i1 = ts->tris[t1].indices[sides[iedg][0]];
  i2 = ts->tris[t1].indices[sides[iedg][1]];
  i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
       ts->tris[t2].indices[2] - i1 - i2;
  if ((i3 < 1) || (i3 > ts->nverts)) return 0;
  a1 = AREA2D(ts->verts[i0-1].uv, ts->verts[i1-1].uv, ts->verts[i2-1].uv);
  a2 = AREA2D(ts->verts[i1-1].uv, ts->verts[i3-1].uv, ts->verts[i2-1].uv);
  if ((a1*ts->orUV > 0.0) && (a2*ts->orUV > 0.0)) return 0;

  /* not ok -- swap? */

  a1 = AREA2D(ts->verts[i0-1].uv, ts->verts[i1-1].uv, ts->verts[i3-1].uv);
  a2 = AREA2D(ts->verts[i0-1].uv, ts->verts[i3-1].uv, ts->verts[i2-1].uv);
  if ((a1*ts->orUV > 0.0) && (a2*ts->orUV > 0.0)) return 1;
  return 0;
}


__HOST_AND_DEVICE__ static int
EG_diagTest(int t1, int iedg, int t2, triStruct *ts)
{
  int    i0, i1, i2, i3;
  double x1[3], x2[3], n1[3], n2[3], angle, dist, newd, old = -2.0;

  i0 = ts->tris[t1].indices[iedg];
  i1 = ts->tris[t1].indices[sides[iedg][0]];
  i2 = ts->tris[t1].indices[sides[iedg][1]];
  i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
       ts->tris[t2].indices[2] - i1 - i2;
  if ((i3 < 1) || (i3 > ts->nverts)) return 0;

  /* compare dot of normals -- pick the maximum */

  x1[0] = ts->verts[i1-1].xyz[0] - ts->verts[i0-1].xyz[0];
  x2[0] = ts->verts[i2-1].xyz[0] - ts->verts[i0-1].xyz[0];
  x1[1] = ts->verts[i1-1].xyz[1] - ts->verts[i0-1].xyz[1];
  x2[1] = ts->verts[i2-1].xyz[1] - ts->verts[i0-1].xyz[1];
  x1[2] = ts->verts[i1-1].xyz[2] - ts->verts[i0-1].xyz[2];
  x2[2] = ts->verts[i2-1].xyz[2] - ts->verts[i0-1].xyz[2];
  CROSS(n1, x1, x2);
  dist = DOT(n1, n1);
  if (dist != 0.0) {
    dist   = 1.0/sqrt(dist);
    n1[0] *= dist;
    n1[1] *= dist;
    n1[2] *= dist;

    x1[0] = ts->verts[i2-1].xyz[0] - ts->verts[i3-1].xyz[0];
    x2[0] = ts->verts[i1-1].xyz[0] - ts->verts[i3-1].xyz[0];
    x1[1] = ts->verts[i2-1].xyz[1] - ts->verts[i3-1].xyz[1];
    x2[1] = ts->verts[i1-1].xyz[1] - ts->verts[i3-1].xyz[1];
    x1[2] = ts->verts[i2-1].xyz[2] - ts->verts[i3-1].xyz[2];
    x2[2] = ts->verts[i1-1].xyz[2] - ts->verts[i3-1].xyz[2];
    CROSS(n2, x1, x2);
    dist = DOT(n2, n2);
    if (dist != 0.0) {
      dist   = 1.0/sqrt(dist);
      n2[0] *= dist;
      n2[1] *= dist;
      n2[2] *= dist;

      old = DOT(n1, n2);
    }
  }

  x1[0] = ts->verts[i3-1].xyz[0] - ts->verts[i1-1].xyz[0];
  x2[0] = ts->verts[i0-1].xyz[0] - ts->verts[i1-1].xyz[0];
  x1[1] = ts->verts[i3-1].xyz[1] - ts->verts[i1-1].xyz[1];
  x2[1] = ts->verts[i0-1].xyz[1] - ts->verts[i1-1].xyz[1];
  x1[2] = ts->verts[i3-1].xyz[2] - ts->verts[i1-1].xyz[2];
  x2[2] = ts->verts[i0-1].xyz[2] - ts->verts[i1-1].xyz[2];
  CROSS(n1, x1, x2);
  dist = DOT(n1, n1);
  if (dist == 0.0) return 0;
  dist   = 1.0/sqrt(dist);
  n1[0] *= dist;
  n1[1] *= dist;
  n1[2] *= dist;

  x1[0] = ts->verts[i0-1].xyz[0] - ts->verts[i2-1].xyz[0];
  x2[0] = ts->verts[i3-1].xyz[0] - ts->verts[i2-1].xyz[0];
  x1[1] = ts->verts[i0-1].xyz[1] - ts->verts[i2-1].xyz[1];
  x2[1] = ts->verts[i3-1].xyz[1] - ts->verts[i2-1].xyz[1];
  x1[2] = ts->verts[i0-1].xyz[2] - ts->verts[i2-1].xyz[2];
  x2[2] = ts->verts[i3-1].xyz[2] - ts->verts[i2-1].xyz[2];
  CROSS(n2, x1, x2);
  dist = DOT(n2, n2);
  if (dist == 0.0) return 0;
  dist   = 1.0/sqrt(dist);
  n2[0] *= dist;
  n2[1] *= dist;
  n2[2] *= dist;

  newd = DOT(n1, n2);

  if (newd > old + ANGTOL) {
    angle = EG_maxUVangle(i0-1, i1-1, i3-1, ts);
    angle = MAX(angle, EG_maxUVangle(i0-1, i3-1, i2-1, ts));
    if (angle > MAXANG) {
      ts->accum = MIN(ts->accum, old);
      return 0;
    }
    ts->accum = MIN(ts->accum, newd);
    return 1;
  }

  ts->accum = MIN(ts->accum, old);
  return 0;
}


__HOST_AND_DEVICE__ static void
EG_fillSides(int t1, double mindist, double emndist, triStruct *ts)
{
  int j, i0, i1, i2, t2;

  i0 = ts->tris[t1].indices[0]-1;
  i1 = ts->tris[t1].indices[1]-1;
  i2 = ts->tris[t1].indices[2]-1;
  ts->tris[t1].area = mindist;
  if ((ts->verts[i0].type != FACE) || (ts->verts[i1].type != FACE) ||
      (ts->verts[i2].type != FACE)) ts->tris[t1].area = emndist;

  for (j = 0; j < 3; j++) {
    ts->tris[t1].mid[j] = 0.0;
    t2 = ts->tris[t1].neighbors[j]-1;
    if (t2 < t1) continue;
    i1 = ts->tris[t1].indices[sides[j][0]]-1;
    i2 = ts->tris[t1].indices[sides[j][1]]-1;
    ts->tris[t1].mid[j] = DIST2(ts->verts[i1].xyz, ts->verts[i2].xyz);
  }
  
}


__HOST_AND_DEVICE__ static void
EG_fillMid(int t1, int close, triStruct *ts)
{
  int    i0, i1, i2;
  double uv[2], result[18];

  ts->tris[t1].close = TOBEFILLED;
  if ((ts->phase < 1) || (ts->phase > 2)) return;

  i0    = ts->tris[t1].indices[0]-1;
  i1    = ts->tris[t1].indices[1]-1;
  i2    = ts->tris[t1].indices[2]-1;
  uv[0] = (ts->verts[i0].uv[0] + ts->verts[i1].uv[0] +
           ts->verts[i2].uv[0]) / 3.0;
  uv[1] = (ts->verts[i0].uv[1] + ts->verts[i1].uv[1] +
           ts->verts[i2].uv[1]) / 3.0;
  if (EG_evaluate(ts->face, uv, result) != EGADS_SUCCESS) return;

  ts->tris[t1].mid[0] = result[0];
  ts->tris[t1].mid[1] = result[1];
  ts->tris[t1].mid[2] = result[2];
  ts->tris[t1].close  = close;
  if (close != 0) ts->tris[t1].close = EG_closeEdge(t1, ts->tris[t1].mid, ts);
}


__HOST_AND_DEVICE__ static void
EG_swapTris(int (*test)(int, int, int, triStruct *), /*@unused@*/ char *string, 
            double start, triStruct *ts)
{
  int swap, t1, t2, side, i, i0, i1, i2, i3, n11, n12, n21, n22, os, count;

  for (count = i = 0; i < ts->ntris; i++) ts->tris[i].hit  = 0;

  do {
    ts->accum = start;
    for (swap = i = 0; i < ts->ntris; i++) ts->tris[i].count = 0;

    for (t1 = 0; t1 < ts->ntris; t1++) {
      for (side = 0; side < 3; side++) {
        if ((ts->tris[t1].mark & (1 << side)) == 0) continue;
        t2 = ts->tris[t1].neighbors[side]-1;

        /* do we need to test? */

        if (t2 <= t1) continue;
        if ((ts->tris[t1].hit == 1) && (ts->tris[t2].hit == 1)) continue;

        if (test(t1, side, t2, ts) == 0) continue;
        ts->tris[t1].hit = ts->tris[t2].hit = 0;
        ts->tris[t1].count++;
        ts->tris[t2].count++;

        if (ts->phase == TOBEFILLED) {
          if (ts->tris[t1].close != TOBEFILLED) 
            EG_hadd(ts->tris[t1].indices[0], ts->tris[t1].indices[1],
                    ts->tris[t1].indices[2], ts->tris[t1].close,
                    ts->tris[t1].mid, ts);
          if (ts->tris[t2].close != TOBEFILLED) 
            EG_hadd(ts->tris[t2].indices[0], ts->tris[t2].indices[1],
                    ts->tris[t2].indices[2], ts->tris[t2].close,
                    ts->tris[t2].mid, ts);
        }

                                                os = 0;
        if  (ts->tris[t2].neighbors[1]-1 == t1) os = 1;
        if  (ts->tris[t2].neighbors[2]-1 == t1) os = 2;
        i0  = ts->tris[t1].indices[side];
        i1  = ts->tris[t1].indices[sides[side][0]];
        i2  = ts->tris[t1].indices[sides[side][1]];
        i3  = ts->tris[t2].indices[os];

        n11 = ts->tris[t1].neighbors[sides[side][0]];
        n12 = ts->tris[t1].neighbors[sides[side][1]];
        if (ts->tris[t2].indices[sides[os][0]] == i1) {
          n21 = ts->tris[t2].neighbors[sides[os][0]];
          n22 = ts->tris[t2].neighbors[sides[os][1]];
        } else {
          n22 = ts->tris[t2].neighbors[sides[os][0]];
          n21 = ts->tris[t2].neighbors[sides[os][1]];
        }

        ts->tris[t1].indices[0]   = i1;
        ts->tris[t1].indices[1]   = i3;
        ts->tris[t1].indices[2]   = i0;
        ts->tris[t1].neighbors[0] = t2+1;
        ts->tris[t1].neighbors[1] = n12;
        ts->tris[t1].neighbors[2] = n22;
        ts->tris[t1].mark         = 1;
        if (n22 > 0)
          for (i = 0; i < 3; i++)
            if (ts->tris[n22-1].neighbors[i] == t2+1)
              ts->tris[n22-1].neighbors[i] = t1+1;
        if (n12 > 0)
          if (EG_checkOr(t1, 1, n12-1, ts) != 0) {
            ts->tris[t1].mark    |= 2;
            ts->tris[n12-1].mark &= 7;
            if (ts->tris[n12-1].neighbors[0]-1 == t1) ts->tris[n12-1].mark |= 1;
            if (ts->tris[n12-1].neighbors[1]-1 == t1) ts->tris[n12-1].mark |= 2;
            if (ts->tris[n12-1].neighbors[2]-1 == t1) ts->tris[n12-1].mark |= 4;
          } else {
            if (ts->tris[n12-1].neighbors[0]-1 == t1) ts->tris[n12-1].mark &= 6;
            if (ts->tris[n12-1].neighbors[1]-1 == t1) ts->tris[n12-1].mark &= 5;
            if (ts->tris[n12-1].neighbors[2]-1 == t1) ts->tris[n12-1].mark &= 3;
          }
        if (n22 > 0)
          if (EG_checkOr(t1, 2, n22-1, ts) != 0) {
            ts->tris[t1].mark    |= 4;
            ts->tris[n22-1].mark &= 7;
            if (ts->tris[n22-1].neighbors[0]-1 == t1) ts->tris[n22-1].mark |= 1;
            if (ts->tris[n22-1].neighbors[1]-1 == t1) ts->tris[n22-1].mark |= 2;
            if (ts->tris[n22-1].neighbors[2]-1 == t1) ts->tris[n22-1].mark |= 4;
          } else {
            if (ts->tris[n22-1].neighbors[0]-1 == t1) ts->tris[n22-1].mark &= 6;
            if (ts->tris[n22-1].neighbors[1]-1 == t1) ts->tris[n22-1].mark &= 5;
            if (ts->tris[n22-1].neighbors[2]-1 == t1) ts->tris[n22-1].mark &= 3;
          }

        ts->tris[t2].indices[0]   = i2;
        ts->tris[t2].indices[1]   = i0;
        ts->tris[t2].indices[2]   = i3;
        ts->tris[t2].neighbors[0] = t1+1;
        ts->tris[t2].neighbors[1] = n21;
        ts->tris[t2].neighbors[2] = n11;
        ts->tris[t2].mark         = 1;
        if (n11 > 0)
          for (i = 0; i < 3; i++)
            if (ts->tris[n11-1].neighbors[i] == t1+1)
              ts->tris[n11-1].neighbors[i] = t2+1;
        if (n21 > 0)
          if (EG_checkOr(t2, 1, n21-1, ts) != 0) {
            ts->tris[t2].mark    |= 2;
            ts->tris[n21-1].mark &= 7;
            if (ts->tris[n21-1].neighbors[0]-1 == t2) ts->tris[n21-1].mark |= 1;
            if (ts->tris[n21-1].neighbors[1]-1 == t2) ts->tris[n21-1].mark |= 2;
            if (ts->tris[n21-1].neighbors[2]-1 == t2) ts->tris[n21-1].mark |= 4;
          } else {
            if (ts->tris[n21-1].neighbors[0]-1 == t2) ts->tris[n21-1].mark &= 6;
            if (ts->tris[n21-1].neighbors[1]-1 == t2) ts->tris[n21-1].mark &= 5;
            if (ts->tris[n21-1].neighbors[2]-1 == t2) ts->tris[n21-1].mark &= 3;
          }
        if (n11 > 0)
          if (EG_checkOr(t2, 2, n11-1, ts) != 0) {
            ts->tris[t2].mark    |= 4;
            ts->tris[n11-1].mark &= 7;
            if (ts->tris[n11-1].neighbors[0]-1 == t2) ts->tris[n11-1].mark |= 1;
            if (ts->tris[n11-1].neighbors[1]-1 == t2) ts->tris[n11-1].mark |= 2;
            if (ts->tris[n11-1].neighbors[2]-1 == t2) ts->tris[n11-1].mark |= 4;
          } else {
            if (ts->tris[n11-1].neighbors[0]-1 == t2) ts->tris[n11-1].mark &= 6;
            if (ts->tris[n11-1].neighbors[1]-1 == t2) ts->tris[n11-1].mark &= 5;
            if (ts->tris[n11-1].neighbors[2]-1 == t2) ts->tris[n11-1].mark &= 3;
          }

        i = NOTFILLED;
        if ((ts->tris[t1].close == 0) && (ts->tris[t2].close == 0)) i = 0;
        EG_fillMid(t1, i, ts);
        EG_fillMid(t2, i, ts);
        swap++;
      }
    }
    for (t1 = 0; t1 < ts->ntris; t1++)
      if (ts->tris[t1].count == 0) {
        ts->tris[t1].hit = 1;
      } else {
        ts->tris[t1].hit = 0;
      }
#ifdef DEBUG
    printf(" EG_tessellate -> swap %s: %d\n", string, swap);
#endif
    count++;
  } while ((swap != 0) && (count < 200));

  /* get the stats -- one last sweep */

  ts->accum = start;
  for (t1 = 0; t1 < ts->ntris; t1++) {
    for (side = 0; side < 3; side++) {
      if ((ts->tris[t1].mark & (1 << side)) == 0) continue;
      t2 = ts->tris[t1].neighbors[side]-1;
/*@-noeffect@*/
      if (t2 > t1) test(t1, side, t2, ts);
/*@=noeffect@*/
    }
  }
#ifdef DEBUG
  printf(" EG_tessellate -> Accumulated %s: %le\n", string, ts->accum);
#endif
}


__HOST_AND_DEVICE__ static void
EG_collapsEdge(int node, int tnode, int flag, triStruct *ts)
{
  int     i, j, nt, *tin, t[2], in[2][2], t1, t2, nn;
  triVert save;
  triTri  hold;

  /* is this a FACE node? */

  if (flag == 0)
    if (ts->verts[node-1].type != FACE) {
      printf("EGADS Internal (EG_collapsEdge): Face %d -- vert is type = %d\n",
             ts->fIndex, ts->verts[node-1].type);
      return;
    }

  /* find all tris containing the node to be removed */

  for (nt = i = 0; i < ts->ntris; i++)
    for (j = 0; j < 3; j++)
      if (ts->tris[i].indices[j] == node) nt++;
  tin = (int *) EG_alloc(nt*sizeof(int));
  if (tin == NULL) return;
 
  for (nt = i = 0; i < ts->ntris; i++)
    for (j = 0; j < 3; j++)
      if (ts->tris[i].indices[j] == node) {
        tin[nt] = i;
        nt++;
      }

  /* find the 2 tris containing the edge to be collapsed */

  for (nn = i = 0; i < nt; i++) {
    t1 = tin[i];
    for (j = 0; j < 3; j++)
      if (ts->tris[t1].indices[j] == tnode) {
        if (nn < 2) {
          t[nn]     = t1;
          in[nn][0] = j;
          in[nn][1] = 0;
          if (ts->tris[t1].indices[1] == node) in[nn][1] = 1;
          if (ts->tris[t1].indices[2] == node) in[nn][1] = 2;
        }
        nn++;
      }
  }

  if (nn != 2) {
    printf(" EGADS Internal (EG_collapsEdge): Face %d -- ntris on side = %d\n",
           ts->fIndex, nn);
    EG_free(tin);
    return;
  }
  for (nn = i = 0; i < nt; i++) 
    if ((tin[i] != t[0]) && (tin[i] != t[1])) {
      tin[nn] = tin[i];
      nn++;
    }
  nt -= 2;

  t1 = ts->ntris-2;
  t2 = t1 + 1;

  /* move node to the end of the node list */

  if (ts->nverts != node) {
    save                    = ts->verts[ts->nverts-1];
    ts->verts[ts->nverts-1] = ts->verts[node-1];
    ts->verts[node-1]       = save;
    for (i = 0; i < ts->ntris; i++)
      for (j = 0; j < 3; j++)
        if (ts->tris[i].indices[j] == node) {
          ts->tris[i].indices[j] = ts->nverts;
        } else if (ts->tris[i].indices[j] == ts->nverts) {
          ts->tris[i].indices[j] = node;
        }
  }
  if (ts->nverts == tnode) {
    for (i = 0; i < ts->ntris; i++)
      for (j = 0; j < 3; j++)
        if (ts->tris[i].indices[j] == ts->nverts)
          ts->tris[i].indices[j] = node;
  } else {
    for (i = 0; i < ts->ntris; i++)
      for (j = 0; j < 3; j++)
        if (ts->tris[i].indices[j] == ts->nverts) 
          ts->tris[i].indices[j] = tnode;
  }

  /* shift 2 tris to the end of the tris list */

  if (t1 != t[0]) {
    hold           = ts->tris[t1];
    ts->tris[t1]   = ts->tris[t[0]];
    ts->tris[t[0]] = hold;
    if (t[1] == t1) t[1] = t[0];
    for (i = 0; i < ts->ntris; i++)
      for (j = 0; j < 3; j++)
        if (ts->tris[i].neighbors[j] == t1+1) {
          ts->tris[i].neighbors[j] = t[0]+1;
        } else if (ts->tris[i].neighbors[j] == t[0]+1) {
          ts->tris[i].neighbors[j] = t1+1;
        }
  }
  t[0] = t1;
  if (t2 != t[1]) {
    hold           = ts->tris[t2];
    ts->tris[t2]   = ts->tris[t[1]];
    ts->tris[t[1]] = hold;
    for (i = 0; i < ts->ntris; i++)
      for (j = 0; j < 3; j++)
        if (ts->tris[i].neighbors[j] == t2+1) {
          ts->tris[i].neighbors[j] = t[1]+1;
        } else if (ts->tris[i].neighbors[j] == t[1]+1) {
          ts->tris[i].neighbors[j] = t2+1;
        }
  }
  t[1] = t2;

  /* patch up neighbors for the removed tris */

  for (i = 0; i < 2; i++) {
    t1 = ts->tris[t[i]].neighbors[in[i][0]];
    t2 = ts->tris[t[i]].neighbors[in[i][1]];
    for (j = 0; j < 3; j++) {
      if (t1 > 0)
        if (ts->tris[t1-1].neighbors[j] == t[i]+1)
          ts->tris[t1-1].neighbors[j] = t2;
      if (t2 > 0)
        if (ts->tris[t2-1].neighbors[j] == t[i]+1)
          ts->tris[t2-1].neighbors[j] = t1;
    }
  }

  ts->nverts--;
  ts->ntris -= 2;
#ifdef DEBUG
  EG_checkTess(ts);
#endif

  /* fix up the modified triangles */

  for (i = 0; i < nt; i++) ts->tris[tin[i]].mark = 0;
  for (i = 0; i < nt; i++) {
    t1 = tin[i];
    for (j = 0; j < 3; j++) {
      t2 = ts->tris[t1].neighbors[j];
      if (t2 <= 0) continue;
      if (EG_checkOr(t1, j, t2-1, ts) != 0) {
        ts->tris[t1].mark |= 1 << j;
        if (ts->tris[t2-1].neighbors[0]-1 == t1) ts->tris[t2-1].mark |= 1;
        if (ts->tris[t2-1].neighbors[1]-1 == t1) ts->tris[t2-1].mark |= 2;
        if (ts->tris[t2-1].neighbors[2]-1 == t1) ts->tris[t2-1].mark |= 4;
      } else {
        if (ts->tris[t2-1].neighbors[0]-1 == t1) ts->tris[t2-1].mark &= 6;
        if (ts->tris[t2-1].neighbors[1]-1 == t1) ts->tris[t2-1].mark &= 5;
        if (ts->tris[t2-1].neighbors[2]-1 == t1) ts->tris[t2-1].mark &= 3;
      }
    }
    EG_fillMid(t1, NOTFILLED, ts);
  }

  EG_free(tin);
}


__HOST_AND_DEVICE__ static void
EG_zeroArea(triStruct *ts, int outLevel, long tID)
{
  int    i, stat, side, i0, i1, i2, s0, s1, per, other;
  int    pti0[2], pti1[2], pti2[2];
  double smallu, smallv, range[4], x1[3], x2[3], n[3];

  stat = EG_getRange(ts->face, range, &per);
  if (stat != EGADS_SUCCESS) {
    printf("%lX Face %d: EG_getRange = %d (zeroArea)!\n",
           tID, ts->fIndex, stat);
    return;
  }
  /* double the size used in egadsTess */
  smallu = 0.0001*(range[1] - range[0]);
  smallv = 0.0001*(range[3] - range[2]);
  
  for (i = 0; i < ts->ntris; i++) {
    i0    = ts->tris[i].indices[0]-1;
    i1    = ts->tris[i].indices[1]-1;
    i2    = ts->tris[i].indices[2]-1;
    x1[0] = ts->verts[i1].xyz[0] - ts->verts[i0].xyz[0];
    x2[0] = ts->verts[i2].xyz[0] - ts->verts[i0].xyz[0];
    x1[1] = ts->verts[i1].xyz[1] - ts->verts[i0].xyz[1];
    x2[1] = ts->verts[i2].xyz[1] - ts->verts[i0].xyz[1];
    x1[2] = ts->verts[i1].xyz[2] - ts->verts[i0].xyz[2];
    x2[2] = ts->verts[i2].xyz[2] - ts->verts[i0].xyz[2];
    CROSS(n, x1, x2);
    if (DOT(n, n) != 0.0) continue;
   
    /* zero area -- get ptype/pindex for verts */
    pti0[0] = pti0[1] = pti1[0] = pti1[1] = pti2[0] = pti2[1] = -1;
    if (ts->verts[i0].type == NODE) {
      pti0[0] = 0;
      pti0[1] = ts->verts[i0].index;
    } else if (ts->verts[i0].type == EDGE) {
      pti0[0] = ts->verts[i0].index;
      pti0[1] = ts->verts[i0].edge;
    }
    if (ts->verts[i1].type == NODE) {
      pti1[0] = 0;
      pti1[1] = ts->verts[i1].index;
    } else if (ts->verts[i1].type == EDGE) {
      pti1[0] = ts->verts[i1].index;
      pti1[1] = ts->verts[i1].edge;
    }
    if (ts->verts[i2].type == NODE) {
      pti2[0] = 0;
      pti2[1] = ts->verts[i2].index;
    } else if (ts->verts[i2].type == EDGE) {
      pti2[0] = ts->verts[i2].index;
      pti2[1] = ts->verts[i2].edge;
    }
    if ((pti0[0] == -1) || (pti1[0] == -1) || (pti2[0] == -1)) continue;
                                                     side = -1;
    if ((pti1[0] == pti2[0]) && (pti1[1] = pti2[1])) side =  0;
    if ((pti0[0] == pti2[0]) && (pti0[1] = pti2[1])) side =  1;
    if ((pti0[0] == pti1[0]) && (pti0[1] = pti1[1])) side =  2;
    if (side == -1) continue;
    other = ts->tris[i].neighbors[side];
    if (other < 0)  continue;
    
    s0 = ts->tris[i].indices[sides[side][0]]-1;
    s1 = ts->tris[i].indices[sides[side][1]]-1;
    if (fabs(ts->verts[s0].uv[0] - ts->verts[s1].uv[0]) > smallu) continue;
    if (fabs(ts->verts[s0].uv[1] - ts->verts[s1].uv[1]) > smallv) continue;
    if (outLevel > 0)
      printf("%lX Face %d: Zero area %d/%d %d -- %d %d  %d %d  %d %d\n", tID,
             ts->fIndex, i+1, side, other, pti0[0], pti0[1], pti1[0], pti1[1],
             pti2[0], pti2[1]);

    /* get rid of these triangles */
    EG_collapsEdge(s0+1, s1+1, 1, ts);
  }
}


__HOST_AND_DEVICE__ static int
EG_checkQuadding(int outLevel, int flag, triStruct *ts, long tID)
{
  int    stat, i, i0, i1, i2;
  double d, x1[3], x2[3], n[3], nor[3], uv[2], *u, *v, result[18];
  
  /* are we from a degenerate mapping? If so, don't check */
  if (flag == 1) return EGADS_SUCCESS;

  for (i = 0; i < ts->ntris; i++) {
    i0    = ts->tris[i].indices[0]-1;
    i1    = ts->tris[i].indices[1]-1;
    i2    = ts->tris[i].indices[2]-1;
    x1[0] = ts->verts[i1].xyz[0] - ts->verts[i0].xyz[0];
    x2[0] = ts->verts[i2].xyz[0] - ts->verts[i0].xyz[0];
    x1[1] = ts->verts[i1].xyz[1] - ts->verts[i0].xyz[1];
    x2[1] = ts->verts[i2].xyz[1] - ts->verts[i0].xyz[1];
    x1[2] = ts->verts[i1].xyz[2] - ts->verts[i0].xyz[2];
    x2[2] = ts->verts[i2].xyz[2] - ts->verts[i0].xyz[2];
    uv[0] = (ts->verts[i0].uv[0] + ts->verts[i1].uv[0] +
             ts->verts[i2].uv[0])/3.0;
    uv[1] = (ts->verts[i0].uv[1] + ts->verts[i1].uv[1] +
             ts->verts[i2].uv[1])/3.0;
    CROSS(n, x1, x2);
    d = sqrt(DOT(n,n));
    if (d == 0.0) {
      printf("%lX Face %d: Quad tri = %d Zero Area!\n", tID, ts->fIndex, i+1);
      return EGADS_DEGEN;
    }
    n[0] /= d;
    n[1] /= d;
    n[2] /= d;
    stat = EG_evaluate(ts->face, uv, result);
    if (stat != EGADS_SUCCESS) {
      printf("%lX Face %d: Quad tri = %d EG_evaluate = %d!\n",
             tID, ts->fIndex, i+1, stat);
      return stat;
    }
    u = &result[3];
    v = &result[6];
    CROSS(nor, u, v);
    d = sqrt(DOT(nor,nor));
    if (d == 0.0) {
      printf("%lX Face %d: Quad tri = %d Zero Normal!\n", tID, ts->fIndex, i+1);
      return EGADS_DEGEN;
    }
    nor[0] /= d;
    nor[1] /= d;
    nor[2] /= d;
    d = DOT(n, nor);
    if (d <= 0.0) {
      if (outLevel > 1)
        printf("%lX Face %d: Quad tri = %d dot = %lf!\n",
               tID, ts->fIndex, i+1, d);
      return EGADS_DEGEN;
    }
  }
  
  return EGADS_SUCCESS;
}


__HOST_AND_DEVICE__ static int
EG_splitTri(int t0, double *uv, double *point, triStruct *ts)
{
  int    i, j, n, node, indices[3], neighbr[3], t1, t2, t[3];
  triTri *tmp;

  if (ts->ntris+1 >= ts->mtris) {
    n   = ts->mtris + CHUNK;
    tmp = (triTri *) EG_reall(ts->tris, n*sizeof(triTri));
    if (tmp == NULL) return EGADS_MALLOC;
    ts->tris  = tmp;
    ts->mtris = n;
#ifdef DEBUG
    printf(" Realloc Tris: now %d (%d)\n", n, ts->ntris);
#endif
  }

  node = EG_addVert(FACE, 0, 0, point, uv, ts);
  if (node < EGADS_SUCCESS) return node;

  for (i = 0; i < 3; i++) {
    indices[i] = ts->tris[t0].indices[i];
    neighbr[i] = ts->tris[t0].neighbors[i];
  }

  /* fill in the tri structures */
  
  t1 = ts->ntris;
  t2 = t1 + 1;
  ts->ntris += 2;

  ts->tris[t0].mark         = 0;
  ts->tris[t0].indices[2]   = node;
  ts->tris[t0].neighbors[0] = t1+1;
  ts->tris[t0].neighbors[1] = t2+1;

  ts->tris[t1].mark         = 0;
  ts->tris[t1].indices[0]   = indices[1];
  ts->tris[t1].indices[1]   = indices[2];
  ts->tris[t1].indices[2]   = node;
  ts->tris[t1].neighbors[0] = t2+1;
  ts->tris[t1].neighbors[1] = t0+1;
  ts->tris[t1].neighbors[2] = neighbr[0];
  if (neighbr[0] > 0) {
    j = 0;
    if (ts->tris[neighbr[0]-1].neighbors[1] == t0+1) j = 1;
    if (ts->tris[neighbr[0]-1].neighbors[2] == t0+1) j = 2;
    ts->tris[neighbr[0]-1].neighbors[j] = t1+1;
  }

  ts->tris[t2].mark         = 0;
  ts->tris[t2].indices[0]   = indices[2];
  ts->tris[t2].indices[1]   = indices[0];
  ts->tris[t2].indices[2]   = node;
  ts->tris[t2].neighbors[0] = t0+1;
  ts->tris[t2].neighbors[1] = t1+1;
  ts->tris[t2].neighbors[2] = neighbr[1];
  if (neighbr[1] > 0) {
    j = 0;
    if (ts->tris[neighbr[1]-1].neighbors[1] == t0+1) j = 1;
    if (ts->tris[neighbr[1]-1].neighbors[2] == t0+1) j = 2;
    ts->tris[neighbr[1]-1].neighbors[j] = t2+1;
  }
  EG_fillMid(t0, NOTFILLED, ts);
  EG_fillMid(t1, NOTFILLED, ts);
  EG_fillMid(t2, NOTFILLED, ts);

  t[0] = t0; t[1] = t1; t[2] = t2;
  for (i = 0; i < 3; i++) {
    t1 = t[i];
    for (j = 0; j < 3; j++) {
      n = ts->tris[t1].neighbors[j];
      if (n <= 0) continue;
      if (EG_checkOr(t1, j, n-1, ts) != 0) {
        ts->tris[t1].mark |= 1 << j;
        if (ts->tris[n-1].neighbors[0]-1 == t1) ts->tris[n-1].mark |= 1;
        if (ts->tris[n-1].neighbors[1]-1 == t1) ts->tris[n-1].mark |= 2;
        if (ts->tris[n-1].neighbors[2]-1 == t1) ts->tris[n-1].mark |= 4;
      } else {
        if (ts->tris[n-1].neighbors[0]-1 == t1) ts->tris[n-1].mark &= 6;
        if (ts->tris[n-1].neighbors[1]-1 == t1) ts->tris[n-1].mark &= 5;
        if (ts->tris[n-1].neighbors[2]-1 == t1) ts->tris[n-1].mark &= 3;
      }
    }
  }

  return EGADS_SUCCESS;
}


__HOST_AND_DEVICE__ static int
EG_splitSide(int t1, int side, int t2, int sideMid, triStruct *ts)
{
  int    i, j, n, node, status, t[4], i0, i1, i2, i3, n11, n12, n21, n22, os;
  double point[18], xyz[3], uv[2], d0, d1, d2, a1, a2;
  triTri *tmp;

  if (ts->ntris+1 >= ts->mtris) {
    n   = ts->mtris + CHUNK;
    tmp = (triTri *) EG_reall(ts->tris, n*sizeof(triTri));
    if (tmp == NULL) return EGADS_MALLOC;
    ts->tris  = tmp;
    ts->mtris = n;
#ifdef DEBUG
    printf(" Realloc Tris: now %d (%d)\n", n, ts->ntris);
#endif
  }

                                         os = 0;
  if (ts->tris[t2].neighbors[1] == t1+1) os = 1;
  if (ts->tris[t2].neighbors[2] == t1+1) os = 2;

  i0 = ts->tris[t1].indices[side];
  i1 = ts->tris[t1].indices[sides[side][0]];
  i2 = ts->tris[t1].indices[sides[side][1]];
  i3 = ts->tris[t2].indices[os];
  a1 = AREA2D(ts->verts[i0-1].uv, ts->verts[i1-1].uv, ts->verts[i3-1].uv);
  a2 = AREA2D(ts->verts[i0-1].uv, ts->verts[i3-1].uv, ts->verts[i2-1].uv);
  if (a1*a2       <= 0.0) return EGADS_DEGEN;
  if (a1*ts->orUV <  0.0) return EGADS_DEGEN;
  if (((ts->verts[i1-1].type == NODE) && (ts->verts[i1-1].edge == -1)) ||
      ((ts->verts[i2-1].type == NODE) && (ts->verts[i2-1].edge == -1))) {
    xyz[0] = 0.5*(ts->verts[i1-1].xyz[0] + ts->verts[i2-1].xyz[0]);
    xyz[1] = 0.5*(ts->verts[i1-1].xyz[1] + ts->verts[i2-1].xyz[1]);
    xyz[2] = 0.5*(ts->verts[i1-1].xyz[2] + ts->verts[i2-1].xyz[2]);
    status = EG_invEvaluate(ts->face, xyz, uv, point);
    if ((a1*AREA2D(ts->verts[i0-1].uv, ts->verts[i1-1].uv, uv) <= 0.0) ||
        (a1*AREA2D(ts->verts[i0-1].uv, uv, ts->verts[i2-1].uv) <= 0.0) ||
        (a1*AREA2D(ts->verts[i1-1].uv, ts->verts[i3-1].uv, uv) <= 0.0) ||
        (a1*AREA2D(uv, ts->verts[i3-1].uv, ts->verts[i2-1].uv) <= 0.0)) {
/*    printf(" Reject side Face %d\n", ts->fIndex);  */
      uv[0]  = 0.5*(ts->verts[i1-1].uv[0] + ts->verts[i2-1].uv[0]);
      uv[1]  = 0.5*(ts->verts[i1-1].uv[1] + ts->verts[i2-1].uv[1]);
      status = EG_evaluate(ts->face, uv, point);
    }
  } else {
    uv[0]  = 0.5*(ts->verts[i1-1].uv[0] + ts->verts[i2-1].uv[0]);
    uv[1]  = 0.5*(ts->verts[i1-1].uv[1] + ts->verts[i2-1].uv[1]);
    status = EG_evaluate(ts->face, uv, point);
  }
  if (status != EGADS_SUCCESS) return status;

  if (sideMid == 1) {
    d0 = DIST2(ts->verts[i1-1].xyz, ts->verts[i2-1].xyz);
    d1 = DIST2(point,               ts->verts[i2-1].xyz);
    d2 = DIST2(ts->verts[i1-1].xyz, point);
    if ((d1/d0 < 0.125) || (d2/d0 < 0.125)) return EGADS_RANGERR;
  }

  node = EG_addVert(FACE, 0, 0, point, uv, ts);
  if (node < EGADS_SUCCESS) return node;

  n11 = ts->tris[t1].neighbors[sides[side][0]];
  n12 = ts->tris[t1].neighbors[sides[side][1]];
  if (ts->tris[t2].indices[sides[os][0]] == i1) {
    n21 = ts->tris[t2].neighbors[sides[os][0]];
    n22 = ts->tris[t2].neighbors[sides[os][1]];
  } else {
    n22 = ts->tris[t2].neighbors[sides[os][0]];
    n21 = ts->tris[t2].neighbors[sides[os][1]];
  }

  /* fill in the tri structures */
  
  t[0] = t1; t[1] = t2; t[2] = ts->ntris; t[3] = t[2] + 1;
  ts->ntris += 2;

  ts->tris[t[0]].mark         = 0;
  ts->tris[t[0]].indices[0]   = i0;
  ts->tris[t[0]].indices[1]   = i1;
  ts->tris[t[0]].indices[2]   = node;
  ts->tris[t[0]].neighbors[0] = t[1]+1;
  ts->tris[t[0]].neighbors[1] = t[2]+1;
  ts->tris[t[0]].neighbors[2] = n12;

  ts->tris[t[1]].mark         = 0;
  ts->tris[t[1]].indices[0]   = i1;
  ts->tris[t[1]].indices[1]   = i3;
  ts->tris[t[1]].indices[2]   = node;
  ts->tris[t[1]].neighbors[0] = t[3]+1;
  ts->tris[t[1]].neighbors[1] = t[0]+1;
  ts->tris[t[1]].neighbors[2] = n22;

  ts->tris[t[2]].mark         = 0;
  ts->tris[t[2]].indices[0]   = i2;
  ts->tris[t[2]].indices[1]   = i0;
  ts->tris[t[2]].indices[2]   = node;
  ts->tris[t[2]].neighbors[0] = t[0]+1;
  ts->tris[t[2]].neighbors[1] = t[3]+1;
  ts->tris[t[2]].neighbors[2] = n11;
  if (n11 > 0) {
    j = 0;
    if (ts->tris[n11-1].neighbors[1] == t[0]+1) j = 1;
    if (ts->tris[n11-1].neighbors[2] == t[0]+1) j = 2;
    ts->tris[n11-1].neighbors[j] = t[2]+1;
  }

  ts->tris[t[3]].mark         = 0;
  ts->tris[t[3]].indices[0]   = i3;
  ts->tris[t[3]].indices[1]   = i2;
  ts->tris[t[3]].indices[2]   = node;
  ts->tris[t[3]].neighbors[0] = t[2]+1;
  ts->tris[t[3]].neighbors[1] = t[1]+1;
  ts->tris[t[3]].neighbors[2] = n21;
  if (n21 > 0) {
    j = 0;
    if (ts->tris[n21-1].neighbors[1] == t[1]+1) j = 1;
    if (ts->tris[n21-1].neighbors[2] == t[1]+1) j = 2;
    ts->tris[n21-1].neighbors[j] = t[3]+1;
  }
  i = NOTFILLED;
  if ((ts->tris[t1].close == 0) && (ts->tris[t2].close == 0)) i = 0;
  EG_fillMid(t[0], i, ts);
  EG_fillMid(t[1], i, ts);
  EG_fillMid(t[2], i, ts);
  EG_fillMid(t[3], i, ts);

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 3; j++) {
      n = ts->tris[t[i]].neighbors[j];
      if (n <= 0) continue;
      if (EG_checkOr(t[i], j, n-1, ts) != 0) {
        ts->tris[t[i]].mark |= 1 << j;
        if (ts->tris[n-1].neighbors[0]-1 == t[i]) ts->tris[n-1].mark |= 1;
        if (ts->tris[n-1].neighbors[1]-1 == t[i]) ts->tris[n-1].mark |= 2;
        if (ts->tris[n-1].neighbors[2]-1 == t[i]) ts->tris[n-1].mark |= 4;
      } else {
        if (ts->tris[n-1].neighbors[0]-1 == t[i]) ts->tris[n-1].mark &= 6;
        if (ts->tris[n-1].neighbors[1]-1 == t[i]) ts->tris[n-1].mark &= 5;
        if (ts->tris[n-1].neighbors[2]-1 == t[i]) ts->tris[n-1].mark &= 3;
      }
    }
  }
  return EGADS_SUCCESS;
}


__HOST_AND_DEVICE__ static double
EG_dotNorm(double *p0, double *p1, double *p2, double *p3)
{
  double x1[3], x2[3], n1[3], n2[3], dist;

  x1[0] = p1[0] - p0[0];
  x2[0] = p2[0] - p0[0];
  x1[1] = p1[1] - p0[1];
  x2[1] = p2[1] - p0[1];
  x1[2] = p1[2] - p0[2];
  x2[2] = p2[2] - p0[2];
  CROSS(n1, x1, x2);
  dist = DOT(n1, n1);
  if (dist == 0.0) return 1.0;
  dist   = 1.0/sqrt(dist);
  n1[0] *= dist;
  n1[1] *= dist;
  n1[2] *= dist;

  x1[0] = p2[0] - p3[0];
  x2[0] = p1[0] - p3[0];
  x1[1] = p2[1] - p3[1];
  x2[1] = p1[1] - p3[1];
  x1[2] = p2[2] - p3[2];
  x2[2] = p1[2] - p3[2];
  CROSS(n2, x1, x2);
  dist = DOT(n2, n2);
  if (dist == 0.0) return 1.0;
  dist   = 1.0/sqrt(dist);
  n2[0] *= dist;
  n2[1] *= dist;
  n2[2] *= dist;

  return DOT(n1, n2);
}


__HOST_AND_DEVICE__ static void
EG_floodTriGraph(int t, int depth, triStruct *ts)
{
  int tn;

  if (depth <= 0) return;
  ts->tris[t].hit = 1;

  tn = ts->tris[t].neighbors[0];
  if (tn > 0) EG_floodTriGraph(tn-1, depth-1, ts);
  tn = ts->tris[t].neighbors[1];
  if (tn > 0) EG_floodTriGraph(tn-1, depth-1, ts);
  tn = ts->tris[t].neighbors[2];
  if (tn > 0) EG_floodTriGraph(tn-1, depth-1, ts);
}


__HOST_AND_DEVICE__ static int
EG_breakTri(int mode, int stri, int *eg_split, triStruct *ts)
{
  int    i, j, side, i0, i1, i2, i3, t1, t2, split;
  double uv[2], xyz[18], x1[3], x2[3], n[3], area, mina, dot, a;

  /* initialize area if new tessellation */

  for (split = i = 0; i < ts->ntris; i++) {
    ts->tris[i].hit = 1;

    /* compute 3D area */

    i0 = ts->tris[i].indices[0]-1;
    i1 = ts->tris[i].indices[1]-1;
    i2 = ts->tris[i].indices[2]-1;
    if (EG_maxUVangle(i0, i1, i2, ts) > CUTANG) continue;
    x1[0] = ts->verts[i1].xyz[0] - ts->verts[i0].xyz[0];
    x2[0] = ts->verts[i2].xyz[0] - ts->verts[i0].xyz[0];
    x1[1] = ts->verts[i1].xyz[1] - ts->verts[i0].xyz[1];
    x2[1] = ts->verts[i2].xyz[1] - ts->verts[i0].xyz[1];
    x1[2] = ts->verts[i1].xyz[2] - ts->verts[i0].xyz[2];
    x2[2] = ts->verts[i2].xyz[2] - ts->verts[i0].xyz[2];
    CROSS(n, x1, x2);
    ts->tris[i].area = DOT(n, n);
    if (ts->tris[i].area == 0.0) continue;

    /* skip if more than 1 edge  or  dot of normals is OK (mode = -1) */
    dot  = 1.0;
    mina = DBL_MAX;
    for (j = side = 0; side < 3; side++)
      if (ts->tris[i].neighbors[side] > 0) {
        j++;
        if (mode == -1) {
          t2 = ts->tris[i].neighbors[side]-1;
          i0 = ts->tris[i].indices[side];
          i1 = ts->tris[i].indices[sides[side][0]];
          i2 = ts->tris[i].indices[sides[side][1]];
          i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
               ts->tris[t2].indices[2] - i1 - i2;
          if ((i3 < 1) || (i3 > ts->nverts)) continue;
          dot = MIN(dot,EG_dotNorm(ts->verts[i0-1].xyz, ts->verts[i1-1].xyz,
                                   ts->verts[i2-1].xyz, ts->verts[i3-1].xyz));
          x1[0] = ts->verts[i1-1].xyz[0] - ts->verts[i3-1].xyz[0];
          x2[0] = ts->verts[i2-1].xyz[0] - ts->verts[i3-1].xyz[0];
          x1[1] = ts->verts[i1-1].xyz[1] - ts->verts[i3-1].xyz[1];
          x2[1] = ts->verts[i2-1].xyz[1] - ts->verts[i3-1].xyz[1];
          x1[2] = ts->verts[i1-1].xyz[2] - ts->verts[i3-1].xyz[2];
          x2[2] = ts->verts[i2-1].xyz[2] - ts->verts[i3-1].xyz[2];
          CROSS(n, x1, x2);
          mina = MIN(mina, DOT(n, n));
        }
      }
    if (j <= 1) continue;
    if (mode == -1)
      if ((dot > -0.9) && (mina/ts->tris[i].area > 0.001)) continue;

    /* are we too small? */
    i0 = ts->tris[i].indices[0]-1;
    i1 = ts->tris[i].indices[1]-1;
    i2 = ts->tris[i].indices[2]-1;
    if (DIST2(ts->verts[i1].xyz, ts->verts[i2].xyz) <= ts->eps2) continue;
    if (DIST2(ts->verts[i1].xyz, ts->verts[i0].xyz) <= ts->eps2) continue;
    if (DIST2(ts->verts[i0].xyz, ts->verts[i2].xyz) <= ts->eps2) continue;

    /* mark as OK */
    ts->tris[i].hit = 0;
  }
  
  do {

    /* pick the largest area */

    t1   = -1;
    area = 0.0;
    for (i = 0; i < ts->ntris; i++) {
      if (ts->tris[i].hit != 0) continue;
      if (ts->tris[i].area > area) {
        t1   = i;
        area = ts->tris[i].area;
      }
    }
    if (t1 == -1) continue;

    /* are we a valid candidate? */

    ts->tris[t1].hit = 1;
    i0    = ts->tris[t1].indices[0]-1;
    i1    = ts->tris[t1].indices[1]-1;
    i2    = ts->tris[t1].indices[2]-1;
    uv[0] = (ts->verts[i0].uv[0] + ts->verts[i1].uv[0] +
             ts->verts[i2].uv[0]) / 3.0;
    uv[1] = (ts->verts[i0].uv[1] + ts->verts[i1].uv[1] +
             ts->verts[i2].uv[1]) / 3.0;

    if (EG_evaluate(ts->face, uv, xyz) != EGADS_SUCCESS) continue;
    if (mode == 0) {
      if (EG_hfind(i0, i1, i2, &j, xyz, ts) == NOTFILLED) {
        if (((ts->verts[i0].type == NODE) && (ts->verts[i0].edge == -1)) ||
            ((ts->verts[i1].type == NODE) && (ts->verts[i1].edge == -1)) ||
            ((ts->verts[i2].type == NODE) && (ts->verts[i2].edge == -1))) {
          if (EG_inTri(t1, xyz, 0.1, ts) == 0) {
            x1[0] = (ts->verts[i0].xyz[0] + ts->verts[i1].xyz[0] +
                     ts->verts[i2].xyz[0]) / 3.0;
            x1[1] = (ts->verts[i0].xyz[1] + ts->verts[i1].xyz[1] +
                     ts->verts[i2].xyz[1]) / 3.0;
            x1[2] = (ts->verts[i0].xyz[2] + ts->verts[i1].xyz[2] +
                     ts->verts[i2].xyz[2]) / 3.0;
            if (EG_invEvaluate(ts->face, x1, uv, xyz) != EGADS_SUCCESS) continue;
            a = AREA2D(ts->verts[i0].uv, ts->verts[i1].uv, ts->verts[i2].uv);
            if ((a*AREA2D(ts->verts[i0].uv, ts->verts[i1].uv, uv) <= 0.0) ||
                (a*AREA2D(ts->verts[i1].uv, ts->verts[i2].uv, uv) <= 0.0) ||
                (a*AREA2D(ts->verts[i2].uv, ts->verts[i0].uv, uv) <= 0.0)) {
              uv[0] = (ts->verts[i0].uv[0] + ts->verts[i1].uv[0] +
                       ts->verts[i2].uv[0]) / 3.0;
              uv[1] = (ts->verts[i0].uv[1] + ts->verts[i1].uv[1] +
                       ts->verts[i2].uv[1]) / 3.0;
              if (EG_evaluate(ts->face, uv, xyz) != EGADS_SUCCESS) continue;
            }
          }
        }
        EG_hadd(i0, i1, i2, 0, xyz, ts);
      }

      if (EG_inTri(t1, xyz, 0.0001, ts) == 0) continue;
      if (EG_dotNorm(ts->verts[i0].xyz, ts->verts[i1].xyz, xyz,
                                        ts->verts[i2].xyz) < -0.98) continue;
      if (EG_dotNorm(ts->verts[i1].xyz, ts->verts[i2].xyz, xyz,
                                        ts->verts[i0].xyz) < -0.98) continue;
      if (EG_dotNorm(ts->verts[i2].xyz, ts->verts[i0].xyz, xyz,
                                        ts->verts[i1].xyz) < -0.98) continue;
    } else {
      if (EG_inTri(t1, xyz, 0.0001, ts) == 1) continue;
      x1[0] = (ts->verts[i0].xyz[0] + ts->verts[i1].xyz[0] +
               ts->verts[i2].xyz[0]) / 3.0;
      x1[1] = (ts->verts[i0].xyz[1] + ts->verts[i1].xyz[1] +
               ts->verts[i2].xyz[1]) / 3.0;
      x1[2] = (ts->verts[i0].xyz[2] + ts->verts[i1].xyz[2] +
               ts->verts[i2].xyz[2]) / 3.0;
      if (EG_invEvaluate(ts->face, x1, uv, xyz) != EGADS_SUCCESS) continue;
      a = AREA2D(ts->verts[i0].uv, ts->verts[i1].uv, ts->verts[i2].uv);
      if ((a*AREA2D(ts->verts[i0].uv, ts->verts[i1].uv, uv) <= 0.0) ||
          (a*AREA2D(ts->verts[i1].uv, ts->verts[i2].uv, uv) <= 0.0) ||
          (a*AREA2D(ts->verts[i2].uv, ts->verts[i0].uv, uv) <= 0.0)) {
        uv[0] = (ts->verts[i0].uv[0] + ts->verts[i1].uv[0] +
                 ts->verts[i2].uv[0]) / 3.0;
        uv[1] = (ts->verts[i0].uv[1] + ts->verts[i1].uv[1] +
                 ts->verts[i2].uv[1]) / 3.0;
        if (EG_evaluate(ts->face, uv, xyz) != EGADS_SUCCESS) continue;
      }
    }
    if (EG_closeEdge(t1, xyz, ts) == 1) continue;

    if (EG_splitTri(t1, uv, xyz, ts) != EGADS_SUCCESS) continue;

    /* successful addition! */

        split++;
    *eg_split += 1;
    if (*eg_split > stri) {
      *eg_split = 0;
      break;
    }
    EG_floodTriGraph(t1, FLOODEPTH, ts);

  } while (t1 != -1);

  return split;
}


__HOST_AND_DEVICE__ static int
EG_addFacetNorm(triStruct *ts)
{
  int    i, i0, i1, i2, i3, t1, t2, side, total, split;
  double d, dot, area, uv[2], x1[3], x2[3], n[3], mid[3];

  total = ts->ntris;
  for (split = t1 = 0; t1 < total; t1++) {
    if (ts->tris[t1].close != 0) continue;

    /* do we have 2 edges? */

    for (i = side = 0; side < 3; side++)
      if (ts->tris[t1].neighbors[side] > 0) i++;
    if (i <= 1) continue;

    i0 = ts->tris[t1].indices[0]-1;
    i1 = ts->tris[t1].indices[1]-1;
    i2 = ts->tris[t1].indices[2]-1;
/*  if (((ts->verts[i0].type == NODE) && (ts->verts[i0].edge == -1)) ||
        ((ts->verts[i1].type == NODE) && (ts->verts[i1].edge == -1)) ||
        ((ts->verts[i2].type == NODE) && (ts->verts[i2].edge == -1)))
      continue;  */
    if (EG_maxUVangle(i0, i1, i2, ts) > CUTANG) continue;

    mid[0] = ts->tris[t1].mid[0];
    mid[1] = ts->tris[t1].mid[1];
    mid[2] = ts->tris[t1].mid[2];
/*  if (EG_inTri(t1, mid, 0.1, ts) == 1) continue;  */
    if (DIST2(ts->verts[i0].xyz, mid) < 0.001*ts->edist2) continue;
    if (DIST2(ts->verts[i1].xyz, mid) < 0.001*ts->edist2) continue;
    if (DIST2(ts->verts[i2].xyz, mid) < 0.001*ts->edist2) continue;

    x1[0] = ts->verts[i1].xyz[0] - ts->verts[i0].xyz[0];
    x2[0] = ts->verts[i2].xyz[0] - ts->verts[i0].xyz[0];
    x1[1] = ts->verts[i1].xyz[1] - ts->verts[i0].xyz[1];
    x2[1] = ts->verts[i2].xyz[1] - ts->verts[i0].xyz[1];
    x1[2] = ts->verts[i1].xyz[2] - ts->verts[i0].xyz[2];
    x2[2] = ts->verts[i2].xyz[2] - ts->verts[i0].xyz[2];
    CROSS(n, x1, x2);
    area  = DOT(n, n);

    uv[0] = (ts->verts[i0].uv[0] + ts->verts[i1].uv[0] +
             ts->verts[i2].uv[0]) / 3.0;
    uv[1] = (ts->verts[i0].uv[1] + ts->verts[i1].uv[1] +
             ts->verts[i2].uv[1]) / 3.0;

    dot = 1.0;
    for (side = 0; side < 3; side++) {
      t2 = ts->tris[t1].neighbors[side]-1;
      if (t2 < 0) continue;

      i0 = ts->tris[t1].indices[side];
      i1 = ts->tris[t1].indices[sides[side][0]];
      i2 = ts->tris[t1].indices[sides[side][1]];
      i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
           ts->tris[t2].indices[2] - i1 - i2;
      if ((i3 < 1) || (i3 > ts->nverts)) continue;
      x1[0] = ts->verts[i1-1].xyz[0] - ts->verts[i3-1].xyz[0];
      x2[0] = ts->verts[i2-1].xyz[0] - ts->verts[i3-1].xyz[0];
      x1[1] = ts->verts[i1-1].xyz[1] - ts->verts[i3-1].xyz[1];
      x2[1] = ts->verts[i2-1].xyz[1] - ts->verts[i3-1].xyz[1];
      x1[2] = ts->verts[i1-1].xyz[2] - ts->verts[i3-1].xyz[2];
      x2[2] = ts->verts[i2-1].xyz[2] - ts->verts[i3-1].xyz[2];
      CROSS(n, x1, x2);
      if ((DOT(n,n) > area) && (ts->tris[t2].close == 0)) continue;

      d = EG_dotNorm(ts->verts[i0-1].xyz, ts->verts[i1-1].xyz,
                     ts->verts[i2-1].xyz, ts->verts[i3-1].xyz);
      if (d < 0.0) break;
      if (d < dot) 
        if (EG_dotNorm(mid,                 ts->verts[i1-1].xyz,
                       ts->verts[i2-1].xyz, ts->verts[i3-1].xyz) > d) dot = d;
    }
    if (side != 3) continue;
    /* is the minimum dot bigger than the threshold? */
    if (dot+ANGTOL > ts->dotnrm) continue;

    if (EG_splitTri(t1, uv, mid, ts) == EGADS_SUCCESS) split++;
    if (ts->maxPts > 0)
      if (ts->nverts > ts->maxPts) break;
  }

#ifdef DEBUG
  printf(" EG_tessellate -> split: %d\n", split);
#endif
  return split;
}


__HOST_AND_DEVICE__ static int
EG_addFacetDist(triStruct *ts)
{
  int    j, i0, i1, i2, t1, total, split, side;
  double cmp, xyz[3], uv[2];

  cmp   = MAX(ts->chord*ts->chord, ts->devia2);
  total = ts->ntris;
  for (split = t1 = 0; t1 < total; t1++) {
    if (ts->tris[t1].close != 0) continue;

    i0 = ts->tris[t1].indices[0]-1;
    i1 = ts->tris[t1].indices[1]-1;
    i2 = ts->tris[t1].indices[2]-1;
    uv[0]  = (ts->verts[i0].uv[0]  + ts->verts[i1].uv[0] +
              ts->verts[i2].uv[0])  / 3.0;
    uv[1]  = (ts->verts[i0].uv[1]  + ts->verts[i1].uv[1] +
              ts->verts[i2].uv[1])  / 3.0;
  
    xyz[0] = (ts->verts[i0].xyz[0] + ts->verts[i1].xyz[0] +
              ts->verts[i2].xyz[0]) / 3.0;
    xyz[1] = (ts->verts[i0].xyz[1] + ts->verts[i1].xyz[1] +
              ts->verts[i2].xyz[1]) / 3.0;
    xyz[2] = (ts->verts[i0].xyz[2] + ts->verts[i1].xyz[2] +
              ts->verts[i2].xyz[2]) / 3.0;
    if (DIST2(xyz, ts->tris[t1].mid)  <= cmp)    continue;

    if (EG_maxUVangle(i0, i1, i2, ts) >  DEVANG) continue;
    xyz[0] = ts->tris[t1].mid[0];
    xyz[1] = ts->tris[t1].mid[1];
    xyz[2] = ts->tris[t1].mid[2];
    if (EG_inTri(t1, xyz, 0.10, ts) == 1) continue;
    if (EG_dotNorm(ts->verts[i0].xyz, ts->verts[i1].xyz, xyz,
                   ts->verts[i2].xyz) < 0.0) continue;
    if (EG_dotNorm(ts->verts[i1].xyz, ts->verts[i2].xyz, xyz,
                   ts->verts[i0].xyz) < 0.0) continue;
    if (EG_dotNorm(ts->verts[i2].xyz, ts->verts[i0].xyz, xyz,
                   ts->verts[i1].xyz) < 0.0) continue;
    for (j = side = 0; side < 3; side++) {
      i1 = ts->tris[t1].indices[sides[side][0]]-1;
      i2 = ts->tris[t1].indices[sides[side][1]]-1;
      if (DIST2(ts->verts[i1].xyz, ts->verts[i2].xyz) <= cmp) j++;
    }
    if (j != 0) continue;

    if (EG_splitTri(t1, uv, xyz, ts) == EGADS_SUCCESS) split++;
    if (ts->maxPts > 0)
      if (ts->nverts > ts->maxPts) break;
  }

#ifdef DEBUG
  printf(" EG_tessellate -> tri split: %d\n", split);
#endif
  return split;
}


__HOST_AND_DEVICE__ static int
EG_splitInter(int sideMid, /*@null@*/ double *aux, int cnt, triStruct *ts)
{
  int    status, j, split, i0, i1, i2, i3, t1, side, t2, total;
  double d, dist, *deru, *derv, *norm1, *norm2, uv[2], norm[3], point[18];

  total = ts->ntris;
  deru  = &point[3];
  derv  = &point[6];
  for (t1 = 0; t1 < total; t1++) ts->tris[t1].hit = 0;

  /* break up an edge that touches 2 bounds and is interior */

  for (split = t1 = 0; t1 < total; t1++) {
    if (ts->tris[t1].hit != 0) continue;

    side = -1;
    dist = 0.0;
    for (j = 0; j < 3; j++) {
      t2 = ts->tris[t1].neighbors[j]-1;
      if (t2 < 0) continue;
      if (ts->tris[t2].hit != 0) continue;
      i1 = ts->tris[t1].indices[sides[j][0]];
      i2 = ts->tris[t1].indices[sides[j][1]];
      if (aux == NULL) {
        if (ts->verts[i1-1].type == FACE) continue;
        if (ts->verts[i2-1].type == FACE) continue;
      } else {
        norm1 = &aux[3*i1-3];
        norm2 = &aux[3*i2-3];
        if (DOT(norm1, norm2) >= -0.00001) continue;
      }
      d = DIST2(ts->verts[i1-1].xyz, ts->verts[i2-1].xyz);
      if (d > dist) {
        dist = d;
        side = j;
      }
    }
    if (side == -1) continue;

    t2 = ts->tris[t1].neighbors[side]-1;
    i0 = ts->tris[t1].indices[side];
    i1 = ts->tris[t1].indices[sides[side][0]];
    i2 = ts->tris[t1].indices[sides[side][1]];
    i3 = ts->tris[t2].indices[0] + ts->tris[t2].indices[1] +
         ts->tris[t2].indices[2] - i1 - i2;
    if ((i3 < 1) || (i3 > ts->nverts)) continue;
    uv[0]  = 0.5*(ts->verts[i1-1].uv[0] + ts->verts[i2-1].uv[0]);
    uv[1]  = 0.5*(ts->verts[i1-1].uv[1] + ts->verts[i2-1].uv[1]);
    status = EG_evaluate(ts->face, uv, point);
    if (status != EGADS_SUCCESS) continue;
    if (EG_dotNorm(ts->verts[i0-1].xyz, point,
                   ts->verts[i2-1].xyz, ts->verts[i3-1].xyz) <= 0.1) continue;
    if (EG_dotNorm(ts->verts[i0-1].xyz, ts->verts[i1-1].xyz,
                   point,               ts->verts[i3-1].xyz) <= 0.1) continue;
    if (EG_splitSide(t1, side, t2, sideMid, ts) == EGADS_SUCCESS) {
      EG_floodTriGraph(t1, FLOODEPTH, ts);
      EG_floodTriGraph(t2, FLOODEPTH, ts);
      if (aux != NULL) {
        i1 = ts->nverts - 1;
        aux[3*i1  ] = aux[3*i1+1] = aux[3*i1+2] = 0.0;
        status = EG_evaluate(ts->face, ts->verts[i1].uv, point);
        if (status == EGADS_SUCCESS) {
          dist       = DOT(deru, deru);
          if (dist  != 0.0) {
            dist     = 1.0/sqrt(dist);
            deru[0] *= dist;
            deru[1] *= dist;
            deru[2] *= dist;
          }
          dist       = DOT(derv, derv);
          if (dist  != 0.0) {
            dist     = 1.0/sqrt(dist);
            derv[0] *= dist;
            derv[1] *= dist;
            derv[2] *= dist;
          }
          CROSS(norm, deru, derv);
          aux[3*i1  ] = norm[0];
          aux[3*i1+1] = norm[1];
          aux[3*i1+2] = norm[2];
        }
      }
      split++;
      if ((cnt != 0) && (ts->nverts >= cnt)) return split;
    } else {
      ts->tris[t1].hit = ts->tris[t2].hit = 1;
    }
  }

  return split;
}


#ifdef REMOVEB
__HOST_AND_DEVICE__ static int
EG_removePhaseB(triStruct *ts)
{
  int    i, j, n, t1, t2, vert, tnode, i0, i1, i2, count = 0;
  double d, dist, dots[2], x1[3], x2[3], n2[3], n1[3];

  for (t1 = 0; t1 < ts->ntris; t1++) {
    for (n = j = 0; j < 3; j++)
      if (ts->tris[t1].neighbors[j] < 0) n++;
    if (n != 1) continue;
    for (j = 0; j < 3; j++)
      if (ts->tris[t1].neighbors[j] < 0) n = j;

    /* triangle has 1 side on an Edge */

    vert = ts->tris[t1].indices[n];
    if (EG_closeEdge(t1, ts->verts[vert-1].xyz, ts) != 1) continue;

    /* do we have a candidate for collapse? */

    tnode = -1;
    dist  = DBL_MAX;
    for (t2 = 0; t2 < ts->ntris; t2++) {
      if (t1 == t2) continue;
      for (n = j = 0; j < 3; j++)
        if (ts->tris[t2].indices[j] == vert) n++;
      if (n != 1) continue;
      for (j = 0; j < 3; j++) {
        if (ts->tris[t2].indices[j] == vert) continue;
        i = ts->tris[t2].indices[j];
        if (ts->verts[i-1].type == FACE) {
          d = DIST2(ts->verts[vert-1].xyz, ts->verts[i-1].xyz);
          if (d < dist) {
            dist  = d;
            tnode = i;
          }
        }
      }
    }
    if (tnode == -1) continue;

    /* check dot of neighbors */

    i0 = ts->tris[t1].indices[0]-1;
    i1 = ts->tris[t1].indices[1]-1;
    i2 = ts->tris[t1].indices[2]-1;

    x1[0] = ts->verts[i1].xyz[0] - ts->verts[i0].xyz[0];
    x2[0] = ts->verts[i2].xyz[0] - ts->verts[i0].xyz[0];
    x1[1] = ts->verts[i1].xyz[1] - ts->verts[i0].xyz[1];
    x2[1] = ts->verts[i2].xyz[1] - ts->verts[i0].xyz[1];
    x1[2] = ts->verts[i1].xyz[2] - ts->verts[i0].xyz[2];
    x2[2] = ts->verts[i2].xyz[2] - ts->verts[i0].xyz[2];
    CROSS(n2, x1, x2);
    d = DOT(n2, n2);
    if (d == 0.0) continue;
    d      = 1.0/sqrt(d);
    n2[0] *= d;
    n2[1] *= d;
    n2[2] *= d;

    dots[0] = dots[1] = 1.0;
    for (n = j = 0; j < 3; j++) {
      t2 = ts->tris[t1].neighbors[j];
      if (t2 < 0) continue;
      i0 = ts->tris[t2-1].indices[0]-1;
      i1 = ts->tris[t2-1].indices[1]-1;
      i2 = ts->tris[t2-1].indices[2]-1;

      x1[0] = ts->verts[i1].xyz[0] - ts->verts[i0].xyz[0];
      x2[0] = ts->verts[i2].xyz[0] - ts->verts[i0].xyz[0];
      x1[1] = ts->verts[i1].xyz[1] - ts->verts[i0].xyz[1];
      x2[1] = ts->verts[i2].xyz[1] - ts->verts[i0].xyz[1];
      x1[2] = ts->verts[i1].xyz[2] - ts->verts[i0].xyz[2];
      x2[2] = ts->verts[i2].xyz[2] - ts->verts[i0].xyz[2];
      CROSS(n1, x1, x2);
      d = DOT(n1, n1);
      if (d == 0.0) {
        n++;
        continue;
      }
      d      = 1.0/sqrt(d);
      n1[0] *= d;
      n1[1] *= d;
      n1[2] *= d;
      dots[n] = n1[0]*n2[0] + n1[1]*n2[1] + n1[2]*n2[2];
      n++;
    }
    if (!((dots[0] < 0.866) && (dots[1] < 0.866))) continue;
#ifdef DEBUG
    printf(" removal for vert %d -> %d, tri = %d, dots = %le %le\n",
           vert, tnode, t1, dots[0], dots[1]);
#endif
    EG_collapsEdge(tnode, vert, 0, ts);
    count++;
  }

  return count;
}
#endif


__HOST_AND_DEVICE__ static int
EG_addSideDist(int iter, double maxlen2, int sideMid, triStruct *ts)
{
  int    i, j, i1, i2, t1, t2, split, side = -1;
  double cmp, d, dist, mindist, emndist, xyz[3];

  mindist = MAX(maxlen2, ts->devia2);
  emndist = MAX(mindist, ts->edist2);
  emndist = MAX(emndist, ts->eps2);
  for (split = t1 = 0; t1 < ts->ntris; t1++) {
    ts->tris[t1].hit = 0;
    EG_fillSides(t1, mindist, emndist, ts);
  }

  do {
    dist = 0.0;
    t1   = -1;
    for (i = 0; i < ts->ntris; i++) {
      if (ts->tris[i].hit != 0) continue;
      cmp = ts->tris[i].area;
      for (j = 0; j < 3; j++) {
        d = ts->tris[i].mid[j];
/*
        if (d == 0.0)
          printf(" EGADS warning: Face %d tri %d side %d -- len = 0.0!\n",
                 ts->fIndex, i, j);
 */
        if (d <= cmp) continue;
        if (d > dist) {
          t1   = i;
          side = j;
          dist = d;
        }
      }
    }
    if (t1 == -1) continue;

    if (ts->phase == 3) {
      i1     = ts->tris[t1].indices[sides[side][0]]-1;
      i2     = ts->tris[t1].indices[sides[side][1]]-1;
      xyz[0] = 0.5*(ts->verts[i1].xyz[0] + ts->verts[i2].xyz[0]);
      xyz[1] = 0.5*(ts->verts[i1].xyz[1] + ts->verts[i2].xyz[1]);
      xyz[2] = 0.5*(ts->verts[i1].xyz[2] + ts->verts[i2].xyz[2]);
      if (EG_close2Edge(t1, xyz, ts) == 1) continue;
    }
    t2 = ts->tris[t1].neighbors[side]-1;
    if (EG_splitSide(t1, side, t2, sideMid, ts) == EGADS_SUCCESS) {
      split++;
      if (2*split > iter) break;
      EG_floodTriGraph(t1, FLOODEPTH, ts);
      EG_floodTriGraph(t2, FLOODEPTH, ts);
      EG_fillSides(t1,          mindist, emndist, ts);
      EG_fillSides(t2,          mindist, emndist, ts);
      EG_fillSides(ts->ntris-2, mindist, emndist, ts);
      EG_fillSides(ts->ntris-1, mindist, emndist, ts);
    } else {
      ts->tris[t1].hit = 1;
    }
    if (ts->maxPts > 0)
      if (ts->nverts > ts->maxPts) break;
    if (ts->maxPts < 0)
      if ((ts->nverts-ts->nfrvrts+2) > -ts->maxPts) break;

  } while (t1 != -1);

  return split;
}


/* fills the tessellate structure for the Face */

__HOST_AND_DEVICE__ int
EG_tessellate(int outLevel, triStruct *ts, long tID)
{
  int    n0, n1, n2, n3, flag, stat[3], status, *tmp;
  int    i, j, k, l, stri, i0, i1, last, split, count, lsplit, qi1, qi3;
  int    eg_split, sideMid, badStart = 0;
  double result[18], trange[2], laccum, dist, lang, maxlen2, dot, xvec[3];
  double norm[3], nrm[3], x1[3], x2[3], *deru, *derv, *aux;
  triTri *tt;

  ts->edist2 = 0.0;             /* average edge segment length */
  ts->eps2   = DBL_MAX;         /* smallest edge segment */
  ts->devia2 = 0.0;             /* largest edge deviation */
  eg_split   = sideMid = 0;
  stri       = ts->ntris;

  /* get UV scaling and max edge deviation */

  ts->VoverU = 1.0;
  trange[0]  = trange[1] = 0.0;
  for (i = 0; i < ts->nverts; i++) {
    if (EG_evaluate(ts->face, ts->verts[i].uv, result) != EGADS_SUCCESS)
      continue;
    dist = (ts->verts[i].xyz[0]-result[0])*(ts->verts[i].xyz[0]-result[0]) +
           (ts->verts[i].xyz[1]-result[1])*(ts->verts[i].xyz[1]-result[1]) +
           (ts->verts[i].xyz[2]-result[2])*(ts->verts[i].xyz[2]-result[2]);
    if (dist > ts->devia2) ts->devia2 = dist;
    trange[0] += sqrt(result[3]*result[3] + result[4]*result[4] + 
                      result[5]*result[5]);
    trange[1] += sqrt(result[6]*result[6] + result[7]*result[7] + 
                      result[8]*result[8]);
  }
  if (trange[0] != 0.0) {
    ts->VoverU = trange[1]/trange[0];
#ifdef REPORT
    printf("%lX:          dv/du = %le\n", tID, ts->VoverU);
#endif
  }
  for (i = 0; i < ts->nsegs; i++) {
    i0   = ts->segs[i].indices[0]-1;
    i1   = ts->segs[i].indices[1]-1;
    dist = DIST2(ts->verts[i0].xyz, ts->verts[i1].xyz);
    ts->edist2 += sqrt(dist);
    if (dist == 0.0) continue;
    if (dist < ts->eps2) ts->eps2 = dist;
  }
  for (i = 0; i < ts->ntris; i++) ts->tris[i].close = TOBEFILLED;

  maxlen2     = ts->maxlen*ts->maxlen;
  ts->devia2 /= 256.0;
  ts->eps2   /=   4.0;
  ts->edist2 /= ts->nsegs;
  ts->edist2 *= ts->edist2;
  if (ts->eps2 < ts->devia2) ts->eps2 = ts->devia2;
  if (ts->minlen != 0.0) {
    if (ts->eps2   < ts->minlen*ts->minlen) ts->eps2   = ts->minlen*ts->minlen;
    if (ts->devia2 < ts->minlen*ts->minlen) ts->devia2 = ts->minlen*ts->minlen;
  }
#ifdef DEBUG
  printf("%lX Face %d: tolerances -> eps2 = %le, devia2 = %le, edist2 = %le\n",
         tID, ts->fIndex, ts->eps2, ts->devia2, ts->edist2);
  EG_checkTess(ts);
#endif

  /* do we have any zero area tris that can be removed? */
  ts->phase = -1;
  EG_zeroArea(ts, outLevel, tID);
  
  /* swap negative areas from initial triangulation 
          NOTE: this is not required when fillArea works fine */
  EG_swapTris(EG_areaTest, "areaTest", 0.0, ts);

  if (ts->ntris > ts->mframe) {
    if (ts->frame == NULL) {
      ts->frame = (int *) EG_alloc(3*ts->ntris*sizeof(int));
      if (ts->frame == NULL) return EGADS_MALLOC;
#ifdef DEBUG
      printf(" Alloc Frame: with %d\n", ts->ntris);
#endif
    } else {
      tmp = (int *) EG_reall(ts->frame, 3*ts->ntris*sizeof(int));
      if (tmp == NULL) return EGADS_MALLOC;
      ts->frame = tmp;
#ifdef DEBUG
      printf(" Realloc Frame: now %d (%d)\n", ts->mframe, ts->ntris);
#endif
    }
    ts->mframe = ts->ntris;
  }
  ts->nframe = ts->ntris;
  for (i = 0; i < ts->ntris; i++) {
    ts->frame[3*i  ] = ts->tris[i].indices[0];
    ts->frame[3*i+1] = ts->tris[i].indices[1];
    ts->frame[3*i+2] = ts->tris[i].indices[2];
  }
  ts->nfrvrts = ts->nverts;
  
  /* quads? */
  if (ts->uvs != NULL) {
    int    nvrt, ntrs, *trs;
    double *quv = NULL;

    nvrt = flag = 0;
    if ((ts->ntris   == 2) && (ts->nverts  == 4) && (ts->lens[0] == 1) &&
        (ts->lens[1] == 1) && (ts->lens[2] == 1) && (ts->lens[3] == 1)) {
      /* special single quad case */
      i   = EGADS_MALLOC;
      trs = (int *) EG_alloc(6*sizeof(int));
      if (trs != NULL) {
        trs[0]  = 0;
        trs[1]  = 1;
        trs[2]  = 2;
        trs[3]  = 0;
        trs[4]  = 2;
        trs[5]  = 3;
        ts->tfi = 1;
        ntrs    = 2;
        nvrt    = 4;
        i       = EGADS_SUCCESS;
      }
    } else if (ts->lens[3] == 0) {
      /* cone-like quadding */
      i = EG_quad2tris3(tID, ts->face, ts->qparm, ts->lens, &ts->uvs[2], &nvrt,
                        &quv, &ntrs, &trs, &flag);
    } else {
      /* normal quadding */
      i = EG_quad2tris (tID, ts->face, ts->qparm, ts->lens, &ts->uvs[2], &nvrt,
                        &quv, &ntrs, &trs, &ts->tfi);
    }
    EG_free(ts->uvs);
    if ((i == EGADS_SUCCESS) && (trs != NULL)) {
      /* fill up non-frame verts */
      if (quv != NULL) {
        for (j = ts->nverts; j < nvrt; j++) {
          n0 = EG_evaluate(ts->face, &quv[2*j], result);
          if (n0 < EGADS_SUCCESS) {
            EG_free(quv);
            EG_free(trs);
            return n0;
          }
          n0 = EG_addVert(FACE, 0, 0, result, &quv[2*j], ts);
          if (n0 < EGADS_SUCCESS) {
            EG_free(quv);
            EG_free(trs);
            return n0;
          }
        }
        EG_free(quv);
      }
      /* fill up the triangles */
      if (ntrs > ts->mtris) {
        tt = (triTri *) EG_reall(ts->tris, (ntrs+1)*sizeof(triTri));
        if (tt == NULL) {
          EG_free(trs);
          return EGADS_MALLOC;
        }
        ts->tris  = tt;
        ts->mtris = ntrs+1;
      }
      for (i = 0; i < ntrs; i++) {
        ts->tris[i].indices[0] = trs[3*i  ]+1;
        ts->tris[i].indices[1] = trs[3*i+1]+1;
        ts->tris[i].indices[2] = trs[3*i+2]+1;
      }
      ts->ntris = ntrs;
      EG_free(trs);
      /* check for proper orientations */
      if (EG_checkQuadding(outLevel, flag, ts, tID) == EGADS_SUCCESS) {
        /* flip tri orientation if face is reversed */
        if (ts->orUV == SREVERSE)
          if (ts->tfi == 1) {
            for (i = 0; i < ts->ntris; i+=2) {
              qi1 = ts->tris[i  ].indices[1];
              qi3 = ts->tris[i+1].indices[2];
              ts->tris[i  ].indices[1] = qi3;
              ts->tris[i+1].indices[2] = qi1;
            }
          } else {
            for (i = 0; i < ts->ntris; i++) {
              j                      = ts->tris[i].indices[1];
              ts->tris[i].indices[1] = ts->tris[i].indices[2];
              ts->tris[i].indices[2] = j;
            }
          }
        /* reset segs & neighbors */
        for (i = 0; i < ts->ntris; i++) {
          ts->tris[i].mark         = 0;
          ts->tris[i].neighbors[0] = i+1;
          ts->tris[i].neighbors[1] = i+1;
          ts->tris[i].neighbors[2] = i+1;
        }
        for (i = 0; i < ts->nsegs; i++) ts->segs[i].neighbor = -(i+1);
        /* connect the triangles and make the neighbor info */
        return EG_makeNeighbors(ts, ts->fIndex);
      }
      ts->tfi    = 0;
      ts->nverts = ts->nfrvrts;
      ts->ntris  = ts->nframe;
      for (i = 0; i < ts->ntris; i++) {
        ts->tris[i].indices[0] = ts->frame[3*i  ];
        ts->tris[i].indices[1] = ts->frame[3*i+1];
        ts->tris[i].indices[2] = ts->frame[3*i+2];
      }
    }
  }

  /* mark neighbors as potential swap sites */

  for (i = 0; i < ts->ntris; i++) ts->tris[i].mark = 0;
  for (last = i = 0; i < ts->ntris; i++) {
    j    = ts->tris[i].indices[0]-1;
    k    = ts->tris[i].indices[1]-1;
    l    = ts->tris[i].indices[2]-1;
    dist = ts->orUV*AREA2D(ts->verts[j].uv, ts->verts[k].uv, ts->verts[l].uv);
    if (dist <= 0.0) {
      printf("%lX Face %d: tri %d (of %d) area = %le  planar=%d\n",
             tID, ts->fIndex, i, ts->ntris, dist, ts->planar);
      last++;
    } else {
      for (j = 0; j < 3; j++) {
        k = ts->tris[i].neighbors[j]-1;
        if (k <= i) continue;
        if (EG_checkOr(i, j, k, ts) == 0) continue;
        ts->tris[i].mark |= 1 << j;
        if (ts->tris[k].neighbors[0]-1 == i) ts->tris[k].mark |= 1;
        if (ts->tris[k].neighbors[1]-1 == i) ts->tris[k].mark |= 2;
        if (ts->tris[k].neighbors[2]-1 == i) ts->tris[k].mark |= 4;
      }
    }
  }
#ifdef DEBUG
  EG_checkTess(ts);
#endif
  /* maybe with a single bad triangle amongst many we can recover? */
  if (last  > 1) return EGADS_SUCCESS;
  if (last == 1) {
    if (ts->ntris  < 16) return EGADS_SUCCESS;
    if (ts->planar == 1) return EGADS_SUCCESS;
    badStart = 1;
  }

  /* perform curvature based enhancements for general surfaces */

  if (ts->planar == 0) {

    /* first try sprinkling points based on a uv grid */

    ts->phase = 0;

    /* swap triangles */

    EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
    lang = ts->accum;
#endif
    EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
#ifdef REPORT
    printf("%lX Start:   dotN = %le (%le),  UVang = %le\n",
           tID, ts->accum, ts->dotnrm, lang);
#endif

    /*
     *   add nodes -- try to get geometrically correct (lettered phases)
     */
    
    /* X) split internal tri sides with opposite normals */
    count = 0;
    split = 1;
    flag  = 6*ts->nverts;
    deru  = &result[3];
    derv  = &result[6];
    aux   = (double *) EG_alloc(3*flag*sizeof(double));
    if (aux == NULL) {
      split = 0;
    } else {
      for (i = 0; i < ts->nverts; i++) {
        aux[3*i  ] = aux[3*i+1] = aux[3*i+2] = 0.0;
        status = EG_evaluate(ts->face, ts->verts[i].uv, result);
        if (status != EGADS_SUCCESS) {
          if (status != EGADS_EXTRAPOL)
            printf(" EGADS Internal: Face %d EG_evaluate %lf %lf = %d\n",
                   ts->fIndex, ts->verts[i].uv[0], ts->verts[i].uv[1], status);
          continue;
        }
        dist       = DOT(deru, deru);
        if (dist  != 0.0) {
          dist     = 1.0/sqrt(dist);
          deru[0] *= dist;
          deru[1] *= dist;
          deru[2] *= dist;
        }
        dist       = DOT(derv, derv);
        if (dist  != 0.0) {
          dist     = 1.0/sqrt(dist);
          derv[0] *= dist;
          derv[1] *= dist;
          derv[2] *= dist;
        }
        CROSS(norm, deru, derv);
        aux[3*i  ] = norm[0];
        aux[3*i+1] = norm[1];
        aux[3*i+2] = norm[2];
      }
    }
    while ((split != 0) && (ts->orCnt < MAXORCNT)) {
      split = EG_splitInter(sideMid, aux, flag, ts);
      if (split != 0) {
        EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
        lang = ts->accum;
#endif
        EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
        count += split;
        if (ts->nverts >= flag) split = 0;
      }
    }
    if (aux != NULL) EG_free(aux);
#ifdef DEBUG
    EG_checkTess(ts);
#endif
#ifdef REPORT
    printf("%lX Phase X: dotN = %le,  UVang = %le,  split = %d\n",
           tID, ts->accum, lang, count);
#endif
    
    /* 0) start out Delauney-ish if maxlen is set -- use 2*maxlen */
    if (ts->maxlen > 0.0) {
      count = i = 0;
      do {
        split = EG_addSideDist(i, 4.0*maxlen2, sideMid, ts);
        if (split > 0) {
          EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
          lang = ts->accum;
          EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
          count += split;
          if ((lang > MAXANG) && (ts->accum < 0.0)) split = 0;
        }
        i++;
        if (ts->maxPts > 0)
          if (ts->nverts > ts->maxPts) break;
        if (ts->maxPts < 0)
          if ((ts->nverts-ts->nfrvrts+2) > -ts->maxPts) break;
      } while ((split > 0) && (ts->orCnt < MAXORCNT));
#ifdef REPORT
      printf("%lX Phase 0: dotN = %le,  UVang = %le,  split = %d\n",
             tID, ts->accum, lang, count);
#endif
    }

    /* A) split big tris with inverted neighbors */
    count = 0;
    do {
      split = EG_breakTri(-1, stri, &eg_split, ts);
      if (split > 0) {
        EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
        lang = ts->accum;
#endif
        EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
        count += split;
        if (ts->accum > 0.866) break;
        if (ts->accum <= -1.0) break;
      }
    } while ((split > 0) && (ts->orCnt < MAXORCNT));
#ifdef REPORT
    printf("%lX Phase A: dotN = %le,  UVang = %le,  split = %d\n",
           tID, ts->accum, lang, count);
#endif

    /* B) split internal tri sides that touch 2 edges */
#ifndef __clang_analyzer__
    laccum = MIN(-0.86, ts->accum);
#endif
    count  = 0;
    split  = 1;
    while ((split != 0) && (ts->orCnt < MAXORCNT)) {
      split = EG_splitInter(sideMid, NULL, 0, ts);
      if (split != 0) {
        EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
        lang = ts->accum;
#endif
        EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
        count += split;
      }
      if (count > 3*stri) split = 0;
    }
#ifdef DEBUG
    EG_checkTess(ts);
#endif
#ifdef REPORT
    printf("%lX Phase B: dotN = %le,  UVang = %le,  split = %d\n",
           tID, ts->accum, lang, count);
#endif

#ifdef REMOVEB
    /* remove problem Phase B additions */
    if (count != 0) count = EG_removePhaseB(ts);
    if (count >  0) {
      EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
      lang = ts->accum;
#endif
      EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
#ifdef DEBUG
      EG_checkTess(ts);
#endif
#ifdef REPORT
      printf("%lX          dotN = %le,  UVang = %le,  remove = %d\n",
             tID, ts->accum, lang, count);
#endif
    }
#endif

    /* C) add nodes where midpoints don't match */
    EG_hcreate(CHUNK, ts);
    count = 0;
    do {
      split = EG_breakTri(0, stri, &eg_split, ts);
      if (split > 0) {
        EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
        lang = ts->accum;
#endif
        EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
        count += split;
        if (ts->accum > 0.866) break;
        if (ts->accum <= -1.0) break;
      }
    } while ((split > 0) && (ts->orCnt < MAXORCNT));
#ifdef REPORT
    printf("%lX Phase C: dotN = %le,  UVang = %le,  split = %d\n",
           tID, ts->accum, lang, count);
#endif
    EG_hdestroy(ts);

    /* D) later phases -> add nodes where side length is too long */
    sideMid = 1;
    if ((ts->maxlen > 0.0) && (badStart == 0)) {
      count = i = 0;
      do {
        split = EG_addSideDist(i, maxlen2, sideMid, ts);
        if (split > 0) {
          EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
          lang = ts->accum;
          EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
          count += split;
          if ((lang > MAXANG) && (ts->accum < 0.0)) split = 0;
        }
        i++;
        if (ts->maxPts > 0)
          if (ts->nverts > ts->maxPts) break;
        if (ts->maxPts < 0)
          if ((ts->nverts-ts->nfrvrts+2) > -ts->maxPts) break;
      } while ((split > 0) && (ts->orCnt < MAXORCNT));
#ifdef REPORT
      printf("%lX Phase D: dotN = %le,  UVang = %le,  split = %d\n",
             tID, ts->accum, lang, count);
#endif
    }

    /* 1) add nodes to minimize the facet normals deviation */
    if (ts->accum < ts->dotnrm) {
      ts->phase = 1;
      count = lsplit = 0;
      for (i = 0; i < ts->ntris; i++) EG_fillMid(i, NOTFILLED, ts);
      do {
        split  = EG_addFacetNorm(ts);
        laccum = ts->accum;
        if (split != 0) {
          ts->phase = TOBEFILLED;
          EG_hcreate(CHUNK, ts);
          EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
          lang = ts->accum;
#endif
          EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
          ts->phase = 1;
          for (i = 0; i < ts->ntris; i++)
            if (ts->tris[i].close == TOBEFILLED)
              if (EG_hfind(ts->tris[i].indices[0], ts->tris[i].indices[1],
                           ts->tris[i].indices[2], &j, ts->tris[i].mid, ts)
                  != NOTFILLED){
                ts->tris[i].close = j;
              } else {
                EG_fillMid(i, NOTFILLED, ts);
              }
          EG_hdestroy(ts);
          if ((ts->accum <= laccum) && (split > lsplit)) {
            count++;
          } else {
            count = 0;
          }
#ifndef __clang_analyzer__
          laccum = ts->accum;
#endif
          lsplit = split;
        }
#ifdef REPORT
        printf("%lX Phase 1: dotN = %le,  UVang = %le,  split = %d,  %d\n",
               tID, ts->accum, lang, split, count);
#endif
        if (count > 6) break;
        if (ts->maxPts > 0)
          if (ts->nverts > ts->maxPts) break;
      } while (split != 0);
    }

    /* 2) enhance based on mid facet deviation */
    if (ts->chord > 0.0) {
      last = ts->phase;
      ts->phase = 2;
      if (last == 0)
        for (i = 0; i < ts->ntris; i++) EG_fillMid(i, NOTFILLED, ts);
      count = lsplit = 0;
      do {
        split  = EG_addFacetDist(ts);
        laccum = ts->accum;
        if (split != 0) {
          ts->phase = TOBEFILLED;
          EG_hcreate(CHUNK, ts);
          EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
#ifdef REPORT
          lang = ts->accum;
#endif
          EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
          ts->phase = 2;
          for (i = 0; i < ts->ntris; i++)
            if (ts->tris[i].close == TOBEFILLED)
              if (EG_hfind(ts->tris[i].indices[0], ts->tris[i].indices[1],
                           ts->tris[i].indices[2], &j, ts->tris[i].mid, ts)
                  != NOTFILLED){
                ts->tris[i].close = j;
              } else {
                EG_fillMid(i, NOTFILLED, ts);
              }
          EG_hdestroy(ts);
          if ((ts->accum <= laccum) && (split > lsplit)) count++;
          if ((ts->accum <= laccum) && (split > lsplit)) {
            count++;
          } else {
            count = 0;
          }
#ifndef __clang_analyzer__
          laccum = ts->accum;
#endif
          lsplit = split;
        }
#ifdef REPORT
        printf("%lX Phase 2: dotN = %le,  UVang = %le,  split = %d\n",
               tID, ts->accum, lang, split);
#endif
        if (count > 6) break;
        if (ts->maxPts > 0)
          if (ts->nverts > ts->maxPts) break;
      } while (split != 0);
    }
#ifdef DEBUG
    EG_checkTess(ts);
#endif

    if (outLevel > 1) {
      dot = 1.0;
      for (stat[0] = stat[1] = i = 0; i < ts->ntris; i++)
        for (j = 0; j < 3; j++) {
          if (ts->tris[i].neighbors[j] < i) continue;
          k    = ts->tris[i].neighbors[j]-1;
          n0   = ts->tris[i].indices[j];
          n1   = ts->tris[i].indices[sides[j][0]];
          n2   = ts->tris[i].indices[sides[j][1]];
          n3   = ts->tris[k].indices[0] + ts->tris[k].indices[1] +
                 ts->tris[k].indices[2] - n1 - n2;
          dist = EG_dotNorm(ts->verts[n0-1].xyz, ts->verts[n1-1].xyz,
                            ts->verts[n2-1].xyz, ts->verts[n3-1].xyz);
          dot  = MIN(dot, dist);
          if (dist >= ts->dotnrm) {
            stat[0]++;
          } else {
            stat[1]++;
          }
        }
      printf("%lX    Min angle     = %le (%le), OK = %d, too big = %d\n",
             tID, dot, ts->dotnrm, stat[0], stat[1]);

      if (ts->chord > 0.0) {
        dist = 0.0;
        for (stat[0] = stat[1] = stat[2] = i = 0; i < ts->ntris; i++) {
          n0      = ts->tris[i].indices[0]-1;
          n1      = ts->tris[i].indices[1]-1;
          n2      = ts->tris[i].indices[2]-1;
          xvec[0] = (ts->verts[n0].xyz[0] + ts->verts[n1].xyz[0] +
                     ts->verts[n2].xyz[0]) / 3.0;
          xvec[1] = (ts->verts[n0].xyz[1] + ts->verts[n1].xyz[1] +
                     ts->verts[n2].xyz[1]) / 3.0;
          xvec[2] = (ts->verts[n0].xyz[2] + ts->verts[n1].xyz[2] +
                     ts->verts[n2].xyz[2]) / 3.0;
          dot     = DIST2(xvec, ts->tris[i].mid);
          dist    = MAX(dist, dot);
          if (dot <= ts->chord*ts->chord) {
            stat[1]++;
          } else {
            stat[2]++;
            if (ts->tris[i].close != 0) stat[0]++;
          }
        }
        printf("%lX    Max deviation = %le (%le), OK = %d, 2Big = %d (2Close=%d)\n",
               tID, sqrt(dist), ts->chord, stat[1], stat[2], stat[0]);
      }
   }

    /* final clean-up */

    ts->phase = 3;
    EG_swapTris(EG_angUVTest, "angleUV",  0.0, ts);
    lang = ts->accum;
    EG_swapTris(EG_diagTest, "diagonals", 1.0, ts);
#ifdef REPORT
    printf("%lX Phase 3: dotN = %le,  UVang = %le\n", tID, ts->accum, lang);
#endif

  } else {

    /* planar surfaces -- check for inversions */
    
    norm[0] = norm[1] = norm[2] = 0.0;
    for (flag = i = 0; i < ts->ntris; i++) {
      n0    = ts->tris[i].indices[0];
      n1    = ts->tris[i].indices[1];
      n2    = ts->tris[i].indices[2];
      x1[0] = ts->verts[n1-1].xyz[0] - ts->verts[n0-1].xyz[0];
      x2[0] = ts->verts[n2-1].xyz[0] - ts->verts[n0-1].xyz[0];
      x1[1] = ts->verts[n1-1].xyz[1] - ts->verts[n0-1].xyz[1];
      x2[1] = ts->verts[n2-1].xyz[1] - ts->verts[n0-1].xyz[1];
      x1[2] = ts->verts[n1-1].xyz[2] - ts->verts[n0-1].xyz[2];
      x2[2] = ts->verts[n2-1].xyz[2] - ts->verts[n0-1].xyz[2];
      CROSS(nrm, x1, x2);
      dist = DOT(nrm, nrm);
      if (dist != 0.0) {
        dist   = 1.0/sqrt(dist);
        nrm[0] *= dist;
        nrm[1] *= dist;
        nrm[2] *= dist;
      }
      if (i != 0) {
        if (DOT(norm,nrm) < 0.0) flag++;
      } else {
        norm[0] = nrm[0];
        norm[1] = nrm[1];
        norm[2] = nrm[2];
      }
    }
#ifdef DEBUG
    if (flag != 0) printf(" *** Face %d: Planar # inverted = %d (%d) ***\n",
                          ts->fIndex, flag, ts->ntris);
#endif

    ts->phase = -3;
    EG_swapTris(EG_angXYZTest, "angleXYZ", 0.0, ts);
    lang = ts->accum;

    /* break up long edges */

    if (ts->maxlen > 0.0) {
      count = i = k = l = 0;
      do {
        split = EG_addSideDist(i, maxlen2, sideMid, ts);
        if (split > 0) {
          EG_swapTris(EG_angXYZTest, "angleXYZ", 0.0, ts);
          lang = ts->accum;
          count += split;
        }
        i++;
        if (ts->maxPts > 0)
          if (ts->nverts > ts->maxPts) break;
        if (ts->maxPts < 0)
          if ((ts->nverts-ts->nfrvrts+2) > -ts->maxPts) break;
        /* resolves strange problem with fillArea not providing a good start */
        if ((i != 1) && (flag != 0)) {
          if ((k == 0) && (split != 1)) {
            k = 1;
          } else if ((k == 1) && (split == 1) && (l == 1)) {
            printf(" *** Face %d: Planar early breakout -- count = %d ***\n",
                   ts->fIndex, count);
            break;
          }
        }
        l = split;
      } while (split > 0);
#ifdef REPORT
      printf("%lX  XYZang = %le,   split = %d\n", tID, ts->accum, count);
#endif
    }

  }
#ifdef DEBUG
  EG_checkTess(ts);
#endif

  /* report stuff and finish up */

  if ((outLevel > 1) && (ts->maxlen > 0.0)) {
    dist = 0.0;
    for (k = l = i = 0; i < ts->ntris; i++)
      for (j = 0; j < 3; j++) {
        if (ts->tris[i].neighbors[j] < i) continue;
        n1   = ts->tris[i].indices[sides[j][0]]-1;
        n2   = ts->tris[i].indices[sides[j][1]]-1;
        dot  = DIST2(ts->verts[n1].xyz, ts->verts[n2].xyz);
        dist = MAX(dist, dot);
        if (dot <= ts->maxlen*ts->maxlen) {
          k++;
        } else {
          l++;
        }
        
      }
    printf("%lX    Max Side Len  = %le (%le), OK = %d, too big = %d\n",
           tID, sqrt(dist), ts->maxlen, k, l);
  }

  if (outLevel > 1) {
    printf("%lX Face %d: npts = %d,  ntris = %d\n", 
           tID, ts->fIndex, ts->nverts, ts->ntris);
    if (ts->planar == 0) {
      if ((ts->accum < -0.1) || (lang > MAXANG)) 
        printf("%lX            **Tessellation problem**  %le  %le\n",
               tID, lang, ts->accum);
    } else {
      if (lang > MAXANG) 
        printf("%lX            **Tessellation problem**  %le\n", tID, lang);
    }
  } else {
#ifdef REPORT
    printf("%lX Face %d: npts = %d,  ntris = %d\n", 
           tID, ts->fIndex, ts->nverts, ts->ntris);
    if (ts->planar == 0) {
      if ((ts->accum < -0.1) || (lang > MAXANG)) 
        printf("%lX            **Tessellation problem**  %le  %le\n",
               tID, lang, ts->accum);
    } else {
      if (lang > MAXANG) 
        printf("%lX            **Tessellation problem**  %le\n", tID, lang);
    }
#endif
  }

  /* perform the last set of swaps based on physical coordinates */
  if ((ts->planar == 0) && (ts->ntris > 2*stri))
    EG_swapTris(EG_angXYZTest, "angleXYZ", 0.0, ts);

  return EGADS_SUCCESS;
}


__HOST_AND_DEVICE__ static int
EG_sign(double s)
{
  if (s > 0.0) return  1;
  if (s < 0.0) return -1;
  return  0;
}


__HOST_AND_DEVICE__ int
EG_inTriExact(double *t1, double *t2, double *t3, double *p, double *w)
{
  int    d1, d2, d3;
  double sum;
  
  w[0] = EG_orienTri(t2, t3, p);
  w[1] = EG_orienTri(t1, p,  t3);
  w[2] = EG_orienTri(t1, t2, p);
  d1   = EG_sign(w[0]);
  d2   = EG_sign(w[1]);
  d3   = EG_sign(w[2]);
  sum  = w[0] + w[1] + w[2];
  if (sum != 0.0) {
    w[0] /= sum;
    w[1] /= sum;
    w[2] /= sum;
  }
  
  if (d1*d2*d3 == 0)
    if (d1 == 0) {
      if ((d2 == 0) && (d3 == 0)) return EGADS_DEGEN;
      if (d2 == d3) return EGADS_SUCCESS;
      if (d2 ==  0) return EGADS_SUCCESS;
      if (d3 ==  0) return EGADS_SUCCESS;
    } else if (d2 == 0) {
      if (d1 == d3) return EGADS_SUCCESS;
      if (d3 ==  0) return EGADS_SUCCESS;
    } else {
      if (d1 == d2) return EGADS_SUCCESS;
    }
  
  /* all resultant tris have the same sign -> intersection */
  if ((d1 == d2) && (d2 == d3)) return EGADS_SUCCESS;
  
  /* otherwise then no intersection */
  return EGADS_OUTSIDE;
}


int
EG_baryFrame(egTess2D *tess2d)
{
  int    i, j, i0, i1, i2, cls;
  double neg, w[3];

  tess2d->bary = (egBary *) EG_alloc(tess2d->npts*sizeof(egBary));
  if (tess2d->bary == NULL) return EGADS_MALLOC;
  
  for (i = 0; i < tess2d->npts; i++) {
    tess2d->bary[i].tri  = 0;
    tess2d->bary[i].w[0] = tess2d->bary[i].w[1] = neg = 0.0;
    for (cls = j = 0; j < tess2d->nframe; j++) {
      i0 = tess2d->frame[3*j  ] - 1;
      i1 = tess2d->frame[3*j+1] - 1;
      i2 = tess2d->frame[3*j+2] - 1;
      if (EG_inTriExact(&tess2d->uv[2*i0], &tess2d->uv[2*i1], &tess2d->uv[2*i2],
                        &tess2d->uv[2*i],  w) == EGADS_SUCCESS) {
        tess2d->bary[i].tri  = j+1;
        tess2d->bary[i].w[0] = w[0];
        tess2d->bary[i].w[1] = w[1];
        break;
      }
      if (w[1] < w[0]) w[0] = w[1];
      if (w[2] < w[0]) w[0] = w[2];
      if (cls == 0) {
        cls = j+1;
        neg = w[0];
      } else {
        if (w[0] > neg) {
          cls = j+1;
          neg = w[0];
        }
      }
    }
    if ((cls == 0) && (tess2d->bary[i].tri == 0)) {
      printf(" EGADS Error: No frame triangle found for %lf %lf  %d!\n",
             tess2d->uv[2*i], tess2d->uv[2*i+1], i+1);
      EG_free(tess2d->bary);
      tess2d->bary = NULL;
      return EGADS_NOTFOUND;
    }
    if (tess2d->bary[i].tri == 0) {
      i0 = tess2d->frame[3*cls-3] - 1;
      i1 = tess2d->frame[3*cls-2] - 1;
      i2 = tess2d->frame[3*cls-1] - 1;
      EG_inTriExact(&tess2d->uv[2*i0], &tess2d->uv[2*i1], &tess2d->uv[2*i2],
                    &tess2d->uv[2*i],  w);
      tess2d->bary[i].tri  = cls;
      tess2d->bary[i].w[0] = w[0];
      tess2d->bary[i].w[1] = w[1];
      printf(" EGADS Warning: Extrapolation for %lf %lf  %d (EG_baryFrame)!\n",
             tess2d->uv[2*i], tess2d->uv[2*i+1], i+1);
      printf("                %3d %3d,   %lf %lf   %le\n",
             tess2d->ptype[i0], tess2d->pindex[i0], tess2d->uv[2*i0],
             tess2d->uv[2*i0+1], w[0]);
      printf("                %3d %3d,   %lf %lf   %le\n",
             tess2d->ptype[i1], tess2d->pindex[i1], tess2d->uv[2*i1],
             tess2d->uv[2*i1+1], w[1]);
      printf("                %3d %3d,   %lf %lf   %le\n",
             tess2d->ptype[i2], tess2d->pindex[i2], tess2d->uv[2*i2],
             tess2d->uv[2*i2+1], w[2]);
    }
  }
  
  return EGADS_SUCCESS;
}


__HOST_AND_DEVICE__ int
EG_baryTess(egTess2D tess2d, const double *uv, double *w)
{
  int    j, i0, i1, i2, cls;
  double neg, *tuv, uvs[2];
  
  tuv    = tess2d.uv;
  uvs[0] = uv[0];
  uvs[1] = uv[1];
  cls    = 0;
  neg    = w[0] = w[1] = w[2] = 0.0;
  for (j = 0; j < tess2d.ntris; j++) {
    i0 = tess2d.tris[3*j  ] - 1;
    i1 = tess2d.tris[3*j+1] - 1;
    i2 = tess2d.tris[3*j+2] - 1;
    if (EG_inTriExact(&tuv[2*i0], &tuv[2*i1], &tuv[2*i2], uvs, w) ==
        EGADS_SUCCESS) return j+1;
    if (w[1] < w[0]) w[0] = w[1];
    if (w[2] < w[0]) w[0] = w[2];
    if (cls == 0) {
      cls = j+1;
      neg = w[0];
    } else {
      if (w[0] > neg) {
        cls = j+1;
        neg = w[0];
      }
    }
  }
  
  if (cls != 0) {
    i0 = tess2d.tris[3*cls-3] - 1;
    i1 = tess2d.tris[3*cls-2] - 1;
    i2 = tess2d.tris[3*cls-1] - 1;
    EG_inTriExact(&tuv[2*i0], &tuv[2*i1], &tuv[2*i2], uvs, w);
  }
  return cls;
}


#ifndef LITE
int
EG_fitTriangles(egObject *context, int npts, double *xyzs, int ntris,
                const int *tris, /*@null@*/ const int *tric, double tol,
                egObject **bspline)
{
  int     i, j, n, outLevel, stat, type, nu, nv, per, sizes[2], *ppnts, *vtab;
  double  rmserr, maxerr, dotmin, *grid = NULL;
  prmXYZ  *pxyz;
  prmTri  *ptris;
  prmUV   *uv;
  connect *etab;
  
  *bspline = NULL;
  if (context == NULL)               return EGADS_NULLOBJ;
  if (context->magicnumber != MAGIC) return EGADS_NOTOBJ;
  if (context->oclass != CONTXT)     return EGADS_NOTCNTX;
  if (EG_sameThread(context))        return EGADS_CNTXTHRD;
  if ((ntris <= 0) || (npts <= 0))   return EGADS_EMPTY;
  outLevel = EG_outLevel(context);
  pxyz     = (prmXYZ *) xyzs;
  
  ptris = (prmTri *) EG_alloc(ntris*sizeof(prmTri));
  if (ptris == NULL) return EGADS_MALLOC;
  
  /* Are we valid? */
  for (i = 0; i < ntris; i++) {
    ptris[i].own = 1;
    if ((tris[3*i  ] < 1) || (tris[3*i  ] > npts) ||
        (tris[3*i+1] < 1) || (tris[3*i+1] > npts) ||
        (tris[3*i+2] < 1) || (tris[3*i+2] > npts)) {
      if (outLevel > 0) {
        printf(" EGADS Warning: %d bad tris [1-%d] (EG_fitTriangles)!\n",
               i+1, npts);
        printf("                tris = %d %d %d\n",
               tris[3*i  ], tris[3*i+1], tris[3*i+2]);
      }
      EG_free(ptris);
      return EGADS_INDEXERR;
    }
    ptris[i].indices[0] = tris[3*i  ];
    ptris[i].indices[1] = tris[3*i+1];
    ptris[i].indices[2] = tris[3*i+2];
    ptris[i].neigh[0]   = ptris[i].neigh[1] = ptris[i].neigh[2] = i+1;
    if (tric == NULL) continue;
    if ((tric[3*i  ] > ntris) || (tric[3*i+1] > ntris) ||
        (tric[3*i+2] > ntris)) {
      if (outLevel > 0) {
        printf(" EGADS Warning: %d bad tric [1-%d] (EG_fitTriangles)!\n",
               i+1, ntris);
        printf("                tric = %d %d %d\n",
               tric[3*i  ], tric[3*i+1], tric[3*i+2]);
      }
      EG_free(ptris);
      return EGADS_INDEXERR;
    }
    ptris[i].neigh[0] = tric[3*i  ];
    ptris[i].neigh[1] = tric[3*i+1];
    ptris[i].neigh[2] = tric[3*i+2];
  }
  
  /* get connectivity if not supplied */
  if (tric == NULL) {
    vtab = (int *) EG_alloc(npts*sizeof(int));
    if (vtab == NULL) {
      EG_free(ptris);
      return EGADS_MALLOC;
    }
    etab = (connect *) EG_alloc(ntris*3*sizeof(connect));
    if (etab == NULL) {
      EG_free(vtab);
      EG_free(ptris);
      return EGADS_MALLOC;
    }
    n = NOTFILLED;
    for (j = 0; j < npts; j++) vtab[j] = NOTFILLED;
    for (i = 0; i < ntris;  i++) {
      EG_makeConnect( ptris[i].indices[1], ptris[i].indices[2],
                     &ptris[i].neigh[0], &n, vtab, etab, 0);
      EG_makeConnect( ptris[i].indices[0], ptris[i].indices[2],
                     &ptris[i].neigh[1], &n, vtab, etab, 0);
      EG_makeConnect( ptris[i].indices[0], ptris[i].indices[1],
                     &ptris[i].neigh[2], &n, vtab, etab, 0);
    }
    /* find any unconnected triangle sides */
    for (j = 0; j <= n; j++) {
      if (etab[j].tri == NULL) continue;
/*    printf(" EGADS Info: Unconnected Side %d %d = %d\n",
             etab[j].node1+1, etab[j].node2+1, *etab[j].tri); */
      *etab[j].tri = 0;
    }
    EG_free(etab);
    EG_free(vtab);
  }
  
  /* get the memory needed */
  uv = (prmUV *) EG_alloc(npts*sizeof(prmUV));
  if (uv == NULL) {
    EG_free(ptris);
    return EGADS_MALLOC;
  }
  
  /* get the parameterization & fit the surface */
  n    = 1;
  stat = EGADS_SUCCESS;
  type = prm_CreateUV(0, ntris, ptris, NULL, npts, NULL, NULL, uv, pxyz,
                      &per, &ppnts);
  if (outLevel > 1) {
    printf(" EG_fitTriangles: prm_CreateUV = %d  per = %d\n", type, per);
    if (type == PRM_NOGLOBALUV) {
      printf("                  npts = %d  ntris = %d\n", npts, ntris);
/*    for (i = 0; i < npts; i++)
        printf("         %d:  %lf %lf %lf\n",
               i, xyzs[3*i  ], xyzs[3*i+1], xyzs[3*i+2]);  */
    }
  }
  if (type > 0) {
    n    = 2;
    stat = prm_SmoothUV(3, per, ppnts, ntris, ptris, npts, 3, uv, xyzs);
    if (outLevel > 1)
      printf(" EG_fitTriangles: prm_SmoothUV = %d\n", stat);
    if (stat == EGADS_MALLOC)     stat = EGADS_SUCCESS;
    if (stat == PRM_NOTCONVERGED) stat = EGADS_SUCCESS;
    while ((stat != EGADS_SUCCESS) && (type < 7)) {
      if (ppnts != NULL) EG_free(ppnts);
      type++;
      if (type < 6) type = 6;
      n    = 1;
      stat = prm_CreateUV(type, ntris, ptris, NULL, npts, NULL, NULL, uv, pxyz,
                          &per, &ppnts);
      if (outLevel > 1)
        printf(" EG_fitTriangles: prm_CreateUV = %d  per = %d\n", stat, per);
      if (stat < EGADS_SUCCESS) continue;
      n    = 2;
      stat = prm_SmoothUV(3, per, ppnts, ntris, ptris, npts, 3, uv, xyzs);
      if (outLevel > 1)
        printf(" EG_fitTriangles: prm_SmoothUV = %d\n", stat);
      if (stat == EGADS_MALLOC)     stat = EGADS_SUCCESS;
      if (stat == PRM_NOTCONVERGED) stat = EGADS_SUCCESS;
    }
    if (stat == EGADS_SUCCESS) {
      n    = 3;
      stat = prm_NormalizeUV(0.01, per, npts, uv);
      if (outLevel > 1)
        printf(" EG_fitTriangles: prm_NormalizeUV = %d\n", stat);
      if (stat == EGADS_SUCCESS) {
        n    = 4;
        nu   = 2*npts;
        nv   = 0;
        stat = prm_BestGrid(npts, 3, uv, xyzs, ntris, ptris, tol, per, ppnts,
                            &nu, &nv, &grid, &rmserr, &maxerr, &dotmin);
        if (stat == PRM_TOLERANCEUNMET) {
          printf(" EG_fitTriangles: Tolerance not met: %lf (%lf)!\n",
                 maxerr, tol);
          stat = EGADS_SUCCESS;
        }
        if (outLevel > 1)
          printf(" EG_fitTriangles: prm_BestGrid = %d  %d %d  %lf %lf (%lf)\n",
                 stat, nu, nv, rmserr, maxerr, tol);
      }
    }
  }
  EG_free(uv);
  EG_free(ptris);
  if (ppnts != NULL) EG_free(ppnts);
  if ((stat != EGADS_SUCCESS) || (grid == NULL)) {
    if (stat == EGADS_SUCCESS) stat = EGADS_CONSTERR;
    if (outLevel > 0)
      printf(" EGADS Warning: Create/Smooth/Normalize/BestGrid %d = %d!\n",
             n, stat);
    return stat;
  }
  
  /* make the surface */
  sizes[0] = sizes[1] = 0;
#ifndef __clang_analyzer__
  sizes[0] = nu;
  sizes[1] = nv;
#endif
  stat = EG_approximate(context, 0, tol, sizes, grid, bspline);
  EG_free(grid);
  
  return stat;
}
#endif
