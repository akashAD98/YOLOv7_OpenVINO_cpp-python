#include <math.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <stdlib.h>
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn/dnn.hpp>
#include <openvino/openvino.hpp>

using namespace std;

struct Object
{
    cv::Rect_<float> rect;
    int label;
    float prob;
};

const std::vector<std::string> class_names = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"};

inline float sigmoid(float x)
{
    return static_cast<float>(1.f / (1.f + exp(-x)));
}

void drawPred(int classId, float conf, cv::Rect box, float ratio_x, float ratio_y, int raw_h, int raw_w, cv::Mat &frame,
              const std::vector<std::string> &classes)
{
    float x0 = box.x;
    float y0 = box.y;
    float x1 = box.x + box.width;
    float y1 = box.y + box.height;

    x0 = x0 * ratio_x;
    y0 = y0 * ratio_y;
    x1 = x1 * ratio_x;
    y1 = y1 * ratio_y;

    x0 = std::max(std::min(x0, (float)(raw_w - 1)), 0.f);
    y0 = std::max(std::min(y0, (float)(raw_h - 1)), 0.f);
    x1 = std::max(std::min(x1, (float)(raw_w - 1)), 0.f);
    y1 = std::max(std::min(y1, (float)(raw_h - 1)), 0.f);

    cv::rectangle(frame, cv::Point(x0, y0), cv::Point(x1, y1), cv::Scalar(0, 255, 0), 1);
    std::string label = cv::format("%.2f", conf);
    if (!classes.empty())
    {
        CV_Assert(classId < (int)classes.size());
        label = classes[classId] + ": " + label;
    }
    int baseLine;
    cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.25, 1, &baseLine);
    y0 = max(int(y0), labelSize.height);
    cv::rectangle(frame, cv::Point(x0, y0 - round(1.5 * labelSize.height)), cv::Point(x0 + round(1.5 * labelSize.width), y0 + baseLine), cv::Scalar(0, 255, 0), cv::FILLED);
    cv::putText(frame, label, cv::Point(x0, y0), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(), 1);
}

