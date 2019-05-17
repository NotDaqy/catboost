#pragma once

#include "kernel.cuh"

#include <catboost/libs/helpers/exception.h>

#include <util/datetime/base.h>
#include <util/generic/array_ref.h>

#include <cuda_runtime.h>

#define CUDA_SAFE_CALL(statement)                                                                                    \
    {                                                                                                                \
        cudaError_t errorCode = statement;                                                                           \
        if (errorCode != cudaSuccess && errorCode != cudaErrorCudartUnloading) {                                     \
            ythrow TCatBoostException() << "CUDA error " << (int)errorCode << ": " << cudaGetErrorString(errorCode); \
        }                                                                                                            \
    }

class TCudaEvent;

class TCudaStream {
private:
    struct TImpl: public TThrRefBase, public TNonCopyable {
    public:
        ~TImpl() {
            CUDA_SAFE_CALL(cudaStreamDestroy(Stream_));
        }

        explicit TImpl(bool nonBlocking)
            : NonBlocking(nonBlocking)
        {
            if (nonBlocking) {
                CUDA_SAFE_CALL(cudaStreamCreateWithFlags(&Stream_, cudaStreamNonBlocking));
            } else {
                CUDA_SAFE_CALL(cudaStreamCreate(&Stream_));
            }
        }

        cudaStream_t Stream_;
        bool NonBlocking = false;
    };

    using TStreamPtr = TIntrusivePtr<TImpl>;

    TCudaStream(TStreamPtr ptr)
        : Stream_(ptr)
    {
    }

    TCudaStream() {
    }

private:
    TStreamPtr Stream_;

public:
    void Synchronize() const {
        CUDA_SAFE_CALL(cudaStreamSynchronize(GetStream()));
    }

    operator cudaStream_t() const {
        return GetStream();
    }

    cudaStream_t GetStream() const {
        return Stream_ ? Stream_->Stream_ : cudaStreamPerThread;
    }

    static TCudaStream ZeroStream() {
        return TCudaStream();
    }

    static TCudaStream NewStream(bool nonBlocking = true) {
        return TCudaStream(new TImpl(nonBlocking));
    }

    void WaitEvent(const TCudaEvent& event) const;
};

inline void DeviceSynchronize() {
    CUDA_SAFE_CALL(cudaDeviceSynchronize());
}

class TDeviceGuard: private TNonCopyable {
public:
    TDeviceGuard(int device) {
        CUDA_SAFE_CALL(cudaGetDevice(&PreviousDevice));
        if (device != PreviousDevice) {
            CUDA_SAFE_CALL(cudaSetDevice(device));
        } else {
            PreviousDevice = -1;
        }
    }

    ~TDeviceGuard() {
        if (PreviousDevice != -1) {
            CUDA_SAFE_CALL(cudaSetDevice(PreviousDevice));
        }
    }

private:
    int PreviousDevice = -1;
};

class TProfile: private TNonCopyable {
public:
    TProfile(const TString& message)
        : Message_(message)
    {
        DeviceSynchronize();
        Start = TInstant::Now();
    }

    ~TProfile() {
        DeviceSynchronize();
        Cout << Message_ << " in " << (TInstant::Now() - Start).SecondsFloat() << " seconds" << Endl;
    }

private:
    TString Message_;
    TInstant Start;
};
//

template <class T>
inline void ClearMemoryAsync(TArrayRef<T> data, TCudaStream stream) {
    CUDA_SAFE_CALL(cudaMemsetAsync(reinterpret_cast<char*>(data.data()), 0, data.size() * sizeof(T), stream.GetStream()));
}
