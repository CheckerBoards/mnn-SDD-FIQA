//
//  CPULSTM.cpp
//  MNN
//
//  Created by MNN on 2018/07/17.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#include "CPULSTM.hpp"
#include <math.h>
#include "CPUBackend.hpp"
#include "BufferAllocator.hpp"
#include "CommonOptFunction.h"
#include "Concurrency.h"
#include "Macro.h"
#include "TensorUtils.hpp"

#ifdef MNN_USE_NEON
#include <arm_neon.h>
#endif

namespace MNN {

static inline float sigmoid(float x) {
    return 1. / (1. + expf(-x));
}

// copy data from src matrix to dst matrix, and align up to 4x4
static void copyWeightAlignUp4x4(float* dst, const float* src, int numUnits, int numFeatures, int devide) {
    int permuteIndex[] = {0, 1, 2, 3};
    if (devide) {
        permuteIndex[2] = 3;
        permuteIndex[3] = 2;
    }
    for (int i = 0; i < 4; ++i) {
        const float* srcData = src + permuteIndex[i] * numUnits * numFeatures;
        float* dstData = dst + i * numUnits * ALIGN_UP4(numFeatures);
        int w = 0, outputIndex = 0;
        for (; w + 3 < numFeatures; w += 4) {
            for (int h = 0, inputIndex = w; h < numUnits; ++h, outputIndex += 4, inputIndex += numFeatures) {
                dstData[outputIndex] = srcData[inputIndex];
                dstData[outputIndex + 1] = srcData[inputIndex + 1];
                dstData[outputIndex + 2] = srcData[inputIndex + 2];
                dstData[outputIndex + 3] = srcData[inputIndex + 3];
            }
        }
        if (w < numFeatures) {
            for (int h = 0, inputIndex = w, ww; h < numUnits; ++h, inputIndex += numUnits) {
                for (ww = 0; ww < numFeatures - w; ++ww) {
                    dstData[outputIndex++] = srcData[inputIndex + ww];
                }
                for (; ww < 4; ++ww) {
                    dstData[outputIndex++] = 0;
                }
            }
        }
    }
}
    
CPULSTM::CPULSTM(Backend *backend, const LSTM *LSTM) : Execution(backend), mLSTM(LSTM) {
    // nothing to do
}

CPULSTM::~CPULSTM() {
    if (mInit) {
        backend()->onReleaseBuffer(mWeightH.get(), Backend::STATIC);
        backend()->onReleaseBuffer(mWeightI.get(), Backend::STATIC);
        backend()->onReleaseBuffer(mBiasC.get(), Backend::STATIC);
    }
}

ErrorCode CPULSTM::onResize(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) {
    auto &input = inputs[0];
    auto &output = outputs[0];
    const int batch = input->buffer().dim[0].extent;
    const int timeSteps = input->buffer().dim[1].extent;
    const int numFeatures = input->buffer().dim[3].extent;
    const int numUnits = output->buffer().dim[3].extent;

    mInput.buffer().dim[0].extent = batch * UP_DIV(timeSteps, 4);
    mInput.buffer().dim[1].extent = UP_DIV(numFeatures, 4);
    mInput.buffer().dim[2].extent = 16;
    mInput.buffer().dimensions    = 3;
    TensorUtils::setLinearLayout(&mInput); // We must invoke setLinearLayout on mInput, otherwise stride value of tensor is incorrect
    bool success                  = backend()->onAcquireBuffer(&mInput, Backend::DYNAMIC);

    mTransposeInputFunction = [batch, timeSteps, numFeatures](const float* src, float* dst) {
        if (numFeatures % 4 == 0) {
            memcpy(dst, src, batch * ALIGN_UP4(timeSteps) * numFeatures * sizeof(float));
            return;
        }
        const int height = batch * UP_DIV(timeSteps, 4);
        const int lineBytes = 4 * numFeatures * sizeof(float);
        const int remainBytes = 4 * ALIGN_UP4(numFeatures) * sizeof(float) - lineBytes;
        for (int h = 0; h < height; ++h, dst += 4 * ALIGN_UP4(numFeatures), src += 4 * numFeatures) {
            memcpy(dst, src, lineBytes);
            memset(dst + 4 * numFeatures, 0, remainBytes);
        }
    };
    
    // cont transform space
    if (inputs.size() > 1) {
        auto &cont = inputs[1];
        TensorUtils::copyShape(cont, &mCont);
        mCont.buffer().dim[1].flags = 0;
        success                     = success && backend()->onAcquireBuffer(&mCont, Backend::DYNAMIC);
    }

    mOutput.buffer().dim[0].extent = timeSteps * numUnits;
    mOutput.buffer().dimensions = 1;
    success                       = success && backend()->onAcquireBuffer(&mOutput, Backend::DYNAMIC);

    // divide weight & bias if needed
    auto weightI   = mLSTM->weightI();
    auto weightH   = mLSTM->weightH();
    int weightSize = weightI->dims()->data()[0];

    // gate space
    mGates.buffer().dim[0].extent = batch * ALIGN_UP4(timeSteps) * numUnits * 4;
    mGates.buffer().dimensions    = 1;
    success                       = success && backend()->onAcquireBuffer(&mGates, Backend::DYNAMIC);
    //MNN_PRINT("%d, %d\n", batch * ALIGN_UP4(timeSteps) * numUnits * 4, mGates.elementSize());
    // cell space
    mCell.buffer().dim[0].extent = numUnits;
    mCell.buffer().dimensions    = 1;
    success                      = success && backend()->onAcquireBuffer(&mCell, Backend::DYNAMIC);
    if (!success) {
        return OUT_OF_MEMORY;
    }
    
    if (!mInit) {
        mInit       = true;
        auto devide = weightI && !weightH && weightSize == 4 * numUnits * (numFeatures + numUnits + 2);
        mWeightI.reset(Tensor::createDevice<float>(std::vector<int>{4, UP_DIV(numFeatures, 4), numUnits, 4}));
        mWeightH.reset(Tensor::createDevice<float>(std::vector<int>{numUnits * numUnits * 4}));
        mBiasC.reset(Tensor::createDevice<float>(std::vector<int>{numUnits * 4}));
        success = success && backend()->onAcquireBuffer(mWeightH.get(), Backend::STATIC);
        success = success && backend()->onAcquireBuffer(mWeightI.get(), Backend::STATIC);
        success = success && backend()->onAcquireBuffer(mBiasC.get(), Backend::STATIC);
        if (!success) {
            return OUT_OF_MEMORY;
        }
        copyWeightAlignUp4x4(mWeightI->host<float>(), mLSTM->weightI()->float32s()->data(), numUnits, numFeatures, devide);
        if (devide) {
            auto data = weightI->float32s()->data() + 4 * numUnits * numFeatures;
            {
                float *to = mWeightH->host<float>();
                int step  = numUnits * numUnits;
                memcpy(to, data, 2 * step * sizeof(float));
                to += 2 * step;
                data += 2 * step;                              // IF
                memcpy(to, data + step, step * sizeof(float)); // O
                memcpy(to + step, data, step * sizeof(float)); // G
                data += 2 * step;
            }
            {
                float *to = mBiasC->host<float>();
                int step  = numUnits;
                memcpy(to, data, 2 * step * sizeof(float));
                to += 2 * step;
                data += 2 * step;                              // IF
                memcpy(to, data + step, step * sizeof(float)); // O
                memcpy(to + step, data, step * sizeof(float)); // G
                // data += 2 * step;
            }
        } else {
            ::memcpy(mBiasC->host<float>(), mLSTM->bias()->float32s()->data(), mBiasC->size());
            ::memcpy(mWeightH->host<float>(), mLSTM->weightH()->float32s()->data(), mWeightH->size());
        }
    }
    
    if (inputs.size() > 1) {
        backend()->onReleaseBuffer(&mCont, Backend::DYNAMIC);
    }
    backend()->onReleaseBuffer(&mOutput, Backend::DYNAMIC);
    backend()->onReleaseBuffer(&mCell, Backend::DYNAMIC);
    
    const int maxDepth = 5;
    const bool cacheB = false;
    BufferAllocator* memoryPool = ((CPUBackend *)backend())->getBufferAllocator();
    memoryPool->barrierBegin();
    std::shared_ptr<void> __a(nullptr, [memoryPool](void *) { memoryPool->barrierEnd(); });
    for (int i = 0; i < 4; ++i) {
        float* weightData = mWeightI->host<float>() + i * mWeightI->stride(0);
        mUnits[i].mTempWeight.reset(Tensor::create<float>(std::vector<int>{UP_DIV(numFeatures, 4), numUnits, 4}, weightData));
        float* gateData = mGates.host<float>() + i * batch * ALIGN_UP4(timeSteps) * numUnits;
        mUnits[i].mTempGates.reset(Tensor::create<float>(std::vector<int>{batch * UP_DIV(timeSteps, 4), numUnits, 4}, gateData));
        mUnits[i].mTempInputVector = std::vector<Tensor*>{mUnits[i].mTempWeight.get(), &mInput};
        mUnits[i].mTempOutputVector = std::vector<Tensor*>{mUnits[i].mTempGates.get()};
        mUnits[i].mStracssenComputor.reset(new StrassenMatrixComputor(backend(), maxDepth, cacheB));
        mUnits[i].mStracssenComputor->onReset();
        memoryPool->beginGroup();
        std::shared_ptr<void> __b(nullptr, [memoryPool](void *) { memoryPool->endGroup(); });
        mUnits[i].mStracssenComputor->onEncode(mUnits[i].mTempInputVector, mUnits[i].mTempOutputVector);
    }
    
    Tensor tempInternalTensor; // just for acquire memory efficiently
    tempInternalTensor.buffer().dim[0].extent = 4 * batch * ALIGN_UP4(timeSteps) * numUnits;
    tempInternalTensor.buffer().dimensions = 1;
    success = success && backend()->onAcquireBuffer(&tempInternalTensor, Backend::DYNAMIC);
    if (!success) {
        return OUT_OF_MEMORY;
    }
    float* tempData = tempInternalTensor.host<float>();
    backend()->onReleaseBuffer(&tempInternalTensor, Backend::DYNAMIC);
    
    mRetriveOutputFunction = [batch, timeSteps, numUnits, tempData](float* gateData) {
        const int itemSize = batch * ALIGN_UP4(timeSteps) * numUnits;
        for (int i = 0; i < 4; ++i) {
            MNNUnpackC4(tempData + i * itemSize, gateData + i * itemSize, numUnits, batch * ALIGN_UP4(timeSteps));
        }
        for (int i = 0, outputIndex = 0; i < itemSize; ++i, outputIndex += 4) {
            gateData[outputIndex] = tempData[i];
            gateData[outputIndex + 1] = tempData[i + itemSize];
            gateData[outputIndex + 2] = tempData[i + 2 * itemSize];
            gateData[outputIndex + 3] = tempData[i + 3 * itemSize];
        }
    };
    
    backend()->onReleaseBuffer(&mInput, Backend::DYNAMIC);
    backend()->onReleaseBuffer(&mGates, Backend::DYNAMIC);
    
    return NO_ERROR;
}

ErrorCode CPULSTM::onExecute(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) {
    auto &input           = inputs[0];
    auto &output          = outputs[0];
    const int batch = input->buffer().dim[0].extent;
    const int timeSteps = input->buffer().dim[1].extent;
    const int numUnits = output->buffer().dim[3].extent;
    const int threadNumber = ((CPUBackend*)backend())->threadNumber();
    
    mTransposeInputFunction(input->host<float>(), mInput.host<float>());
    MNN_CONCURRENCY_BEGIN(index, 4) {
        mUnits[index].mStracssenComputor->onExecute();
    }
    MNN_CONCURRENCY_END();
    mRetriveOutputFunction(mGates.host<float>());

    // tranform
    const float *contData = nullptr;
    if (inputs.size() > 1) {
        auto &cont = inputs[1];
        MNNUnpackC4(mCont.host<float>(), cont->host<float>(), cont->width() * cont->height(), cont->channel());
        contData = mCont.host<float>();
    }
    
    // calc weightHC
    auto cellData     = mCell.host<float>();
    memset(cellData, 0, numUnits * sizeof(float));
    const auto hcStep = batch * numUnits * numUnits;
    for (int batchIndex = 0; batchIndex < batch; ++batchIndex) {
        for (int ic = 0; ic < timeSteps; ic++) {
            // clip hidden by continuation indicator
            auto cont       = ic > 0 && (!contData || contData[ic]);
            auto outChannel = mOutput.host<float>() + ic * numUnits;
            MNN_CONCURRENCY_BEGIN(tId, threadNumber) {
                auto gatesPtr   = mGates.host<const float>() + ic * numUnits * 4 + tId * 4 + batchIndex * timeSteps * numUnits * 4;
                auto weightHCI  = mWeightH->host<const float>() + numUnits * tId;
                for (int oc = (int)tId; oc < numUnits; oc += threadNumber, gatesPtr += 4 * threadNumber, weightHCI += numUnits * threadNumber) {
                    float I = gatesPtr[0], F = gatesPtr[1], O = gatesPtr[2], G = gatesPtr[3];
                    // hidden
                    if (cont) {
                        auto weightHCF = weightHCI + hcStep;
                        auto weightHCO = weightHCF + hcStep;
                        auto weightHCG = weightHCO + hcStep;
                        auto hiddenPtr = mOutput.host<float>() + (ic - 1) * numUnits;
                        
                        int i = 0;
#ifdef MNN_USE_NEON
                        float32x4_t Ix4 = vdupq_n_f32(0);
                        float32x4_t Fx4 = vdupq_n_f32(0);
                        float32x4_t Ox4 = vdupq_n_f32(0);
                        float32x4_t Gx4 = vdupq_n_f32(0);
                        for (; i + 3 < numUnits; i += 4) {
                            const float32x4_t hiddenData = vld1q_f32(hiddenPtr + i);
                            Ix4 += vld1q_f32(weightHCI + i) * hiddenData;
                            Fx4 += vld1q_f32(weightHCF + i) * hiddenData;
                            Ox4 += vld1q_f32(weightHCO + i) * hiddenData;
                            Gx4 += vld1q_f32(weightHCG + i) * hiddenData;
                        }
#if !(defined(__ARM_FEATURE_FMA) && defined(__aarch64__))
#define vaddvq_f32(__v4) (__v4[0] + __v4[1] + __v4[2] + __v4[3]) // support A64 only
#endif
                        I += vaddvq_f32(Ix4);
                        F += vaddvq_f32(Fx4);
                        O += vaddvq_f32(Ox4);
                        G += vaddvq_f32(Gx4);
#endif
                        for (; i < numUnits; i++) {
                            const float hiddenData = hiddenPtr[i];
                            I += weightHCI[i] * hiddenData;
                            F += weightHCF[i] * hiddenData;
                            O += weightHCO[i] * hiddenData;
                            G += weightHCG[i] * hiddenData;
                        }
                    }
                    
                    // add bias
                    auto biasPtr = mBiasC->host<float>() + oc;
                    I            = sigmoid(*biasPtr + I);
                    biasPtr      = biasPtr + numUnits;
                    F            = cont ? sigmoid(*biasPtr + F) : 0.f;
                    biasPtr      = biasPtr + numUnits;
                    O            = sigmoid(*biasPtr + O);
                    biasPtr      = biasPtr + numUnits;
                    G            = tanhf(*biasPtr + G);
                    
                    auto newCell   = F * cellData[oc] + I * G;
                    cellData[oc]   = newCell;
                    auto H         = O * tanhf(newCell);
                    outChannel[oc] = H;
                }
            }
            MNN_CONCURRENCY_END();
        }
        MNNPackC4(output->host<float>() + batchIndex * output->stride(0), mOutput.host<float>(), output->width() * output->height(), output->channel());
    }
    return NO_ERROR;
}

class CPULSTMCreator : public CPUBackend::Creator {
public:
    virtual Execution *onCreate(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs,
                                const MNN::Op *op, Backend *backend) const {
        return new CPULSTM(backend, op->main_as_LSTM());
    }
};
REGISTER_CPU_OP_CREATOR(CPULSTMCreator, OpType_LSTM);

} // namespace MNN
