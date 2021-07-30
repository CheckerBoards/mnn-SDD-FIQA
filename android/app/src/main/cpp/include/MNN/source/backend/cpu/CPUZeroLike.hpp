//
//  CPUZeroLike.hpp
//  MNN
//
//  Created by MNN on 2019/5/22.
//  Copyright © 2018 Alibaba. All rights reserved.
//

#ifndef CPUZeroLike_hpp
#define CPUZeroLike_hpp

#include "CPUBackend.hpp"
namespace MNN {
class CPUZeroLike : public Execution {
public:
    CPUZeroLike(Backend *bn) : Execution(bn) {
        // Do nothing
    }
    virtual ErrorCode onExecute(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) override;
};
}; // namespace MNN

#endif /* CPUZeroLike_hpp */
