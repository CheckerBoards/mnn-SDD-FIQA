/**
 * File Description
 * @author CheckerBoard
 * @date 29-07-2021
 * @description
 */
#ifndef FACEIMAGEQUALITY_MINTERFACE_H
#define FACEIMAGEQUALITY_MINTERFACE_H

#include "faceImageQuality.h"

class mInterface {
public:
    mInterface();
    ~mInterface();
    float process(const cv::Mat& image);
private:
    void initial();
    faceImageQuality *m_faceImageQuality = nullptr;
};


#endif //FACEIMAGEQUALITY_MINTERFACE_H
