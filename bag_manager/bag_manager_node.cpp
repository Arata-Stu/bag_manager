#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <rosbag2_cpp/recorder.hpp>
#include <rosbag2_cpp/record_options.hpp>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <string>

using namespace std::placeholders;

class Rosbag2TriggerNode : public rclcpp::Node
{
public:
  Rosbag2TriggerNode()
  : Node("rosbag2_trigger_node")
  {
    // パラメータ宣言
    declare_parameter<std::string>("output_dir", "rosbag2_output");
    declare_parameter<bool>("all_topics", true);
    declare_parameter<std::vector<std::string>>("topics", std::vector<std::string>{});

    // パラメータ取得
    get_parameter("output_dir", output_dir_);
    get_parameter("all_topics", all_topics_);
    get_parameter("topics", topics_);

    // サブスクライバ: トピック名はremapで変更可能
    trigger_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/rosbag2_recorder/trigger", 10,
      std::bind(&Rosbag2TriggerNode::triggerCallback, this, _1)
    );

    RCLCPP_INFO(get_logger(),
      "Started Rosbag2TriggerNode: output_dir=%s, all_topics=%s",
      output_dir_.c_str(),
      all_topics_ ? "true" : "false");
  }

private:
  void triggerCallback(const std_msgs::msg::Bool::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bool trigger = msg->data;

    // false -> true: start recording
    if (!recording_active_ && trigger) {
      rosbag2_cpp::RecordOptions options;
      // ユニークな出力ディレクトリ名にタイムスタンプを付加
      auto now = std::chrono::system_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
      options.uri = output_dir_ + "_" + std::to_string(ms);
      options.all = all_topics_;
      options.topics = topics_;

      recorder_ = std::make_shared<rosbag2_cpp::Recorder>();
      recorder_->init(options);
      record_thread_ = std::thread([this]() { recorder_->record(); });
      recording_active_ = true;
      RCLCPP_INFO(get_logger(), "Rosbag recording started: %s", options.uri.c_str());
    }
    // true -> false: stop recording
    else if (recording_active_ && !trigger) {
      recorder_->stop();
      if (record_thread_.joinable()) {
        record_thread_.join();
      }
      recording_active_ = false;
      RCLCPP_INFO(get_logger(), "Rosbag recording stopped");
    }
    // その他の遷移は無視
  }

  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr trigger_sub_;
  std::shared_ptr<rosbag2_cpp::Recorder> recorder_;
  std::thread record_thread_;
  std::mutex mutex_;

  bool recording_active_{false};
  std::string output_dir_;
  bool all_topics_{true};
  std::vector<std::string> topics_;
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<Rosbag2TriggerNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
