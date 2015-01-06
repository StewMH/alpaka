/**
* \file
* Copyright 2014-2015 Benjamin Worpitz
*
* This file is part of alpaka.
*
* alpaka is free software: you can redistribute it and/or modify
* it under the terms of either the GNU General Public License or
* the GNU Lesser General Public License for more details. as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* alpaka is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with alpaka.
* If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <alpaka/interfaces/Atomic.hpp> // IAtomic

namespace alpaka
{
    namespace serial
    {
        namespace detail
        {
            //#############################################################################
            //! The serial accelerator atomic operations.
            //#############################################################################
            class AtomicSerial
            {
            public:
                //-----------------------------------------------------------------------------
                //! Default-constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AtomicSerial() = default;
                //-----------------------------------------------------------------------------
                //! Copy-constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AtomicSerial(AtomicSerial const &) = default;
                //-----------------------------------------------------------------------------
                //! Move-constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AtomicSerial(AtomicSerial &&) = default;
                //-----------------------------------------------------------------------------
                //! Copy-assignment.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AtomicSerial & operator=(AtomicSerial const &) = delete;
                //-----------------------------------------------------------------------------
                //! Destructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST ~AtomicSerial() noexcept = default;
            };
            using TInterfacedAtomic = alpaka::detail::IAtomic<AtomicSerial>;
        }
    }

    namespace detail
    {
        //#############################################################################
        //! The serial accelerator atomic operation functor.
        //#############################################################################
        template<typename TOp, typename T>
        struct AtomicOp<serial::detail::AtomicSerial, TOp, T>
        {
            ALPAKA_FCT_HOST T operator()(serial::detail::AtomicSerial const &, T * const addr, T const & value) const
            {
                return TOp()(addr, value);
            }
        };
    }
}
