

#include "yolov8-obb.h"
#include <iomanip>
#include <sstream>

// 时间戳的设定
std::string getTimeFileName()
{
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
    localtime_r(&now_time_t, &tm_buf);

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S")
        << "_"
        << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

YOLOv8_obb::YOLOv8_obb(const std::string& engine_file_path)
    : yolo_trt::Runtime<obb::Binding>(engine_file_path)
{
}

void YOLOv8_obb::letterbox(const cv::Mat& image, cv::Mat& out, cv::Size& size)
{
    const float inp_h  = size.height;
    const float inp_w  = size.width;
    float       height = image.rows;
    float       width  = image.cols;

    float r    = std::min(inp_h / height, inp_w / width);
    int   padw = std::round(width * r);
    int   padh = std::round(height * r);

    cv::Mat tmp;
    if ((int)width != padw || (int)height != padh) {
        cv::resize(image, tmp, cv::Size(padw, padh));
    }
    else {
        tmp = image.clone();
    }

    float dw = inp_w - padw;
    float dh = inp_h - padh;

    dw /= 2.0f;
    dh /= 2.0f;
    int top    = int(std::round(dh - 0.1f));
    int bottom = int(std::round(dh + 0.1f));
    int left   = int(std::round(dw - 0.1f));
    int right  = int(std::round(dw + 0.1f));

    cv::copyMakeBorder(tmp, tmp, top, bottom, left, right, cv::BORDER_CONSTANT, {114, 114, 114});

    cv::dnn::blobFromImage(tmp, out, 1 / 255.f, cv::Size(), cv::Scalar(0, 0, 0), true, false, CV_32F);
    this->pparam.ratio  = 1 / r;
    this->pparam.dw     = dw;
    this->pparam.dh     = dh;
    this->pparam.height = height;
    this->pparam.width  = width;
    ;
}

void YOLOv8_obb::copy_from_Mat(const cv::Mat& image)
{
    const auto& dims = this->input_bindings[0].dims;
    cv::Size size{static_cast<int>(dims.d[3]), static_cast<int>(dims.d[2])};
    copy_from_Mat(image, size);
}

void YOLOv8_obb::copy_from_Mat(const cv::Mat& image, cv::Size& size)
{
    if (image.empty() || image.type() != CV_8UC3 || size.width <= 0 || size.height <= 0) {
        throw std::runtime_error("Expected a nonempty BGR8 image and positive input size");
    }
    this->set_input_shape(nvinfer1::Dims4{1, 3, size.height, size.width});
    cv::Mat nchw;
    this->letterbox(image, nchw, size);
    this->copy_input(nchw.ptr<float>(), nchw.total() * nchw.elemSize());
}

void YOLOv8_obb::postprocess(std::vector<obb::Object>& objs, float score_thres, float iou_thres, int topk, int num_labels)
{
    this->validate_output(num_labels, 5);
    objs.clear();
    int num_channels = static_cast<int>(this->output_bindings[0].dims.d[1]);
    int num_anchors = static_cast<int>(this->output_bindings[0].dims.d[2]);

    auto& dw     = this->pparam.dw;
    auto& dh     = this->pparam.dh;
    auto& width  = this->pparam.width;
    auto& height = this->pparam.height;
    auto& ratio  = this->pparam.ratio;

    std::vector<cv::RotatedRect> bboxes;
    std::vector<float>           scores;
    std::vector<int>             labels;
    std::vector<int>             indices;

    cv::Mat output = cv::Mat(num_channels, num_anchors, CV_32F, static_cast<float*>(this->host_ptrs[0]));
    output         = output.t();
    for (int i = 0; i < num_anchors; i++) {
        auto row_ptr    = output.row(i).ptr<float>();
        auto bboxes_ptr = row_ptr;
        auto scores_ptr = row_ptr + 4;
        auto max_s_ptr  = std::max_element(scores_ptr, scores_ptr + num_labels);
        auto angle_ptr  = row_ptr + 4 + num_labels;

        float score = *max_s_ptr;
        if (score > score_thres) {
            float x = (*bboxes_ptr++ - dw) * ratio;
            float y = (*bboxes_ptr++ - dh) * ratio;
            float w = (*bboxes_ptr++) * ratio;
            float h = (*bboxes_ptr) * ratio;

            if (w < 1.f || h < 1.f) {
                continue;
            }

            x = clamp(x, 0.f, width);
            y = clamp(y, 0.f, height);
            w = clamp(w, 0.f, width);
            h = clamp(h, 0.f, height);

            float angle = *angle_ptr / CV_PI * 180.f;

            cv::RotatedRect bbox;
            bbox.center.x    = x;
            bbox.center.y    = y;
            bbox.size.width  = w;
            bbox.size.height = h;
            bbox.angle       = angle;

            bboxes.push_back(bbox);
            labels.push_back(std::distance(scores_ptr, max_s_ptr));
            scores.push_back(score);
        }
    }

    cv::dnn::NMSBoxes(bboxes, scores, score_thres, iou_thres, indices);

    int cnt = 0;
    for (auto& i : indices) {
        if (cnt >= topk) {
            break;
        }
        obb::Object obj;
        obj.rect  = bboxes[i];
        obj.prob  = scores[i];
        obj.label = labels[i];
        objs.push_back(obj);
        cnt += 1;
    }
}



void YOLOv8_obb::draw_objects(const cv::Mat& image,
                              cv::Mat& res,
                              const std::vector<obb::Object>& objs,
                              const std::vector<std::string>& CLASS_NAMES,
                              const std::vector<std::vector<unsigned int>>& COLORS,
                              std::vector<std::vector<float>>& result_array)
{
    res = image.clone();

    result_array.clear();

    // 记录当前这一帧所有目标中心点
    std::vector<cv::Point> current_centers;

    for (const auto& obj : objs) {
        cv::Mat points;
        cv::boxPoints(obj.rect, points);

        cv::Scalar color = cv::Scalar(
            COLORS[obj.label][0],
            COLORS[obj.label][1],
            COLORS[obj.label][2]
        );

        points.convertTo(points, CV_32S);
        cv::polylines(res, points, true, color, 2);

        int x = static_cast<int>(obj.rect.center.x);
        int y = static_cast<int>(obj.rect.center.y);

        current_centers.push_back(cv::Point(x, y));
        cv::Point2f box_points[4];
        // cv::boxPoints(obj.rect, box_points);
        obj.rect.points(box_points);
        //以最上面的边为基准

        result_array.push_back({
            static_cast<float>(obj.label),
            static_cast<float>(x),
            static_cast<float>(y),
        });

        char text[256];
        sprintf(text, "%s %.1f%%", CLASS_NAMES[obj.label].c_str(), obj.prob * 100);

        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(
            text,
            cv::FONT_HERSHEY_SIMPLEX,
            0.4,
            1,
            &baseLine
        );
        
        int text_x = x;
        int text_y = y + 1;

        if (text_y > res.rows) {
            text_y = res.rows;
        }

        cv::rectangle(
            res,
            cv::Rect(text_x, text_y, label_size.width, label_size.height + baseLine),
            {0, 0, 255},
            -1
        );

        cv::putText(
            res,
            text,
            cv::Point(text_x, text_y + label_size.height),
            cv::FONT_HERSHEY_SIMPLEX,
            0.4,
            {255, 255, 255},
            1
        );
    }

    // 没检测到目标，不保存
    if (objs.empty()) {
        return;
    }

    // 静态变量：保存上一次存图时的目标中心点和时间
    static bool has_saved = false;
    static std::vector<cv::Point> last_saved_centers;
    static std::chrono::steady_clock::time_point last_save_time;

    const int pixel_threshold = 10;
    const auto min_save_interval = std::chrono::seconds(1);
    auto now = std::chrono::steady_clock::now();
    bool position_changed = false;

    // 第一次检测到目标，允许保存
    if (!has_saved) {
        position_changed = true;
    }
    // 目标数量变化，也认为需要保存
    else if (current_centers.size() != last_saved_centers.size()) {
        position_changed = true;
    }
    else {
        for (size_t i = 0; i < current_centers.size(); ++i) {
            int dx = std::abs(current_centers[i].x - last_saved_centers[i].x);
            int dy = std::abs(current_centers[i].y - last_saved_centers[i].y);

            if (dx > pixel_threshold || dy > pixel_threshold) {
                position_changed = true;
                break;
            }
        }
    }

    bool time_ok = !has_saved || (now - last_save_time >= min_save_interval);

    // 同时满足：位置变化超过 10 像素，并且距离上次保存至少 1 秒
    if (position_changed && time_ok) 
    {
        std::string file_name = getTimeFileName() + ".jpg";

        bool ok = cv::imwrite(file_name, res);

        if (!ok) {
            std::cerr << "Failed to save image: " << file_name << std::endl;
        } else {
            std::cout << "Saved detect image: " << file_name << std::endl;
        }

        // 只有真正保存成功后，才更新上一次保存状态
        if (ok) {
            has_saved = true;
            last_saved_centers = current_centers;
            last_save_time = now;
        }
    }
}

