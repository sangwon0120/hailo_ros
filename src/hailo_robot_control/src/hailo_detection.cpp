#include <rclcpp/rclcpp.hpp>
#include <unitree_go2_vision_msgs/msg/detection.hpp>
#include <unitree_go2_vision_msgs/msg/detection_array.hpp>

#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>

#include <iostream>
#include <vector>
#include <map>
#include <string>

using namespace std::chrono_literals;

class HailoDetectionPublisher : public rclcpp::Node
{
public:
    HailoDetectionPublisher() : Node("hailo_detection_publisher")
    {
        // ROS 2 Publisher
        publisher_ = this->create_publisher<unitree_go2_vision_msgs::msg::DetectionArray>("hailo_detections", 10);

        // Open Camera
        cap_.open(0);
        if (!cap_.isOpened()) {
            RCLCPP_ERROR(this->get_logger(), "카메라를 열 수 없습니다. /dev/video0 연결 상태를 확인하세요.");
            throw std::runtime_error("Cannot open camera");
        }
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

        // Map class names
        class_names_ = {
            {0, "car"}, {1, "person"}, {2, "pole"}, {3, "tree"}, {4, "bollard"}
        };

        // Initialize Hailo
        init_hailo();

        // Timer for inference loop
        timer_ = this->create_wall_timer(
            1ms, std::bind(&HailoDetectionPublisher::inference_loop, this));
        
        RCLCPP_INFO(this->get_logger(), "실시간 추론 시작");
    }

    ~HailoDetectionPublisher()
    {
        cap_.release();
        cv::destroyAllWindows();
    }

private:
    void init_hailo()
    {
        std::string hef_path = "hyolov8s.hef";
        
        // VDevice creation (Using default parameters)
        auto vdevice_exp = hailort::VDevice::create();
        if (!vdevice_exp) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create VDevice");
            throw std::runtime_error("Hailo init failed");
        }
        vdevice_ = vdevice_exp.release(); // VDevice::create usually returns Expected<unique_ptr<VDevice>>