static void generate_proposals(int stride, const float *feat, float prob_threshold, std::vector<Object> &objects)
{
    float anchors[18] = {12, 16, 19, 36, 40, 28, 36, 75, 76, 55, 72, 146, 142, 110, 192, 243, 459, 401};
    int anchor_num = 3;
    int feat_w = 640 / stride;
    int feat_h = 640 / stride;
    int cls_num = 80;
    int anchor_group = 0;
    if (stride == 8)
        anchor_group = 0;
    if (stride == 16)
        anchor_group = 1;
    if (stride == 32)
        anchor_group = 2;

    for (int anchor = 0; anchor <= anchor_num - 1; anchor++)
    {
        for (int i = 0; i <= feat_h - 1; i++)
        {
            for (int j = 0; j <= feat_w - 1; j++)
            {
                float box_prob = feat[anchor * feat_h * feat_w * (cls_num + 5) + i * feat_w * (cls_num + 5) + j * (cls_num + 5) + 4];
                box_prob = sigmoid(box_prob);
                if (box_prob < prob_threshold)
                    continue;
                float x = feat[anchor * feat_h * feat_w * (cls_num + 5) + i * feat_w * (cls_num + 5) + j * (cls_num + 5) + 0];
                float y = feat[anchor * feat_h * feat_w * (cls_num + 5) + i * feat_w * (cls_num + 5) + j * (cls_num + 5) + 1];
                float w = feat[anchor * feat_h * feat_w * (cls_num + 5) + i * feat_w * (cls_num + 5) + j * (cls_num + 5) + 2];
                float h = feat[anchor * feat_h * feat_w * (cls_num + 5) + i * feat_w * (cls_num + 5) + j * (cls_num + 5) + 3];

                double max_prob = 0;
                int idx = 0;
                for (int t = 5; t < 85; ++t)
                {
                    double tp = feat[anchor * feat_h * feat_w * (cls_num + 5) + i * feat_w * (cls_num + 5) + j * (cls_num + 5) + t];
                    tp = sigmoid(tp);
                    if (tp > max_prob)
                    {
                        max_prob = tp;
                        idx = t;
                    }
                }
                float cof = box_prob * max_prob;
                if (cof < prob_threshold)
                    continue;

                x = (sigmoid(x) * 2 - 0.5 + j) * stride;
                y = (sigmoid(y) * 2 - 0.5 + i) * stride;
                w = pow(sigmoid(w) * 2, 2) * anchors[anchor_group * 6 + anchor * 2];
                h = pow(sigmoid(h) * 2, 2) * anchors[anchor_group * 6 + anchor * 2 + 1];

                float r_x = x - w / 2;
                float r_y = y - h / 2;
                Object obj;
                obj.rect.x = r_x;
                obj.rect.y = r_y;
                obj.rect.width = w;
                obj.rect.height = h;
                obj.label = idx - 5;
                obj.prob = cof;
                objects.push_back(obj);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int img_h = 640;
    int img_w = 640;
    int img_c = 3;
    int img_size = img_h * img_h * img_c;

    const float prob_threshold = 0.30f;
    const float nms_threshold = 0.60f;

    const std::string model_path{argv[1]};
    const char *image_path{argv[2]};
    const std::string device_name{argv[3]};

    cv::Mat sample = cv::imread(image_path);
    cv::Mat img;
    cv::resize(sample, img, cv::Size(img_w, img_h));
    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);

    // -------- Step 1. Initialize OpenVINO Runtime Core --------
    ov::Core core;

    // -------- Step 2. Read a model --------
    std::shared_ptr<ov::Model> model = core.read_model(model_path);

    // -------- Step 3. Loading a model to the device --------
    ov::CompiledModel compiled_model = core.compile_model(model, device_name);

    // Get input port for model with one input
    auto input_port = compiled_model.input();
    // Create tensor from external memory
    // ov::Tensor input_tensor(input_port.get_element_type(), input_port.get_shape(), input_data.data());
    // -------- Step 4. Create an infer request --------
    ov::InferRequest infer_request = compiled_model.create_infer_request();

    // -------- Step 5. Prepare input --------
    ov::Tensor input_tensor1 = infer_request.get_input_tensor(0);
    // NHWC => NCHW
    auto data1 = input_tensor1.data<float>();
    for (int h = 0; h < img_h; h++)
    {
        for (int w = 0; w < img_w; w++)
        {
            for (int c = 0; c < 3; c++)
            {
                // int in_index = h * img_w * 3 + w * 3 + c;
                int out_index = c * img_h * img_w + h * img_w + w;
                data1[out_index] = float(img.at<cv::Vec3b>(h, w)[c]) / 255.0f;
            }
        }
    }

    // -------- Step 6. Start inference --------
    infer_request.infer();

    // -------- Step 7. Process output
    auto output_tensor_p8 = infer_request.get_output_tensor(0);
    const float *result_p8 = output_tensor_p8.data<const float>();
    auto output_tensor_p16 = infer_request.get_output_tensor(1);
    const float *result_p16 = output_tensor_p16.data<const float>();
    auto output_tensor_p32 = infer_request.get_output_tensor(2);
    const float *result_p32 = output_tensor_p32.data<const float>();

    std::vector<Object> proposals;
    std::vector<Object> objects8;
    std::vector<Object> objects16;
    std::vector<Object> objects32;
    std::vector<Object> objects;

    generate_proposals(8, result_p8, prob_threshold, objects8);
    proposals.insert(proposals.end(), objects8.begin(), objects8.end());
    generate_proposals(16, result_p16, prob_threshold, objects16);
    proposals.insert(proposals.end(), objects16.begin(), objects16.end());
    generate_proposals(32, result_p32, prob_threshold, objects32);
    proposals.insert(proposals.end(), objects32.begin(), objects32.end());

    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    for (size_t i = 0; i < proposals.size(); i++)
    {
        classIds.push_back(proposals[i].label);
        confidences.push_back(proposals[i].prob);
        boxes.push_back(proposals[i].rect);
    }

    std::vector<int> picked;
    cv::dnn::NMSBoxes(boxes, confidences, prob_threshold, nms_threshold, picked);

    int raw_h = sample.rows;
    int raw_w = sample.cols;
    float ratio_x = (float)raw_w / img_w;
    float ratio_y = (float)raw_h / img_h;

    for (size_t i = 0; i < picked.size(); i++)
    {
        int idx = picked[i];
        cv::Rect box = boxes[idx];
        drawPred(classIds[idx], confidences[idx], box, ratio_x, ratio_y, raw_h, raw_w, sample, class_names);
    }
    cv::imwrite("yolov7_out.jpg", sample);
}
