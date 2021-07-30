//
//  CPUBinary.hpp
//  MNN
//
//  Created by MNN on 2018/08/02.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#ifndef CPUBinary_hpp
#define CPUBinary_hpp

#include "Execution.hpp"

namespace MNN {

template <typename T>
class CPUBinary : public Execution {
public:
    CPUBinary(Backend *b, int32_t type);
    virtual ~CPUBinary() = default;
    virtual ErrorCode onResize(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) override;
    virtual ErrorCode onExecute(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) override;

protected:
    int32_t mType;
};
} // namespace MNN
#endif /* CPUBinary_hpp */
