/*
Copyright (c) 2023 Electrosmith, Corp, Barry Vercoe, john ffitch, Gabriel Maldonado

Use of this copy is governed by the GNU General Public License,
version 3 or (at your option) any later version. See the LICENSE
file at the repository root or https://www.gnu.org/licenses/gpl-3.0.html
*/

#pragma once
#ifndef DSY_BALANCE_H
#define DSY_BALANCE_H

#include <stdint.h>
#ifdef __cplusplus

namespace daisysp
{
/** Balances two sound sources. Sig is boosted to the level of comp. */
class Balance
{
  public:
    Balance() {}
    ~Balance() {}
    /** Initializes the balance module.
        \param sample_rate - The sample rate of the audio engine being run. 
    */
    void Init(float sample_rate);


    /** adjust sig level to level of comp
    */
    float Process(float sig, float comp);


    /** adjusts the rate at which level compensation happens
        \param cutoff : Sets half power point of special internal cutoff filter.

        defaults to 10
    */
    inline void SetCutoff(float cutoff) { ihp_ = cutoff; }

  private:
    float sample_rate_, ihp_, c2_, c1_, prvq_, prvr_, prva_;
};
} // namespace daisysp
#endif
#endif
