/**
 * File Description
 * @author CheckerBoard
 * @date 29-07-2021
 * @description
 */
#ifndef DMS_MNN_FACEIMAGEQUALITY_H
#define DMS_MNN_FACEIMAGEQUALITY_H
#include <vector>
#include <memory>

#include "include/opencv2/opencv.hpp"

#include "include/MNN/Interpreter.hpp"
#include "include/MNN/ImageProcess.hpp"
#include "include/MNN/MNNDefine.h"
#include "include/MNN/Tensor.hpp"
#include "math.h"
#include <algorithm>

class faceImageQuality {
public:
    faceImageQuality(std::string &model_path);
    ~faceImageQuality();
    float estimateQuality(const cv::Mat& image);

private:
    int initial(const std::string &model_path);
    std::string model_file_ = "sdd_fiqa.mnn";
    bool initialized_ = false;
    std::shared_ptr<MNN::CV::ImageProcess> pretreat_ = nullptr;
    std::shared_ptr<MNN::Interpreter> fiqa_interpreter_ = nullptr;
    MNN::Session* fiqa_sess_ = nullptr;
    MNN::Tensor* input_tensor_ = nullptr;
    const cv::Size inputSize_ = cv::Size(112, 112);
    const float meanVals_[3] = {127.5f, 127.5f, 127.5f };
    const float normVals_[3] = {0.007843137f, 0.007843137f, 0.007843137f};
};


#endif //DMS_MNN_FACEIMAGEQUALITY_H
