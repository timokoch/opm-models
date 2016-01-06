// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright (C) 2011-2013 by Andreas Lauser

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
 * \file
 *
 * \copydoc Ewoms::NcpPrimaryVariables
 */
#ifndef EWOMS_NCP_PRIMARY_VARIABLES_HH
#define EWOMS_NCP_PRIMARY_VARIABLES_HH

#include "ncpproperties.hh"

#include <ewoms/disc/common/fvbaseprimaryvariables.hh>
#include <ewoms/models/common/energymodule.hh>

#include <opm/material/constraintsolvers/NcpFlash.hpp>
#include <opm/material/fluidstates/CompositionalFluidState.hpp>
#include <opm/material/localad/Math.hpp>

#include <dune/common/fvector.hh>

namespace Ewoms {

/*!
 * \ingroup NcpModel
 *
 * \brief Represents the primary variables used by the compositional
 *        multi-phase NCP model.
 *
 * This class is basically a Dune::FieldVector which can retrieve its
 * contents from an aribitatry fluid state.
 */
template <class TypeTag>
class NcpPrimaryVariables : public FvBasePrimaryVariables<TypeTag>
{
    typedef FvBasePrimaryVariables<TypeTag> ParentType;

    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, Evaluation) Evaluation;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, MaterialLaw) MaterialLaw;
    typedef typename GET_PROP_TYPE(TypeTag, MaterialLawParams) MaterialLawParams;

    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;
    enum { pressure0Idx = Indices::pressure0Idx };
    enum { saturation0Idx = Indices::saturation0Idx };
    enum { fugacity0Idx = Indices::fugacity0Idx };

    enum { numPhases = GET_PROP_VALUE(TypeTag, NumPhases) };
    enum { numComponents = GET_PROP_VALUE(TypeTag, NumComponents) };
    typedef Dune::FieldVector<Scalar, numComponents> ComponentVector;

    enum { enableEnergy = GET_PROP_VALUE(TypeTag, EnableEnergy) };
    typedef Ewoms::EnergyModule<TypeTag, enableEnergy> EnergyModule;

    typedef Opm::NcpFlash<Scalar, FluidSystem> NcpFlash;
    typedef Opm::MathToolbox<Evaluation> Toolbox;

public:
    NcpPrimaryVariables() : ParentType()
    {}

    /*!
     * \copydoc ImmisciblePrimaryVariables::ImmisciblePrimaryVariables(Scalar)
     */
    NcpPrimaryVariables(Scalar value) : ParentType(value)
    {}

    /*!
     * \copydoc ImmisciblePrimaryVariables::ImmisciblePrimaryVariables(const
     * ImmisciblePrimaryVariables &)
     */
    NcpPrimaryVariables(const NcpPrimaryVariables &value) : ParentType(value)
    {}

    /*!
     * \copydoc ImmisciblePrimaryVariables::assignMassConservative
     */
    template <class FluidState>
    void assignMassConservative(const FluidState &fluidState,
                                const MaterialLawParams &matParams,
                                bool isInEquilibrium = false)
    {
        typedef Opm::MathToolbox<typename FluidState::Scalar> FsToolbox;

#ifndef NDEBUG
        // make sure the temperature is the same in all fluid phases
        for (int phaseIdx = 1; phaseIdx < numPhases; ++phaseIdx) {
            assert(fluidState.temperature(0) == fluidState.temperature(phaseIdx));
        }
#endif // NDEBUG

        // for the equilibrium case, we don't need complicated
        // computations.
        if (isInEquilibrium) {
            assignNaive(fluidState);
            return;
        }

        // use a flash calculation to calculate a fluid state in
        // thermodynamic equilibrium
        typename FluidSystem::ParameterCache paramCache;
        Opm::CompositionalFluidState<Scalar, FluidSystem> fsFlash;

        // calculate the "global molarities"
        ComponentVector globalMolarities(0.0);
        for (int compIdx = 0; compIdx < numComponents; ++compIdx) {
            for (int phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx) {
                globalMolarities[compIdx] +=
                    FsToolbox::value(fluidState.saturation(phaseIdx))
                    * FsToolbox::value(fluidState.molarity(phaseIdx, compIdx));
            }
        }

        // use the externally given fluid state as initial value for
        // the flash calculation
        fsFlash.assign(fluidState);
        // NcpFlash::guessInitial(fsFlash, paramCache, globalMolarities);

        // run the flash calculation
        NcpFlash::template solve<MaterialLaw>(fsFlash, paramCache, matParams, globalMolarities);

        // use the result to assign the primary variables
        assignNaive(fsFlash);
    }

    /*!
     * \copydoc ImmisciblePrimaryVariables::assignNaive
     */
    template <class FluidState>
    void assignNaive(const FluidState &fluidState, unsigned refPhaseIdx = 0)
    {
        typedef Opm::MathToolbox<typename FluidState::Scalar> FsToolbox;

        // assign the phase temperatures. this is out-sourced to
        // the energy module
        EnergyModule::setPriVarTemperatures(*this, fluidState);

        // assign fugacities.
        for (int compIdx = 0; compIdx < numComponents; ++compIdx) {
            Scalar fug = FsToolbox::value(fluidState.fugacity(refPhaseIdx, compIdx));
            (*this)[fugacity0Idx + compIdx] = fug;
        }

        // assign pressure of first phase
        (*this)[pressure0Idx] = FsToolbox::value(fluidState.pressure(/*phaseIdx=*/0));

        // assign first M - 1 saturations
        for (int phaseIdx = 0; phaseIdx < numPhases - 1; ++phaseIdx)
            (*this)[saturation0Idx + phaseIdx] = FsToolbox::value(fluidState.saturation(phaseIdx));
    }
};

} // namespace Ewoms

#endif
