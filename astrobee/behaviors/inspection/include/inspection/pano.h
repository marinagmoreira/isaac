/* Copyright (c) 2021, United States Government, as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 *
 * All rights reserved.
 *
 * The "ISAAC - Integrated System for Autonomous and Adaptive Caretaking
 * platform" software is licensed under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with the
 * License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#ifndef INSPECTION_PANO_H_
#define INSPECTION_PANO_H_

#include <cstdint>
#include <vector>

namespace inspection {

class PanoAttitude {
 public:
  double pan, tilt;  // radians
  int16_t iy;  // row index, 0=bottom row
  int16_t ix;  // column index, 0=left column

  inline PanoAttitude(double _pan, double _tilt, int16_t _iy, int16_t _ix) :
    pan(_pan),
    tilt(_tilt),
    iy(_iy),
    ix(_ix)
  {}
};

void pano_orientations(std::vector<PanoAttitude>* orientations_out,
                       double pan_radius, double tilt_radius,
                       double h_fov, double v_fov,
                       double overlap, double attitude_tolerance);

}  // namespace inspection

#endif  // INSPECTION_PANO_H_
