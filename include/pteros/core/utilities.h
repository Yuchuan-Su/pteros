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


#ifndef UTILITIES_H
#define UTILITIES_H

#include "pteros/core/typedefs.h"
#include <vector>

namespace pteros {

    float angle_between_vectors(Vector3f_const_ref vec1, Vector3f_const_ref vec2);

    Eigen::Vector3f project_vector(Vector3f_const_ref vec1, Vector3f_const_ref vec2);

    float rad_to_deg(float ang);
    float deg_to_rad(float ang);

    constexpr long double operator"" _deg ( long double ang ) {
        return ang*3.141592/180.0;
    }

    constexpr long double operator"" _rad ( long double ang ) {
        return ang*180.0/3.141592;
    }


    std::string get_element_name(int elnum);

    float get_vdw_radius(int elnum, const std::string& name);


    /// Returns rotation matrix given pivot, axis and angle in radians
    Eigen::Affine3f rotation_transform(Vector3f_const_ref pivot, Vector3f_const_ref axis, float angle);

    /// Simple histogram class
    class Histogram {
    public:
        Histogram(){}
        Histogram(float minval, float maxval, int n);
        void create(float minval, float maxval, int n);
        void add(float v);
        void add(const std::vector<float> &v);
        void normalize();
        float value(int i) const;
        float position(int i) const;
        const Eigen::VectorXd& values() const;
        const Eigen::VectorXd& positions() const;
        int num_bins() const;
        void save_to_file(const std::string& fname);
    private:
        int nbins;
        float minv,maxv,d;
        Eigen::VectorXd val;
        Eigen::VectorXd pos;
        bool normalized;
    };





}

#endif

