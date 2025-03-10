// Copyright (c) 2001,2004,2008-2009, 2017  INRIA Sophia-Antipolis (France).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org).
// You can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// Licensees holding a valid commercial license may use this file in
// accordance with the commercial license agreement provided with the software.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// $URL: https://github.com/CGAL/cgal/blob/releases/CGAL-4.14.1/Periodic_3_triangulation_3/include/CGAL/internal/Periodic_3_Delaunay_triangulation_statically_filtered_traits_3.h $
// $Id: Periodic_3_Delaunay_triangulation_statically_filtered_traits_3.h adfdc70 %aI Mael Rouxel-Labbé
// SPDX-License-Identifier: GPL-3.0+
//
//
// Author(s)     : Sylvain Pion <Sylvain.Pion@sophia.inria.fr>
//                 Manuel Caroli <Manuel.Caroli@sophia.inria.fr>

#ifndef CGAL_PERIODIC_3_DELAUNAY_TRIANGULATION_STATICALLY_FILTERED_TRAITS_3_H
#define CGAL_PERIODIC_3_DELAUNAY_TRIANGULATION_STATICALLY_FILTERED_TRAITS_3_H

#include <CGAL/license/Periodic_3_triangulation_3.h>

#include <CGAL/internal/Static_filters/Periodic_3_side_of_oriented_sphere_3.h>
#include <CGAL/internal/Periodic_3_Delaunay_triangulation_filtered_traits_3.h>

namespace CGAL {

template<typename K_,
         typename Off_ = typename CGAL::Periodic_3_offset_3>
class Periodic_3_Delaunay_triangulation_statically_filtered_traits_3
  : public Periodic_3_Delaunay_triangulation_filtered_traits_base_3<K_, Off_>
{
  typedef Periodic_3_Delaunay_triangulation_statically_filtered_traits_3<K_, Off_> Self;
  typedef Periodic_3_Delaunay_triangulation_filtered_traits_base_3<K_, Off_>       Base;

public:
  typedef K_                                                                       Kernel;
  typedef typename Kernel::Iso_cuboid_3                                            Iso_cuboid_3;

  Periodic_3_Delaunay_triangulation_statically_filtered_traits_3(const Iso_cuboid_3& domain,
                                                                 const Kernel& k)
    : Base(domain, k)
  { }

  typedef internal::Static_filters_predicates::Periodic_3_side_of_oriented_sphere_3<
            Self, typename Base::Side_of_oriented_sphere_3> Side_of_oriented_sphere_3;

  Side_of_oriented_sphere_3 side_of_oriented_sphere_3_object() const {
    return Side_of_oriented_sphere_3(&this->_domain,
                                     this->Base::side_of_oriented_sphere_3_object());
  }
};

} //namespace CGAL

#endif // CGAL_PERIODIC_3_DELAUNAY_TRIANGULATION_STATICALLY_FILTERED_TRAITS_3_H
