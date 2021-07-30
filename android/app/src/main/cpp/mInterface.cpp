/**
 * File Description
 * @author CheckerBoard
 * @date 29-07-2021
 * @description
 */
#include "mInterface.h"

mInterface::mInterface(){
    initial();
}

mInterface::~mInterface(){
    if(m_faceImageQuality != nullptr){
        delete m_faceImageQuality;
    }
    m_faceImageQuality = nullptr;
}

void mInterface::initial(){
    std::string model_path = "/data/data/com.demo.faceimagequality/files/assets/resource/model/";
    m_faceImageQuality = new faceImageQuality(model_path);
}

float mInterface::process(const cv::Mat& image){
    float qualityResult = 0.0;
    cv::Mat process_image = image.clone();
    if(!process_image.empty()){
        qualityResult = m_faceImageQuality->estimateQuality(process_image);
    }
    process_image.release();
    return qualityResult;
}