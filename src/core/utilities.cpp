/*
 * This file is a part of
 *
 * ============================================
 * ###   Pteros molecular modeling library  ###
 * ============================================
 *
 * (C) 2009-2018, Semen Yesylevskyy
 *
 * All works, which use Pteros, should cite the following papers:
 *  
 *  1.  Semen O. Yesylevskyy, "Pteros 2.0: Evolution of the fast parallel
 *      molecular analysis library for C++ and python",
 *      Journal of Computational Chemistry, 2015, 36(19), 1480–1488.
 *      doi: 10.1002/jcc.23943.
 *
 *  2.  Semen O. Yesylevskyy, "Pteros: Fast and easy to use open-source C++
 *      library for molecular analysis",
 *      Journal of Computational Chemistry, 2012, 33(19), 1632–1636.
 *      doi: 10.1002/jcc.22989.
 *
 * This is free software distributed under Artistic License:
 * http://www.opensource.org/licenses/artistic-license-2.0.php
 *
*/


#include "pteros/core/utilities.h"
#include "pteros/core/pteros_error.h"
#include <fstream>

using namespace std;
using namespace pteros;
using namespace Eigen;

float pteros::angle_between_vectors(Vector3f_const_ref vec1, Vector3f_const_ref vec2)
{
    float ang = vec1.dot(vec2)/vec1.norm()/vec2.norm();
    if(ang>1.0) ang = 1.0;
    if(ang<-1.0) ang = -1.0;
    return acos(ang);
}


Vector3f pteros::project_vector(Vector3f_const_ref vec1, Vector3f_const_ref vec2)
{
    return (vec1.dot(vec2)/vec2.dot(vec2))*vec2;
}


float pteros::deg_to_rad(float ang)
{
    return ang*3.141592/180.0;
}

float pteros::rad_to_deg(float ang)
{
    return ang*180.0/3.141592;
}

Histogram::Histogram(float minval, float maxval, int n): minv(minval), maxv(maxval), nbins(n), normalized(false)
{
    create(minval,maxval,n);
}

void Histogram::create(float minval, float maxval, int n)
{
    val.resize(nbins);
    val.fill(0.0);
    pos.resize(nbins);
    d = (maxv-minv)/float(nbins);
    for(int i=0;i<nbins;++i) pos(i) = minv+0.5*d+d*i;
}

void Histogram::add(float v)
{
    if(normalized) throw Pteros_error("Can't add value to normalized histogram!");
    int b = floor((v-minv)/d);
    if(b>=0 && b<nbins) val(b) += 1.0;
}

void Histogram::add(const std::vector<float>& v)
{
    for(auto& val: v) add(val);
}

void Histogram::normalize()
{
    val /= val.sum()*(pos(1)-pos(0));
    normalized = true;
}

float Histogram::value(int i) const
{
    return val[i];
}

float Histogram::position(int i) const
{
    return pos[i];
}

const VectorXd &Histogram::values() const
{
    return val;
}

const VectorXd &Histogram::positions() const
{
    return pos;
}

int Histogram::num_bins() const
{
    return nbins;
}

void Histogram::save_to_file(const string &fname)
{
    ofstream f(fname);
    for(int i=0;i<nbins;++i) f << pos(i) << " " << val(i) << endl;
    f.close();
}

//-----------------------------------------

Affine3f pteros::rotation_transform(Vector3f_const_ref pivot, Vector3f_const_ref axis, float angle)
{
    Affine3f m;
    m = AngleAxisf(angle,axis.normalized());
    m.translation().fill(0.0);
    return Translation3f(pivot)*m*Translation3f(-pivot);
}
