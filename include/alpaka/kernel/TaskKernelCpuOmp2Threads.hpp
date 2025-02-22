/* Copyright 2022 Axel Huebl, Benjamin Worpitz, Bert Wesarg, René Widera, Jan Stephan, Bernhard Manfred Gruber
 *
 * This file is part of alpaka.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#ifdef ALPAKA_ACC_CPU_B_SEQ_T_OMP2_ENABLED

#    if _OPENMP < 200203
#        error If ALPAKA_ACC_CPU_B_SEQ_T_OMP2_ENABLED is set, the compiler has to support OpenMP 2.0 or higher!
#    endif

// Specialized traits.
#    include <alpaka/acc/Traits.hpp>
#    include <alpaka/dev/Traits.hpp>
#    include <alpaka/dim/Traits.hpp>
#    include <alpaka/idx/Traits.hpp>
#    include <alpaka/pltf/Traits.hpp>

// Implementation details.
#    include <alpaka/acc/AccCpuOmp2Threads.hpp>
#    include <alpaka/core/Decay.hpp>
#    include <alpaka/dev/DevCpu.hpp>
#    include <alpaka/kernel/Traits.hpp>
#    include <alpaka/meta/NdLoop.hpp>
#    include <alpaka/workdiv/WorkDivMembers.hpp>

#    include <omp.h>

#    include <functional>
#    include <stdexcept>
#    include <tuple>
#    include <type_traits>
#    if ALPAKA_DEBUG >= ALPAKA_DEBUG_MINIMAL
#        include <iostream>
#    endif

namespace alpaka
{
    //! The CPU OpenMP 2.0 thread accelerator execution task.
    template<typename TDim, typename TIdx, typename TKernelFnObj, typename... TArgs>
    class TaskKernelCpuOmp2Threads final : public WorkDivMembers<TDim, TIdx>
    {
    public:
        template<typename TWorkDiv>
        ALPAKA_FN_HOST TaskKernelCpuOmp2Threads(TWorkDiv&& workDiv, TKernelFnObj const& kernelFnObj, TArgs&&... args)
            : WorkDivMembers<TDim, TIdx>(std::forward<TWorkDiv>(workDiv))
            , m_kernelFnObj(kernelFnObj)
            , m_args(std::forward<TArgs>(args)...)
        {
            static_assert(
                Dim<std::decay_t<TWorkDiv>>::value == TDim::value,
                "The work division and the execution task have to be of the same dimensionality!");
        }

        //! Executes the kernel function object.
        ALPAKA_FN_HOST auto operator()() const -> void
        {
            ALPAKA_DEBUG_MINIMAL_LOG_SCOPE;

            auto const gridBlockExtent = getWorkDiv<Grid, Blocks>(*this);
            auto const blockThreadExtent = getWorkDiv<Block, Threads>(*this);
            auto const threadElemExtent = getWorkDiv<Thread, Elems>(*this);

            // Get the size of the block shared dynamic memory.
            auto const blockSharedMemDynSizeBytes = std::apply(
                [&](ALPAKA_DECAY_T(TArgs) const&... args)
                {
                    return getBlockSharedMemDynSizeBytes<AccCpuOmp2Threads<TDim, TIdx>>(
                        m_kernelFnObj,
                        blockThreadExtent,
                        threadElemExtent,
                        args...);
                },
                m_args);

#    if ALPAKA_DEBUG >= ALPAKA_DEBUG_FULL
            std::cout << __func__ << " blockSharedMemDynSizeBytes: " << blockSharedMemDynSizeBytes << " B"
                      << std::endl;
#    endif

            AccCpuOmp2Threads<TDim, TIdx> acc(
                *static_cast<WorkDivMembers<TDim, TIdx> const*>(this),
                blockSharedMemDynSizeBytes);

            // The number of threads in this block.
            TIdx const blockThreadCount(blockThreadExtent.prod());
            [[maybe_unused]] int const iBlockThreadCount(static_cast<int>(blockThreadCount));

            if(::omp_in_parallel() != 0)
            {
                throw std::runtime_error(
                    "The OpenMP 2.0 thread backend can not be used within an existing parallel region!");
            }

            // Force the environment to use the given number of threads.
            int const ompIsDynamic(::omp_get_dynamic());
            ::omp_set_dynamic(0);

            // Execute the blocks serially.
            meta::ndLoopIncIdx(
                gridBlockExtent,
                [&](Vec<TDim, TIdx> const& gridBlockIdx)
                {
                    acc.m_gridBlockIdx = gridBlockIdx;

// Execute the threads in parallel.

// Parallel execution of the threads in a block is required because when syncBlockThreads is called all of them have to
// be done with their work up to this line. So we have to spawn one OS thread per thread in a block. 'omp for' is not
// useful because it is meant for cases where multiple iterations are executed by one thread but in our case a 1:1
// mapping is required. Therefore we use 'omp parallel' with the specified number of threads in a block.
#    pragma omp parallel num_threads(iBlockThreadCount)
                    {
                        // The guard is for gcc internal compiler error, as discussed in #735
                        if constexpr((!BOOST_COMP_GNUC) || (BOOST_COMP_GNUC >= BOOST_VERSION_NUMBER(8, 1, 0)))
                        {
#    pragma omp single nowait
                            {
                                // The OpenMP runtime does not create a parallel region when only one thread is
                                // required in the num_threads clause. In all other cases we expect to be in a parallel
                                // region now.
                                if((iBlockThreadCount > 1) && (::omp_in_parallel() == 0))
                                {
                                    throw std::runtime_error(
                                        "The OpenMP 2.0 runtime did not create a parallel region!");
                                }

                                int const numThreads = ::omp_get_num_threads();
                                if(numThreads != iBlockThreadCount)
                                {
                                    throw std::runtime_error(
                                        "The OpenMP 2.0 runtime did not use the number of threads "
                                        "that had been required!");
                                }
                            }
                        }

                        std::apply(m_kernelFnObj, std::tuple_cat(std::tie(acc), m_args));

                        // Wait for all threads to finish before deleting the shared memory.
                        // This is done by default if the omp 'nowait' clause is missing on the omp parallel directive
                        // syncBlockThreads(acc);
                    }

                    // After a block has been processed, the shared memory has to be deleted.
                    freeSharedVars(acc);
                });

            // Reset the dynamic thread number setting.
            ::omp_set_dynamic(ompIsDynamic);
        }

    private:
        TKernelFnObj m_kernelFnObj;
        std::tuple<std::decay_t<TArgs>...> m_args;
    };

    namespace trait
    {
        //! The CPU OpenMP 2.0 block thread execution task accelerator type trait specialization.
        template<typename TDim, typename TIdx, typename TKernelFnObj, typename... TArgs>
        struct AccType<TaskKernelCpuOmp2Threads<TDim, TIdx, TKernelFnObj, TArgs...>>
        {
            using type = AccCpuOmp2Threads<TDim, TIdx>;
        };

        //! The CPU OpenMP 2.0 block thread execution task device type trait specialization.
        template<typename TDim, typename TIdx, typename TKernelFnObj, typename... TArgs>
        struct DevType<TaskKernelCpuOmp2Threads<TDim, TIdx, TKernelFnObj, TArgs...>>
        {
            using type = DevCpu;
        };

        //! The CPU OpenMP 2.0 block thread execution task dimension getter trait specialization.
        template<typename TDim, typename TIdx, typename TKernelFnObj, typename... TArgs>
        struct DimType<TaskKernelCpuOmp2Threads<TDim, TIdx, TKernelFnObj, TArgs...>>
        {
            using type = TDim;
        };

        //! The CPU OpenMP 2.0 block thread execution task platform type trait specialization.
        template<typename TDim, typename TIdx, typename TKernelFnObj, typename... TArgs>
        struct PltfType<TaskKernelCpuOmp2Threads<TDim, TIdx, TKernelFnObj, TArgs...>>
        {
            using type = PltfCpu;
        };

        //! The CPU OpenMP 2.0 block thread execution task idx type trait specialization.
        template<typename TDim, typename TIdx, typename TKernelFnObj, typename... TArgs>
        struct IdxType<TaskKernelCpuOmp2Threads<TDim, TIdx, TKernelFnObj, TArgs...>>
        {
            using type = TIdx;
        };
    } // namespace trait
} // namespace alpaka

#endif
