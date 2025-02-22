/* Copyright 2022 Jeffrey Kelling
 *
 * This file exemplifies usage of alpaka.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL ISC BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <alpaka/alpaka.hpp>
#include <alpaka/example/ExampleDefaultAcc.hpp>
#include <alpaka/rand/RandPhiloxStateless.hpp>

#include <chrono>
#include <iostream>
#include <random>
#include <typeinfo>

//! A kernel that fills an array with pseudo-random numbers.
class CounterBasedRngKernel
{
public:
    template<class TAcc>
    using Vec = alpaka::Vec<alpaka::Dim<TAcc>, alpaka::Idx<TAcc>>;
    template<class TAcc>
    using Gen = typename alpaka::rand::PhiloxStateless4x32x10Vector<TAcc>;
    template<class TAcc>
    using Key = typename Gen<TAcc>::Key;
    template<class TAcc>
    using Counter = typename Gen<TAcc>::Counter;

private:
    template<unsigned int I>
    struct ElemLoop
    {
        ALPAKA_NO_HOST_ACC_WARNING
        template<typename TAcc, typename TElem>
        static ALPAKA_FN_ACC auto elemLoop(
            TAcc const& acc,
            alpaka::experimental::
                BufferAccessor<TAcc, TElem, alpaka::Dim<TAcc>::value, alpaka::experimental::WriteAccess> dst,
            Key<TAcc> const& key,
            Vec<TAcc> const& threadElemExtent,
            Vec<TAcc>& threadFirstElemIdx) -> void
        {
            auto const threadLastElemIdx = threadFirstElemIdx[I] + threadElemExtent[I];
            auto const threadLastElemIdxClipped
                = (dst.extents[I] > threadLastElemIdx) ? threadLastElemIdx : dst.extents[I];

            constexpr auto Dim = alpaka::Dim<TAcc>::value;

            auto const firstElem = threadFirstElemIdx[I];
            if constexpr(I < Dim - 1)
            {
                for(; threadFirstElemIdx[I] < threadLastElemIdxClipped; ++threadFirstElemIdx[I])
                {
                    ElemLoop<I + 1>::elemLoop(acc, dst, key, threadElemExtent, threadFirstElemIdx);
                }
            }
            else
            {
                Counter<TAcc> c = {0, 0, 0, 0};
                for(unsigned int i = 0; i < Dim; ++i)
                    c[i] = threadFirstElemIdx[i];

                for(; threadFirstElemIdx[Dim - 1] < threadLastElemIdxClipped; ++threadFirstElemIdx[Dim - 1])
                {
                    c[Dim - 1] = threadFirstElemIdx[Dim - 1];
                    auto const random = Gen<TAcc>::generate(c, key);
                    // to make use of the whole random vector we would need to ensure numElement[0] % 4 == 0
                    dst[threadFirstElemIdx] = TElem(random[0]);
                }
            }
            threadFirstElemIdx[I] = firstElem;
        }
    };

public:
    //! The kernel entry point.
    //!
    //! \tparam TAcc The accelerator environment to be executed on.
    //! \tparam TElem The matrix element type.
    //! \param acc The accelerator to be executed on.
    //! \param dst destimation matrix.
    //! \param extent The matrix dimension in elements.
    ALPAKA_NO_HOST_ACC_WARNING
    template<typename TAcc, typename TElem>
    ALPAKA_FN_ACC auto operator()(
        TAcc const& acc,
        alpaka::experimental::BufferAccessor<TAcc, TElem, alpaka::Dim<TAcc>::value, alpaka::experimental::WriteAccess>
            dst,
        Key<TAcc> const& key) const -> void
    {
        constexpr auto Dim = alpaka::Dim<TAcc>::value;
        static_assert(Dim <= 4, "The CounterBasedRngKernel expects at most 4-dimensional indices!");

        Vec<TAcc> const gridThreadIdx(alpaka::getIdx<alpaka::Grid, alpaka::Threads>(acc));
        Vec<TAcc> const threadElemExtent(alpaka::getWorkDiv<alpaka::Thread, alpaka::Elems>(acc));
        Vec<TAcc> threadFirstElemIdx(gridThreadIdx * threadElemExtent);

        ElemLoop<0>::elemLoop(acc, dst, key, threadElemExtent, threadFirstElemIdx);
    }
};

auto main() -> int
{
// Fallback for the CI with disabled sequential backend
#if defined(ALPAKA_CI) && !defined(ALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED)
    return EXIT_SUCCESS;
#else

    // Define the index domain
    using Dim = alpaka::DimInt<3u>;
    using Idx = std::size_t;

    // Define the accelerator
    //
    // It is possible to choose from a set of accelerators:
    // - AccGpuCudaRt
    // - AccGpuHipRt
    // - AccCpuThreads
    // - AccCpuFibers
    // - AccCpuOmp2Threads
    // - AccCpuOmp2Blocks
    // - AccOmp5
    // - AccCpuTbbBlocks
    // - AccCpuSerial
    // using Acc = alpaka::AccCpuSerial<Dim, Idx>;
    using Acc = alpaka::ExampleDefaultAcc<Dim, Idx>;
    std::cout << "Using alpaka accelerator: " << alpaka::getAccName<Acc>() << std::endl;

    using AccHost = alpaka::AccCpuSerial<Dim, Idx>;
    // Get the host device for allocating memory on the host.
    using DevHost = alpaka::DevCpu;

    // Defines the synchronization behavior of a queue
    //
    // choose between Blocking and NonBlocking
    using QueueProperty = alpaka::Blocking;
    using QueueAcc = alpaka::Queue<Acc, QueueProperty>;
    using QueueHost = alpaka::Queue<AccHost, QueueProperty>;

    // Select a device
    auto const devAcc = alpaka::getDevByIdx<Acc>(0u);
    auto const devHost = alpaka::getDevByIdx<AccHost>(0u);

    // Create a queue on the device
    QueueAcc queueAcc(devAcc);
    QueueHost queueHost(devHost);

    // Define the work division
    alpaka::Vec<Dim, Idx> const extent = {16, 16, 16 * 8};
    alpaka::Vec<Dim, Idx> const elementsPerThread = {1, 1, 1};
    alpaka::Vec<Dim, Idx> const elementsPerThreadHost = {1, 1, 8};

    // Let alpaka calculate good block and grid sizes given our full problem extent
    alpaka::WorkDivMembers<Dim, Idx> const workDivAcc(alpaka::getValidWorkDiv<Acc>(
        devAcc,
        extent,
        elementsPerThread,
        false,
        alpaka::GridBlockExtentSubDivRestrictions::Unrestricted));
    alpaka::WorkDivMembers<Dim, Idx> const workDivHost(alpaka::getValidWorkDiv<AccHost>(
        devHost,
        extent,
        elementsPerThreadHost,
        false,
        alpaka::GridBlockExtentSubDivRestrictions::Unrestricted));

    // Define the buffer element type
    using Data = std::uint32_t;

    // Allocate 3 host memory buffers
    using BufHost = alpaka::Buf<DevHost, Data, Dim, Idx>;
    BufHost bufHost(alpaka::allocBuf<Data, Idx>(devHost, extent));
    BufHost bufHostDev(alpaka::allocBuf<Data, Idx>(devHost, extent));

    // Initialize the host input vectors A and B
    Data* const pBufHost(alpaka::getPtrNative(bufHost));
    Data* const pBufHostDev(alpaka::getPtrNative(bufHostDev));

    std::random_device rd{};
    CounterBasedRngKernel::Key<AccHost> key = {rd(), rd()};

    // Allocate buffer on the accelerator
    using BufAcc = alpaka::Buf<Acc, Data, Dim, Idx>;
    BufAcc bufAcc(alpaka::allocBuf<Data, Idx>(devAcc, extent));

    // Create the kernel execution task.
    auto const taskKernelAcc = alpaka::createTaskKernel<Acc>(
        workDivAcc,
        CounterBasedRngKernel(),
        alpaka::experimental::writeAccess(bufAcc),
        key);
    auto const taskKernelHost = alpaka::createTaskKernel<AccHost>(
        workDivHost,
        CounterBasedRngKernel(),
        alpaka::experimental::writeAccess(bufHost),
        key);

    // Enqueue the kernel execution task
    alpaka::enqueue(queueHost, taskKernelHost);
    alpaka::enqueue(queueAcc, taskKernelAcc);

    // Copy the result from the device
    alpaka::memcpy(queueAcc, bufHostDev, bufAcc);
    auto const numElements = extent.prod();

    // wait in case we are using an asynchronous queue to time actual kernel runtime
    alpaka::wait(queueHost);
    alpaka::wait(queueAcc);

    int falseResults = 0;
    int const maxPrintFalseResults = extent[2] * 2;

    auto aHost = alpaka::experimental::readAccess(bufHost);
    auto aAcc = alpaka::experimental::readAccess(bufHostDev);
    for(Idx z = 0; z < aHost.extents[0]; ++z)
        for(Idx y = 0; y < aHost.extents[1]; ++y)
            for(Idx x = 0; x < aHost.extents[2]; ++x)
            {
                Data const& valHost(aHost(z, y, x));
                Data const& valAcc(aAcc(z, y, x));
                if(valHost != valAcc)
                {
                    if(falseResults < maxPrintFalseResults)
                        std::cerr << "host[" << z << ", " << y << ", " << x << "] = " << valHost << " != acc[" << z
                                  << ", " << y << ", " << x << "] = " << valAcc << std::endl;
                    ++falseResults;
                }
            }

    if(falseResults == 0)
    {
        std::cout << "Execution results correct!" << std::endl;
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Found " << falseResults << " false results, printed no more than " << maxPrintFalseResults
                  << "\n"
                  << "Execution results incorrect!" << std::endl;
        return EXIT_FAILURE;
    }
#endif
}
