/*
 *  Copyright (C) 2018 Rui Pimentel de Figueiredo
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *      
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*!    
    \author Rui Figueiredo : ruipimentelfigueiredo
*/

#include <fstream>
#include <sstream>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

const char* keys =
    "{ help  h     | | Print help message. }"
    "{ input i     | | Path to input image or video file. Skip this argument to capture frames from a camera.}"
    "{ model m     | | Path to a binary file of model contains trained weights. "
                      "It could be a file with extensions .caffemodel (Caffe), "
                      ".pb (TensorFlow), .t7 or .net (Torch), .weights (Darknet) }"
    "{ config c    | | Path to a text file of model contains network configuration. "
                      "It could be a file with extensions .prototxt (Caffe), .pbtxt (TensorFlow), .cfg (Darknet) }"
    "{ framework f | | Optional name of an origin framework of the model. Detect it automatically if it does not set. }"
    "{ classes     | | Optional path to a text file with names of classes to label detected objects. }"
    "{ mean        | | Preprocess input image by subtracting mean values. Mean values should be in BGR order and delimited by spaces. }"
    "{ scale       |  1 | Preprocess input image by multiplying on a scale factor. }"
    "{ width       | -1 | Preprocess input image by resizing to a specific width. }"
    "{ height      | -1 | Preprocess input image by resizing to a specific height. }"
    "{ rgb         |    | Indicate that model works with RGB input images instead BGR ones. }"
    "{ thr         | .5 | Confidence threshold. }"
    "{ backend     |  0 | Choose one of computation backends: "
                         "0: default C++ backend, "
                         "1: Halide language (http://halide-lang.org/), "
                         "2: Intel's Deep Learning Inference Engine (https://software.seek.intel.com/deep-learning-deployment)}"
    "{ target      |  0 | Choose one of target computation devices: "
                         "0: CPU target (by default),"
                         "1: OpenCL }";

using namespace cv;
using namespace dnn;

float confThreshold;
std::vector<std::string> classes;


void callback(int pos, void*)
{
    confThreshold = pos * 0.01f;
}




class ObjectDetection {
    Net net;
    bool swapRB;
    int inpWidth;
    int inpHeight;


	void drawPred(int classId, float conf, int left, int top, int right, int bottom, Mat& frame)
	{
	    rectangle(frame, Point(left, top), Point(right, bottom), Scalar(0, 255, 0));

	    std::string label = format("%.2f", conf);
	    if (!classes.empty())
	    {
		CV_Assert(classId < (int)classes.size());
		label = classes[classId] + ": " + label;
	    }

	    int baseLine;
	    Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

	    top = max(top, labelSize.height);
	    rectangle(frame, Point(left, top - labelSize.height),
		      Point(left + labelSize.width, top + baseLine), Scalar::all(255), FILLED);
	    putText(frame, label, Point(left, top), FONT_HERSHEY_SIMPLEX, 0.5, Scalar());
	}


