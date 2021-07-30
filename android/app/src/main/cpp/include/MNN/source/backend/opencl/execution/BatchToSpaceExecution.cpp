//
//  BatchToSpaceExecution.cpp
//  MNN
//
//  Created by MNN on 2019/02/28.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#include "execution/BatchToSpaceExecution.hpp"
#include "Macro.h"
#include "TensorUtils.hpp"

namespace MNN {
namespace OpenCL {

BatchToSpaceExecution::BatchToSpaceExecution(const std::vector<Tensor *> &inputs, const MNN::Op *op, Backend *backend)
    : Execution(backend) {
#ifdef LOG_VERBOSE
    MNN_PRINT("Start BatchToSpaceExecution init !\n");
#endif
    mOpenCLBackend = static_cast<OpenCLBackend *>(backend);
    auto param     = op->main_as_SpaceBatch();
    mPaddings[1]   = param->padding()->int32s()->data()[0];
    mPaddings[0]   = param->padding()->int32s()->data()[1];
    mBlockShape[1] = param->blockShape()->int32s()->data()[0];
    mBlockShape[0] = param->blockShape()->int32s()->data()[1];
    std::set<std::string> buildOptions;
    std::string kernelName = "batch_to_space";
    auto runtime           = mOpenCLBackend->getOpenCLRuntime();
    mKernel                = runtime->buildKernel("batch_to_space", kernelName, buildOptions);

#ifdef LOG_VERBOSE
    MNN_PRINT("end BatchToSpaceExecution init !\n");
#endif
}

ErrorCode BatchToSpaceExecution::onResize(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) {
#ifdef LOG_VERBOSE
    MNN_PRINT("Start BatchToSpaceExecution onResize !\n");
#endif

#ifdef LOG_VERBOSE
    MNN_PRINT("end BatchToSpaceExecution onResize !\n");
#endif
    return NO_ERROR;
}

ErrorCode BatchToSpaceExecution::onExecute(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) {
#ifdef LOG_VERBOSE
    MNN_PRINT("Start BatchToSpaceExecution onExecute !\n");
#endif
    auto input  = outputs[0];
    auto output = inputs[0];

    int inputSize[4]  = {input->width(), input->height(), UP_DIV(input->channel(), 4), input->batch()};
    int outputSize[4] = {output->width(), output->height(), UP_DIV(output->channel(), 4), output->batch()};

    mKernel.setArg(0, openCLImage(input));
    mKernel.setArg(1, openCLImage(output));
    mKernel.setArg(2, sizeof(inputSize), inputSize);
    mKernel.setArg(3, sizeof(outputSize), outputSize);
    mKernel.setArg(4, sizeof(mPaddings), mPaddings);
    mKernel.setArg(5, sizeof(mBlockShape), mBlockShape);

    auto runtime = mOpenCLBackend->getOpenCLRuntime();

    runtime->commandQueue().enqueueNDRangeKernel(
        mKernel, cl::NullRange,
        cl::NDRange(UP_DIV(outputSize[0], 16) * 16, UP_DIV(outputSize[1], 16) * 16, outputSize[2] * outputSize[3]),
        cl::NDRange(16, 16, 1));

#ifdef LOG_VERBOSE
    MNN_PRINT("end BatchToSpaceExecution onExecute !\n");
#endif
    return NO_ERROR;
}

OpenCLCreatorRegister<TypedCreator<BatchToSpaceExecution>> __batch_to_space_op(OpType_BatchToSpaceND);

} // namespace OpenCL
} // namespace MNN
