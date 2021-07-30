//
//  Schedule.cpp
//  MNN
//
//  Created by MNN on 2018/07/30.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#include "Schedule.hpp"
#include <algorithm>
#include <iterator>
#include <set>
#include <unordered_map>
#include "DirectedAcyclicGraph.hpp"
#include "Macro.h"
#include "TensorUtils.hpp"
//#define MNN_OPEN_TIME_TRACE
#include "AutoTime.hpp"

namespace MNN {

class OpNodeDef : public NodeDef<Op*> {
public:
    OpNodeDef(Op* op) {
        this->op = op;
    }

public:
    virtual shared_ptr<Node<Op*>> makeNode() override {
        shared_ptr<Node<Op*>> ptr = make_shared<Node<Op*>>();
        ptr->setData(this->op);
        return ptr;
    }

private:
    Op* op;
};

static MNNForwardType _getApprociateType(const ScheduleConfig& config) {
    MNNForwardType type = config.type;
    if (MNN_FORWARD_AUTO == config.type) {
        // Search Backend Exclude MNN_FORWARD_CPU
        for (int i = 1; i < MNN_FORWARD_ALL; ++i) {
            if (MNNGetExtraBackendCreator((MNNForwardType)i) != nullptr) {
                type = (MNNForwardType)i;
                break;
            }
        }
    }
    auto creator = MNNGetExtraBackendCreator(type);
    if (nullptr == creator) {
        MNN_PRINT("Can't Find type=%d backend, use %d instead\n", type, config.backupType);
        type = config.backupType;
    }
    return type;
}

static void _setUpTensorInfo(std::vector<std::shared_ptr<Tensor>>& allTensors, const Net* net) {
    auto& tensors = allTensors;
    tensors.resize(net->tensorName()->size());
    for (int i = 0; i < tensors.size(); ++i) {
        tensors[i].reset(new Tensor(4)); // NCHW, TODO
        tensors[i]->setType(DataType_DT_FLOAT);
    }
    auto sourceType = net->sourceType();
    if (sourceType == NetSource_CAFFE) {
        // Caffe Data Type
        for (auto& t : tensors) {
            auto& second                                            = t;
            TensorUtils::getDescribe(second.get())->dimensionFormat = MNN_DATA_FORMAT_NC4HW4;
            second->buffer().dim[1].flags                           = Tensor::REORDER_4;
        }
    } else {
        // Set ExtraTensorDescribe come from tensorflow model
        auto extraTensorDescribe = net->extraTensorDescribe();
        if (nullptr != extraTensorDescribe) {
            for (int i = 0; i < extraTensorDescribe->size(); ++i) {
                auto tInfo = extraTensorDescribe->GetAs<TensorDescribe>(i);
                if (nullptr != tInfo->blob()) {
                    auto tensor = tensors[tInfo->index()].get();
                    tensor->setType(tInfo->blob()->dataType());
                    TensorUtils::getDescribe(tensor)->dimensionFormat = tInfo->blob()->dataFormat();
                }
            }
        }
    }
    // Set Input Tensor, if the type of input is not the same with ExtraTensorDescribe, use input parameter
    for (int opIndex = 0; opIndex < net->oplists()->size(); ++opIndex) {
        auto op = net->oplists()->GetAs<Op>(opIndex);
        if (OpType_Input == op->type()) {
            MNN_ASSERT(nullptr != op->outputIndexes());
            auto index      = op->outputIndexes()->data()[0];
            auto tensor     = tensors[index].get();
            auto& tb        = tensor->buffer();
            auto inputParam = op->main_as_Input();
            if (auto idims = inputParam->dims()) {
                for (int i = 0; i < idims->size(); ++i) {
                    tb.dim[i].min = 0;
                    int extent    = idims->data()[i];
                    // dim-0 is batch(when input batch is -1, set it to be 1, ignore other dim)
                    if (i == 0 && extent == -1) {
                        extent = 1;
                    }
                    tb.dim[i].extent = extent;
                }
                tb.dimensions = idims->size();
            } else {
                tb.dimensions = 0;
            }
            tensor->setType(inputParam->dtype());
            TensorUtils::getDescribe(tensor)->dimensionFormat = inputParam->dformat();
        }
    }
    for (auto& t : tensors) {
        if (MNN_DATA_FORMAT_NC4HW4 == TensorUtils::getDescribe(t.get())->dimensionFormat) {
            t->buffer().dim[1].flags = Tensor::REORDER_4;
        }
    }
}

static int _findOpPosition(const std::string& opName, const Net* net) {
    for (int i = 0; i < net->oplists()->size(); ++i) {
        auto op = net->oplists()->GetAs<Op>(i);
        if (opName == op->name()->str()) {
            return i;
        }
    }
    return -1;
}

static bool _validateOp(const Op* op) {
    if (nullptr == op->inputIndexes() && nullptr == op->outputIndexes()) {
        return false;
    }
    if (nullptr == op->name()) {
        return false;
    }
    return true;
}

static vector<Op*> generateOneSchedulePath(const Net* net, const int begin, const int end,
                                           const vector<shared_ptr<Tensor>>& allTensors) {
    vector<Op*> oplists;
    for (int i = begin; i < end; ++i) {
        auto op = net->oplists()->GetAs<Op>(i);
        if (op->type() == OpType_Input || !_validateOp(op)) {
            continue;
        }
        oplists.emplace_back(const_cast<Op*>(op));
    }
    return oplists;
}

static vector<vector<Op*>> generateSchedulePath(const Net* net, const ScheduleConfig& configs,
                                                const vector<shared_ptr<Tensor>>& allTensors) {
    AUTOTIME;
    vector<vector<Op*>> oplists;
    vector<string> inputs(configs.path.inputs);
    vector<string> outputs(configs.path.outputs);
    auto maxSize = std::max(inputs.size(), outputs.size());
    inputs.resize(maxSize);
    outputs.resize(maxSize);

    for (int i = 0; i < inputs.size(); i++) {
        string in  = inputs[i];
        string out = outputs[i];
        int start  = 0;
        int end    = net->oplists()->size();
        if (in.length() > 0) {
            auto pos = _findOpPosition(in, net);
            if (-1 == pos) {
                MNN_PRINT("Can't find %s op as start op\n", in.c_str());
            } else {
                start = pos;
            }
        }
        if (out.length() > 0) {
            auto pos = _findOpPosition(out, net);
            if (-1 == pos) {
                MNN_PRINT("Can't find %s op as end op\n", out.c_str());
            } else {
                end = pos + 1;
            }
        }
        if (start > end) {
            MNN_PRINT("op order incorrect end op '%s' before begin op '%s',please check!\n", out.c_str(), in.c_str());
        } else {
            vector<Op*> path = generateOneSchedulePath(net, start, end, allTensors);
            oplists.emplace_back(path);
        }
    }

    return oplists;
}

static void generateScheduleGraph(vector<const Op*>& ops, const Net* net, const ScheduleConfig& configs,
                                  const vector<shared_ptr<Tensor>>& allTensors) {
    AUTOTIME;

    if (configs.path.inputs.empty() && configs.path.outputs.empty()) {
        // Use Default Linear schedule
        ops.clear();
        ops.reserve(net->oplists()->size());
        for (int i = 0; i < net->oplists()->size(); ++i) {
            auto op = net->oplists()->GetAs<Op>(i);
            if (op->type() != OpType_Input) {
                ops.emplace_back(op);
            }
        }
        return;
    }
    vector<vector<Op*>> paths = generateSchedulePath(net, configs, allTensors);

    unique_ptr<DirectedAcyclicGraph<Op*>> graph(new DirectedAcyclicGraph<Op*>());

    // add Node
    unordered_map<Op*, shared_ptr<Node<Op*>>> opMaps;
    for (vector<Op*> path : paths) {
        for (Op* op : path) {
            if (opMaps.find(op) == opMaps.end()) {
                OpNodeDef def(op);
                shared_ptr<Node<Op*>> n = graph->AddNode(def);
                opMaps.insert(make_pair(op, n));
            }
        }
    }

    // add edges
    for (vector<Op*> path : paths) {
        shared_ptr<Node<Op*>> pre = nullptr;
        for (Op* op : path) {
            shared_ptr<Node<Op*>> n = opMaps[op];
            if (nullptr == pre) {
                pre = n;
            } else {
                graph->AddEdge(pre, n);
                pre = n;
            }
        }
    }
    ops.clear();
    vector<shared_ptr<Node<Op*>>> order;
    if (graph->GetPostOrder(order)) {
        for (shared_ptr<Node<Op*>> n : order) {
            ops.emplace_back(n->getData());
        }
    } else {
        MNN_PRINT("op graph have cycle,schedule failed\n");
    }
}

static vector<Schedule::PipelineInfo> _scheduleUnit(const Net* net, const ScheduleConfig& configs,
                                                    const vector<shared_ptr<Tensor>>& allTensors) {
    vector<Schedule::PipelineInfo> oplists;
    vector<const Op*> ops;
    generateScheduleGraph(ops, net, configs, allTensors);
    for (const Op* op : ops) {
        Schedule::PipelineInfo opInfo;
        opInfo.op = op;
        if (nullptr != op->outputIndexes()) {
            auto data = op->outputIndexes()->data();
            for (int j = 0; j < op->outputIndexes()->size(); ++j) {
                opInfo.outputs.push_back(allTensors[data[j]].get());
            }
        }
        if (nullptr != op->inputIndexes()) {
            auto data = op->inputIndexes()->data();
            for (int j = 0; j < op->inputIndexes()->size(); ++j) {
                opInfo.inputs.push_back(allTensors[data[j]].get());
            }
        }
        oplists.emplace_back(opInfo);
    }

    return oplists;
}

Schedule::ScheduleInfo Schedule::schedule(const Net* net, const std::vector<ScheduleConfig>& configs) {
    std::vector<std::shared_ptr<Tensor>> allTensors;
    AUTOTIME;

    _setUpTensorInfo(allTensors, net);

    ScheduleInfo schedule;
    if (nullptr == net->oplists()) {
        MNN_PRINT("Error net for schedule\n");
        return schedule;
    }

    std::vector<std::pair<Backend::Info, std::vector<PipelineInfo>>> result;

    for (auto& config : configs) {
        Backend::Info compute;
        compute.type      = _getApprociateType(config);
        compute.numThread = config.numThread;
        compute.user      = config.backendConfig;
        auto oplists      = _scheduleUnit(net, config, allTensors);
        result.emplace_back(std::make_pair(compute, std::move(oplists)));
    }

    schedule.pipelineInfo = std::move(result);

    // get all used op's output, drop unused op, won't change op order. always insert all Input Ops
    std::set<const Op*> oplists;
    {
        AUTOTIME;
        for (std::pair<Backend::Info, vector<PipelineInfo>>& pipeline : schedule.pipelineInfo) {
            for (auto& info : pipeline.second) {
                oplists.insert(info.op);
            }
        }
    }

    std::set<int> outputIndexes;
    std::set<int> inputIndexes;
    for (auto op : oplists) {
        if (nullptr != op->outputIndexes()) {
            auto data = op->outputIndexes()->data();
            for (int j = 0; j < op->outputIndexes()->size(); ++j) {
                outputIndexes.insert(data[j]);
            }
        }
        if (nullptr != op->inputIndexes()) {
            auto data = op->inputIndexes()->data();
            for (int j = 0; j < op->inputIndexes()->size(); ++j) {
                inputIndexes.insert(data[j]);
            }
        }
        MNN_ASSERT(OpType_Input != op->type());
    }

    // Get All Output and Input
    std::set<int> inputIndexDiff;
    std::set<int> outputIndexesDiff;
    std::set_difference(outputIndexes.begin(), outputIndexes.end(), inputIndexes.begin(), inputIndexes.end(),
                        std::inserter(outputIndexesDiff, outputIndexesDiff.begin()));
    std::set_difference(inputIndexes.begin(), inputIndexes.end(), outputIndexes.begin(), outputIndexes.end(),
                        std::inserter(inputIndexDiff, inputIndexDiff.begin()));

    std::unordered_map<std::string, int> tensorNameIndexMap;
    for (int i = 0; i < net->tensorName()->size(); ++i) {
        tensorNameIndexMap[net->tensorName()->Get(i)->str()] = i;
    }
    for (auto& config : configs) {
        for (const auto& name : config.saveTensors) {
            if (tensorNameIndexMap.count(name)) {
                outputIndexesDiff.insert(tensorNameIndexMap[name]);
            } else {
                MNN_PRINT("Bad outputname: %s\n", name.c_str());
            }
        }
    }
    if (net->outputName()) {
        for (int i = 0; i < net->outputName()->size(); ++i) {
            std::string name = net->outputName()->Get(i)->str();
            if (tensorNameIndexMap.count(name)) {
                outputIndexesDiff.insert(tensorNameIndexMap[name]);
            }
        }
    }
    for (auto index : inputIndexDiff) {
        schedule.inputTensors.insert(
            std::make_pair(net->tensorName()->GetAsString(index)->c_str(), allTensors[index].get()));
    }
    for (auto index : outputIndexesDiff) {
        schedule.outputTensor.insert(
            std::make_pair(net->tensorName()->GetAsString(index)->c_str(), allTensors[index].get()));
    }

    for (auto& t : allTensors) {
        schedule.allTensors.emplace_back(std::make_pair(0, std::move(t)));
    }

    for (int i = 0; i < net->oplists()->size(); ++i) {
        auto op = net->oplists()->GetAs<Op>(i);
        if (nullptr != op->inputIndexes()) {
            auto data = op->inputIndexes()->data();
            for (int j = 0; j < op->inputIndexes()->size(); ++j) {
                auto index = data[j];
                schedule.allTensors[index].first += 1;
            }
        }
    }
    for (auto outputIndex : outputIndexesDiff) {
        schedule.allTensors[outputIndex].first += 1;
    }
    schedule.library = net->gpulibrary();
    return schedule;
}
} // namespace MNN