        // Create Infer Model
        auto infer_model_exp = vdevice_->create_infer_model(hef_path);
        if (!infer_model_exp) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create InferModel from %s", hef_path.c_str());
            throw std::runtime_error("Hailo init failed");
        }
        infer_model_ = infer_model_exp.release();

        infer_model_->set_batch_size(BATCH_SIZE);
        
        // Output configuration
        auto output_names = infer_model_->get_output_names();
        if (output_names.empty()) throw std::runtime_error("No output names found.");
        output_name_ = output_names[0];

        infer_model_->output(output_name_)->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);

        // Configure Model
        auto configured_exp = infer_model_->configure();
        if (!configured_exp) throw std::runtime_error("Failed to configure model");
        configured_infer_model_ = std::make_unique<hailort::ConfiguredInferModel>(configured_exp.release());

        auto bindings_exp = configured_infer_model_->create_bindings();
        if (!bindings_exp) throw std::runtime_error("Failed to create bindings");
        bindings_ = std::make_unique<hailort::ConfiguredInferModel::Bindings>(bindings_exp.release());

        // Get Input Shape
        auto shape = infer_model_->input()->shape();
        input_h_ = shape.height;
        input_w_ = shape.width;
        input_c_ = shape.features;
        RCLCPP_INFO(this->get_logger(), "Input shape: H=%d, W=%d, C=%d", input_h_, input_w_, input_c_);

        // Allocate Output Buffer
        size_t output_size = infer_model_->output(output_name_)->get_frame_size();
        output_buffer_.resize(output_size);
        bindings_->output(output_name_)->set_buffer(hailort::MemoryView(output_buffer_.data(), output_buffer_.size()));
    }

    cv::Mat letterbox(const cv::Mat& img, cv::Size new_shape, cv::Scalar color)
    {
        cv::Mat out;
        float r = std::min((float)new_shape.width / img.cols, (float)new_shape.height / img.rows);
        int new_unpad_w = std::round(img.cols * r);
        int new_unpad_h = std::round(img.rows * r);
        
        if (img.cols != new_unpad_w || img.rows != new_unpad_h) {
            cv::resize(img, out, cv::Size(new_unpad_w, new_unpad_h), 0, 0, cv::INTER_LINEAR);
        } else {
            out = img.clone();
        }

        float dw = (new_shape.width - new_unpad_w) / 2.0f;
        float dh = (new_shape.height - new_unpad_h) / 2.0f;

        int top = std::round(dh - 0.1f);
        int bottom = std::round(dh + 0.1f);
        int left = std::round(dw - 0.1f);
        int right = std::round(dw + 0.1f);

        cv::copyMakeBorder(out, out, top, bottom, left, right, cv::BORDER_CONSTANT, color);
        return out;
    }

    void inference_loop()
    {
        cv::Mat frame;
        if (!cap_.read(frame)) {
            RCLCPP_WARN(this->get_logger(), "카메라 프레임 읽기 실패");
            return;
        }

        int orig_h = frame.rows;
        int orig_w = frame.cols;

        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

        cv::Mat img_letterbox = letterbox(rgb, cv::Size(input_w_, input_h_), cv::Scalar(114, 114, 114));
        if (!img_letterbox.isContinuous()) {
            img_letterbox = img_letterbox.clone();
        }

        bindings_->input()->set_buffer(hailort::MemoryView(img_letterbox.data, img_letterbox.total() * img_letterbox.elemSize()));

        auto status = configured_infer_model_->wait_for_async_ready(TIMEOUT_MS);
        if (status != HAILO_SUCCESS) return;

        // Sync or Async run. Python wait() immediately after run_async. We can just use wait_for_async_ready and run().
        auto job = configured_infer_model_->run_async({*bindings_});
        if (!job) return;
        job->wait(TIMEOUT_MS);

        // Parse Results using HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS layout
        auto nms_shape = infer_model_->output(output_name_)->get_nms_shape().value();
        uint32_t num_classes = nms_shape.number_of_classes;
        uint32_t max_bboxes_per_class = nms_shape.max_bboxes_per_class;

        float* data = reinterpret_cast<float*>(output_buffer_.data());

        unitree_go2_vision_msgs::msg::DetectionArray msg;
        msg.header.stamp = this->now();
        msg.header.frame_id = "camera_link";

        for (uint32_t c = 0; c < num_classes; ++c) {
            float* class_data = data + c * (1 + max_bboxes_per_class * 5); // 1 count + Max boxes * 5 attributes
            int num_detections = static_cast<int>(class_data[0]);

            for (int i = 0; i < num_detections; ++i) {
                float* box = &class_data[1 + (i * 5)];
                float y_min = box[0];
                float x_min = box[1];
                float y_max = box[2];
                float x_max = box[3];
                float score = box[4];
                int class_id = c; // Class ID is implicit by array index

                if (score < CONF_THRES) continue;

                // Scale to original (Exactly matching Python logic)
                x_min *= orig_w;
                x_max *= orig_w;
                y_min *= orig_h;
                y_max *= orig_h;

                x_min = std::clamp(x_min, 0.0f, (float)(orig_w - 1));
                x_max = std::clamp(x_max, 0.0f, (float)(orig_w - 1));
                y_min = std::clamp(y_min, 0.0f, (float)(orig_h - 1));
                y_max = std::clamp(y_max, 0.0f, (float)(orig_h - 1));

                unitree_go2_vision_msgs::msg::Detection d;
                d.class_id = class_id;
                d.class_name = class_names_.count(class_id) ? class_names_[class_id] : std::to_string(class_id);
                d.score = score;
                d.x_min = x_min;
                d.y_min = y_min;
                d.x_max = x_max;
                d.y_max = y_max;

                msg.detections.push_back(d);

                if (x_max > x_min && y_max > y_min) {
                    cv::rectangle(frame, cv::Point(x_min, y_min), cv::Point(x_max, y_max), cv::Scalar(0, 255, 0), 2);
                    cv::putText(frame, d.class_name + ": " + std::to_string(score).substr(0, 4),
                                cv::Point(x_min, std::max((float)y_min - 5, 0.0f)),
                                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
                }
            }
        }

        publisher_->publish(msg);

        // Calculate FPS
        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = current_time - prev_time_;
        double fps = 1.0 / elapsed.count();
        prev_time_ = current_time;

        cv::putText(frame, "FPS: " + std::to_string(fps).substr(0, 5), cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);

        cv::imshow("Hailo Real-time Detection", frame);
        cv::waitKey(1);
    }

    rclcpp::Publisher<unitree_go2_vision_msgs::msg::DetectionArray>::SharedPtr publisher_;
    cv::VideoCapture cap_;
    rclcpp::TimerBase::SharedPtr timer_;

    std::map<int, std::string> class_names_;
    std::chrono::steady_clock::time_point prev_time_;

    // Hailo config
    const size_t BATCH_SIZE = 1;
    const std::chrono::milliseconds TIMEOUT_MS{10000};
    const float CONF_THRES = 0.25f;

    std::unique_ptr<hailort::VDevice> vdevice_;
    std::shared_ptr<hailort::InferModel> infer_model_;
    std::unique_ptr<hailort::ConfiguredInferModel> configured_infer_model_;
    std::unique_ptr<hailort::ConfiguredInferModel::Bindings> bindings_;
    
    std::string output_name_;
    std::vector<uint8_t> output_buffer_;
    int input_w_, input_h_, input_c_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<HailoDetectionPublisher>());
    rclcpp::shutdown();
    return 0;
}