    void postprocess(Mat& frame, const Mat& out, Net& net)
    {
        static std::vector<int> outLayers = net.getUnconnectedOutLayers();
        static std::string outLayerType = net.getLayer(outLayers[0])->type;

        float* data = (float*)out.data;
        if (net.getLayer(0)->outputNameToIndex("im_info") != -1)  // Faster-RCNN or R-FCN
        {
            // Network produces output blob with a shape 1x1xNx7 where N is a number of
            // detections and an every detection is a vector of values
            // [batchId, classId, confidence, left, top, right, bottom]
            for (size_t i = 0; i < out.total(); i += 7)
            {
                float confidence = data[i + 2];
                if (confidence > confThreshold)
                {
                    int left = (int)data[i + 3];
                    int top = (int)data[i + 4];
                    int right = (int)data[i + 5];
                    int bottom = (int)data[i + 6];
                    int classId = (int)(data[i + 1]) - 1;  // Skip 0th background class id.
                    drawPred(classId, confidence, left, top, right, bottom, frame);
                }
            }
        }
        else if (outLayerType == "DetectionOutput")
        {
            // Network produces output blob with a shape 1x1xNx7 where N is a number of
            // detections and an every detection is a vector of values
            // [batchId, classId, confidence, left, top, right, bottom]
            for (size_t i = 0; i < out.total(); i += 7)
            {
                float confidence = data[i + 2];
                if (confidence > confThreshold)
                {
                    int left = (int)(data[i + 3] * frame.cols);
                    int top = (int)(data[i + 4] * frame.rows);
                    int right = (int)(data[i + 5] * frame.cols);
                    int bottom = (int)(data[i + 6] * frame.rows);
                    int classId = (int)(data[i + 1]) - 1;  // Skip 0th background class id.
                    drawPred(classId, confidence, left, top, right, bottom, frame);
                }
            }
        }
        else if (outLayerType == "Region")
        {
            // Network produces output blob with a shape NxC where N is a number of
            // detected objects and C is a number of classes + 4 where the first 4
            // numbers are [center_x, center_y, width, height]
            for (int i = 0; i < out.rows; ++i, data += out.cols)
            {
                Mat confidences = out.row(i).colRange(5, out.cols);
                Point classIdPoint;
                double confidence;
                minMaxLoc(confidences, 0, &confidence, 0, &classIdPoint);
                if (confidence > confThreshold)
                {
                    int classId = classIdPoint.x;
                    int centerX = (int)(data[0] * frame.cols);
                    int centerY = (int)(data[1] * frame.rows);
                    int width = (int)(data[2] * frame.cols);
                    int height = (int)(data[3] * frame.rows);
                    int left = centerX - width / 2;
                    int top = centerY - height / 2;
                    drawPred(classId, (float)confidence, left, top, left + width, top + height, frame);
                }
            }
        }
        else
            CV_Error(Error::StsNotImplemented, "Unknown output layer type: " + outLayerType);
    }

public:
    ObjectDetection(const std::string & model, const int & backend, const int & target, const bool & swapRB_, const int inpWidth_, const int inpHeight_) :
        net(dnn::readNetFromTensorflow(model)),
        swapRB(swapRB_),
        inpWidth(inpWidth_),
        inpHeight(inpHeight_)
    {
        // Load a model.
        net.setPreferableBackend(backend);
        net.setPreferableTarget(target);

        // Create a window
        static const std::string kWinName = "Deep learning object detection in OpenCV";
    	namedWindow(kWinName, WINDOW_NORMAL);
    	int initialConf = (int)(confThreshold * 100);
    	createTrackbar("Confidence threshold, %", kWinName, &initialConf, 99, callback, this);
    }



    cv::Mat detect(cv::Mat & frame, const Scalar & mean, const double & scale)
    {
        // Create a 4D blob from a frame.
        Size inpSize(inpWidth > 0 ? inpWidth : frame.cols, inpHeight > 0 ? inpHeight : frame.rows);
        cv::Mat blob=blobFromImage(frame, scale, inpSize, mean, swapRB);
                     //blobFromImage(InputArray image, double scalefactor=1.0, const Size& size = Size(),
                     //                      const Scalar& mean = Scalar(), bool swapRB=true, bool crop=true);
        // Run a model.
        net.setInput(blob);
        if (net.getLayer(0)->outputNameToIndex("im_info") != -1)  // Faster-RCNN or R-FCN
        {
            resize(frame, frame, inpSize);
            Mat imInfo = (Mat_<float>(1, 3) << inpSize.height, inpSize.width, 1.6f);
            net.setInput(imInfo, "im_info");
        }
        Mat out = net.forward();

        postprocess(frame, out, net);

        // Put efficiency information.
        std::vector<double> layersTimes;
        //double freq = getTickFrequency() / 1000;
        //double t = net.getPerfProfile(layersTimes) / freq;
        //std::string label = format("Inference time: %.2f ms", t);
        //putText(frame, label, Point(0, 15), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 255, 0));
        static const std::string kWinName = "Deep learning object detection in OpenCV";

        imshow(kWinName, frame);
        //return out;
        return out;
    }
};
