//
//  CPUBinary.cpp
//  MNN
//
//  Created by MNN on 2018/08/02.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#include "CPUBinary.hpp"
#include <math.h>
#include <algorithm>
#include "CPUBackend.hpp"
#include "CommonOptFunction.h"
#include "Macro.h"

namespace MNN {

template <typename T>
CPUBinary<T>::CPUBinary(Backend* b, int32_t type) : MNN::Execution(b), mType(type) {
    // nothing to do
}

template <typename T>
ErrorCode CPUBinary<T>::onResize(const std::vector<Tensor*>& inputs, const std::vector<Tensor*>& outputs) {
    MNN_ASSERT(1 == outputs.size());
    return NO_ERROR;
}

template <typename Tin, typename Tout, typename Func>
static ErrorCode _binaryOp(Tensor* input0, Tensor* input1, Tensor* output) {
    Func f;

    const int input0DataCount = input0->elementSize();
    const int input1DataCount = input1->elementSize();

    const Tin* input0Data = input0->host<Tin>();
    const Tin* input1Data = input1->host<Tin>();
    Tout* outputData      = output->host<Tout>();

    if (input0DataCount == 1) { // data count == 1, not only mean scalar input, maybe of shape (1, 1, 1, ...,1)
        for (int i = 0; i < input1DataCount; i++) {
            outputData[i] = static_cast<Tout>(f(input0Data[0], input1Data[i]));
        }
    } else if (input1DataCount == 1) {
        for (int i = 0; i < input0DataCount; i++) {
            outputData[i] = static_cast<Tout>(f(input0Data[i], input1Data[0]));
        }
    } else { // both input contains more than one element，which means no scalar input
        bool sameShape = input0->elementSize() == input1->elementSize();
        if (sameShape) { // two inputs have the same shape, apply element-wise operation
            for (int i = 0; i < input0DataCount; i++) {
                outputData[i] = static_cast<Tout>(f(input0Data[i], input1Data[i]));
            }
        } else { // not the same shape, use broadcast
#define MAX_DIM 6
            MNN_ASSERT(output->dimensions() <= MAX_DIM);
            int dims[MAX_DIM];
            int stride[MAX_DIM];
            int iStride0[MAX_DIM];
            int iStride1[MAX_DIM];
            for (int i = MAX_DIM - 1; i >= 0; --i) {
                dims[i]     = 1;
                stride[i]   = 0;
                iStride0[i] = 0;
                iStride1[i] = 0;
                int input0I = i - (output->dimensions() - input0->dimensions());
                int input1I = i - (output->dimensions() - input1->dimensions());
                if (i < output->dimensions()) {
                    dims[i]   = output->length(i);
                    stride[i] = output->stride(i);
                }
                if (input0I >= 0 && input0->length(input0I) != 1) {
                    iStride0[i] = input0->stride(input0I);
                }
                if (input1I >= 0 && input1->length(input1I) != 1) {
                    iStride1[i] = input1->stride(input1I);
                }
            }
            for (int w = 0; w < dims[5]; ++w) {
                auto ow  = outputData + w * stride[5];
                auto i0w = input0Data + w * iStride0[5];
                auto i1w = input1Data + w * iStride1[5];
#define PTR(x, y, i)                      \
    auto o##x  = o##y + x * stride[i];    \
    auto i0##x = i0##y + x * iStride0[i]; \
    auto i1##x = i1##y + x * iStride1[i]

                for (int v = 0; v < dims[4]; ++v) {
                    PTR(v, w, 4);
                    for (int u = 0; u < dims[3]; ++u) {
                        PTR(u, v, 3);
                        for (int z = 0; z < dims[2]; ++z) {
                            PTR(z, u, 2);
                            for (int y = 0; y < dims[1]; ++y) {
                                PTR(y, z, 1);
                                for (int x = 0; x < dims[0]; ++x) {
                                    PTR(x, y, 0);
                                    *ox = static_cast<Tout>(f(*i0x, *i1x));
                                }
                            }
                        }
                    }
                }
            }
#undef MAX_DIM
#undef PTR
        }
        // broadcast-capable check is done in compute size
    }

    return NO_ERROR;
}

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryMax : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return std::max(x, y);
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryMin : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return std::min(x, y);
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryMul : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return x * y;
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryAdd : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return x + y;
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinarySub : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return x - y;
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryRealDiv : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return x / y;
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryGreater : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return (_ErrorCode)((x > y) ? 1 : 0);
    }
};
template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryLess : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return (_ErrorCode)((x < y) ? 1 : 0);
    }
};
template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryGreaterEqual : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return (_ErrorCode)((x >= y) ? 1 : 0);
    }
};
template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryEqual : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return (_ErrorCode)((x == y) ? 1 : 0);
    }
};
template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryFloorDiv : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return floor(x / y);
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinarySquaredDifference : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return (x - y) * (x - y);
    }
};

