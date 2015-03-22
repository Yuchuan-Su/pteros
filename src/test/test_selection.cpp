/*
 *
 *                This source code is part of
 *                    ******************
 *                    ***   Pteros   ***
 *                    ******************
 *                 molecular modeling library
 *
 * Copyright (c) 2009-2013, Semen Yesylevskyy
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of Artistic License:
 *
 * Please note, that Artistic License is slightly more restrictive
 * then GPL license in terms of distributing the modified versions
 * of this software (they should be approved first).
 * Read http://www.opensource.org/licenses/artistic-license-2.0.php
 * for details. Such license fits scientific software better then
 * GPL because it prevents the distribution of bugged derivatives.
 *
*/

#include <string>
#include "pteros/analysis/options.h"
#include <Eigen/Core>
#include "pteros/core/pteros_error.h"
#include "pteros/core/selection.h"
#include <chrono>
#include <boost/variant.hpp>
#include <functional>

using namespace std;
using namespace pteros;
using namespace Eigen;

int main(int argc, char** argv)
{

    try{

        System s("/media/semen/data/semen/trajectories/asymmetric_hexagonal/with_c60/last.gro");
        //System s("/media/semen/data/semen/trajectories/2lao/average.pdb");

        /*
        auto t_start = std::chrono::high_resolution_clock::now();
        Grid_searcher(2.0,sel1,sel2,bon,true,true);
        auto t_end = std::chrono::high_resolution_clock::now();

        cout << bon.size() << " elapsed: "
             << std::chrono::duration<double>(t_end-t_start).count() << endl;
        */
        //-----------


        auto t_start = std::chrono::high_resolution_clock::now();
        Selection w;
        for(int i=0;i<1;++i)
            w.modify(s,"within 0.25 nopbc of name CA CB");

        auto t_end = std::chrono::high_resolution_clock::now();

        cout << w.size() << " elapsed: "
             << std::chrono::duration<double>(t_end-t_start).count()/100.0 << endl;


    } catch(const Pteros_error& e){ e.print(); }

}

