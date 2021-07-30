//
//  BatchMatMulTf.cpp
//  MNNConverter
//
//  Created by MNN on 2019/03/25.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#include "TfUtils.hpp"
#include "tfOpConverter.hpp"

#include "graph.pb.h"

DECLARE_OP_CONVERTER(BatchMatMulTf);

MNN::OpType BatchMatMulTf::opType() {
    return MNN::OpType_BatchMatMul;
}

MNN::OpParameter BatchMatMulTf::type() {
    return MNN::OpParameter_BatchMatMulParam;
}

void BatchMatMulTf::run(MNN::OpT *dstOp, TmpNode *srcNode, TmpGraph *tempGraph) {
    auto batchMatMulParam = new MNN::BatchMatMulParamT;

    DCHECK(2 == srcNode->inEdges.size()) << "BatchMatMul input error!";

    tensorflow::AttrValue value;
    if (find_attr_value(srcNode->tfNode, "adj_x", value)) {
        batchMatMulParam->adjX = value.b();
    }

    if (find_attr_value(srcNode->tfNode, "adj_y", value)) {
        batchMatMulParam->adjY = value.b();
    }

    dstOp->main.value = batchMatMulParam;
}

REGISTER_CONVERTER(BatchMatMulTf, BatchMatMul);