template <typename _Arg1, typename _Arg2, typename _ErrorCode>
struct BinaryPow : std::binary_function<_Arg1, _Arg2, _ErrorCode> {
    _ErrorCode operator()(const _Arg1& x, const _Arg2& y) const {
        return pow(x, y);
    }
};

template <typename T>
ErrorCode CPUBinary<T>::onExecute(const std::vector<Tensor*>& inputs, const std::vector<Tensor*>& outputs) {
    auto input  = inputs[0];
    auto input1 = inputs[1];
    auto output = outputs[0];

    switch (mType) {
        case BinaryOpOperation_MUL:
            _binaryOp<T, T, BinaryMul<T, T, T>>(input, input1, output);
            break;
        case BinaryOpOperation_ADD:
            _binaryOp<T, T, BinaryAdd<T, T, T>>(input, input1, output);
            break;
        case BinaryOpOperation_SUB:
            _binaryOp<T, T, BinarySub<T, T, T>>(input, input1, output);
            break;

        case BinaryOpOperation_REALDIV:
            _binaryOp<T, T, BinaryRealDiv<T, T, T>>(input, input1, output);
            break;
        case BinaryOpOperation_MINIMUM:
            _binaryOp<T, T, BinaryMin<T, T, T>>(input, input1, output);
            break;
        case BinaryOpOperation_MAXIMUM:
            _binaryOp<T, T, BinaryMax<T, T, T>>(input, input1, output);
            break;

        case BinaryOpOperation_GREATER:
            _binaryOp<T, int32_t, BinaryGreater<T, T, int32_t>>(input, input1, output);
            break;
        case BinaryOpOperation_LESS:
            _binaryOp<T, T, BinaryLess<T, T, int32_t>>(input, input1, output);
            break;
        case BinaryOpOperation_GREATER_EQUAL:
            _binaryOp<T, T, BinaryGreaterEqual<T, T, int32_t>>(input, input1, output);
            break;
        case BinaryOpOperation_EQUAL:
            _binaryOp<T, T, BinaryEqual<T, T, int32_t>>(input, input1, output);
            break;
        case BinaryOpOperation_FLOORDIV:
            _binaryOp<T, T, BinaryFloorDiv<T, T, T>>(input, input1, output);
            break;
        case BinaryOpOperation_POW:
            _binaryOp<T, T, BinaryPow<T, T, T>>(input, input1, output);
            break;
        case BinaryOpOperation_SquaredDifference:
            _binaryOp<T, T, BinarySquaredDifference<T, T, T>>(input, input1, output);
            break;
        default:
            MNN_ASSERT(false);
            break;
    }
    return NO_ERROR;
}

class CPUBinaryCreator : public CPUBackend::Creator {
public:
    virtual Execution* onCreate(const std::vector<Tensor*>& inputs, const std::vector<Tensor*>& outputs,
                                const MNN::Op* op, Backend* backend) const override {
        auto dataType   = outputs[0]->getType();
        int32_t type = op->main_as_BinaryOp()->opType();
        if (dataType.bits == 32) {
            if (dataType.code == halide_type_int) {
                return new CPUBinary<int32_t>(backend, type);
            }
            if (dataType.code == halide_type_float) {
                return new CPUBinary<float>(backend, type);
            }
        }
        return nullptr;
    }
};

REGISTER_CPU_OP_CREATOR(CPUBinaryCreator, OpType_BinaryOp);

} // namespace MNN
