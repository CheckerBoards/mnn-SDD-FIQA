/**
 * File Description
 * @author CheckerBoard
 * @date 29-07-2021
 * @description
 */
#include "faceImageQuality.h"
faceImageQuality::faceImageQuality(std::string &model_path) {
    initialized_ = false;
    initial(model_path);
}

faceImageQuality::~faceImageQuality() {
    fiqa_interpreter_->releaseModel();
    fiqa_interpreter_->releaseSession(fiqa_sess_);
}

int faceImageQuality::initial(const std::string &model_path) {
    std::string model_file = std::string(model_path) + model_file_;
    fiqa_interpreter_ = std::unique_ptr<MNN::Interpreter>(MNN::Interpreter::createFromFile(model_file.c_str()));
    if (nullptr == fiqa_interpreter_) {
        return -1;
    }
    MNN::ScheduleConfig schedule_config;
    schedule_config.type = MNN_FORWARD_CPU;
    schedule_config.numThread = 4;
    MNN::BackendConfig backend_config;
    backend_config.memory = MNN::BackendConfig::Memory_Normal;
    backend_config.power = MNN::BackendConfig::Power_Normal;
    backend_config.precision = MNN::BackendConfig::Precision_High;
    schedule_config.backendConfig = &backend_config;
    fiqa_sess_ = fiqa_interpreter_->createSession(schedule_config);
    input_tensor_ = fiqa_interpreter_->getSessionInput(fiqa_sess_, nullptr);
    fiqa_interpreter_->resizeTensor(input_tensor_, {1, 3, inputSize_.height, inputSize_.width});
    fiqa_interpreter_->resizeSession(fiqa_sess_);
    pretreat_ = std::shared_ptr<MNN::CV::ImageProcess> (MNN::CV::ImageProcess::create(MNN::CV::BGR, MNN::CV::RGB, meanVals_, 3,normVals_, 3));
    initialized_ = true;
    return 0;
}

float faceImageQuality::estimateQuality(const cv::Mat &image) {
    if (image.empty()) {
        return -1.0;
    }
    cv::Mat process_img_face = image.clone();
    cv::Mat img_resized;
    cv::resize(process_img_face, img_resized, inputSize_);
    pretreat_->convert(img_resized.data, inputSize_.width, inputSize_.height, 0, input_tensor_);
    fiqa_interpreter_->runSession(fiqa_sess_);
    std::string output_name = "output";
    auto output_quality = fiqa_interpreter_->getSessionOutput(fiqa_sess_, output_name.c_str());
    MNN::Tensor quality_tensor(output_quality, output_quality->getDimensionType());
    output_quality->copyToHostTensor(&quality_tensor);
    return quality_tensor.host<float>()[0];
}