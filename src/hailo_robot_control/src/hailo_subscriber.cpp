#include <rclcpp/rclcpp.hpp>
#include <unitree_go2_vision_msgs/msg/detection_array.hpp>

class HailoSubscriberNode : public rclcpp::Node
{
public:
    HailoSubscriberNode() : Node("hailo_subscriber_node")
    {
        subscription_ = this->create_subscription<unitree_go2_vision_msgs::msg::DetectionArray>(
            "hailo_detections", 10,
            std::bind(&HailoSubscriberNode::topic_callback, this, std::placeholders::_1));
        
        RCLCPP_INFO(this->get_logger(), "Hailo Subscriber Node has been started.");
    }

private:
    void topic_callback(const unitree_go2_vision_msgs::msg::DetectionArray::SharedPtr msg)
    {
        RCLCPP_INFO(this->get_logger(), "Received %zu detections", msg->detections.size());
        
        for (const auto & det : msg->detections) {
            RCLCPP_INFO(this->get_logger(), 
                "[ID: %d] %s: Score=%.2f, BBox=[%.0f, %.0f, %.0f, %.0f]", 
                det.class_id, 
                det.class_name.c_str(), 
                det.score, 
                det.x_min, 
                det.y_min, 
                det.x_max, 
                det.y_max);
        }
    }

    rclcpp::Subscription<unitree_go2_vision_msgs::msg::DetectionArray>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<HailoSubscriberNode>());
    rclcpp::shutdown();
    return 0;
}
