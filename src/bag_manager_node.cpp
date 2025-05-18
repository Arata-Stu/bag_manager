#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <rclcpp/serialized_message.hpp>

#include <rosbag2_cpp/typesupport_helpers.hpp>
#include <rosbag2_cpp/writers/sequential_writer.hpp>
#include <rosbag2_storage/serialized_bag_message.hpp>

#include <rcutils/time.h>
#include <rcutils/error_handling.h>

#include <string>
#include <vector>
#include <unordered_map>

using namespace std::placeholders;

class Rosbag2TriggerNode : public rclcpp::Node
{
public:
  Rosbag2TriggerNode()
  : Node("rosbag2_trigger_node")
  {
    // --- パラメータ宣言と取得 ---
    declare_parameter<std::string>("output_dir", "rosbag2_output");
    declare_parameter<bool>("all_topics", true);
    declare_parameter<std::vector<std::string>>("topics", std::vector<std::string>{});
    get_parameter("output_dir", output_dir_);
    get_parameter("all_topics", all_topics_);
    get_parameter("topics", topics_);

    // トリガー用サブスクライバだけは常にアクティブ
    trigger_sub_ = create_subscription<std_msgs::msg::Bool>(
      "rosbag2_recorder/trigger", 10,
      std::bind(&Rosbag2TriggerNode::triggerCallback, this, _1));

    RCLCPP_INFO(get_logger(),
      "Initialized: output_dir='%s', all_topics=%s, topics count=%zu",
      output_dir_.c_str(),
      all_topics_ ? "true" : "false",
      topics_.size());
  }

private:
  void triggerCallback(const std_msgs::msg::Bool::SharedPtr msg)
  {
    if (msg->data && !recording_) {
      startRecording();
    } else if (!msg->data && recording_) {
      stopRecording();
    }
  }

  void startRecording()
  {
    RCLCPP_INFO(get_logger(), "=== Start Recording ===");

    // 1) SequentialWriter の生成・open
    writer_ = std::make_unique<rosbag2_cpp::writers::SequentialWriter>();
    rosbag2_storage::StorageOptions storage_opts{output_dir_, "sqlite3"};
    rosbag2_cpp::ConverterOptions converter_opts{
      { rmw_get_serialization_format(), rmw_get_serialization_format() }
    };
    writer_->open(storage_opts, converter_opts);

    // 2) 録画対象トピック名と型を決定
    std::vector<std::string> record_topics;
    auto all = this->get_topic_names_and_types();
    if (all_topics_) {
      // 全トピック／ただしトリガートピックは除外
      for (auto &p : all) {
        if (p.first == "rosbag2_recorder/trigger") {
          continue;
        }
        record_topics.push_back(p.first);
        topic_types_[p.first] = p.second.front();
      }
    } else {
      // パラメータで指定されたもののみ
      for (auto &t : topics_) {
        auto it = all.find(t);
        if (it != all.end()) {
          record_topics.push_back(t);
          topic_types_[t] = it->second.front();
        } else {
          RCLCPP_WARN(get_logger(), "Topic '%s' not found, skip.", t.c_str());
        }
      }
    }

    // 3) 各トピックを writer に登録し、Generic（SerializedMessage）でサブスクライブ
    for (auto &t : record_topics) {
      writer_->create_topic({
        t,
        topic_types_[t],
        rmw_get_serialization_format(),
        ""
      });

      auto sub = this->create_subscription<rclcpp::SerializedMessage>(
        t,  // テンプレートに SerializedMessage を渡すと generic subscribe になる
        10,
        std::bind(&Rosbag2TriggerNode::serializedCallback, this, _1, t)
      );
      subs_.push_back(sub);
    }

    recording_ = true;
    RCLCPP_INFO(get_logger(),
      "Recording started: %zu topics registered.", subs_.size());
  }

  void stopRecording()
  {
    RCLCPP_INFO(get_logger(), "=== Stop Recording ===");

    // サブスクライバ解放 → トピック受信停止
    subs_.clear();

    // writer の破棄 → bag ファイル close
    writer_.reset();
    topic_types_.clear();

    recording_ = false;
    RCLCPP_INFO(get_logger(), "Recording stopped.");
  }

  void serializedCallback(
    std::shared_ptr<rclcpp::SerializedMessage> msg,
    const std::string & topic_name)
  {
    if (!recording_) {
      return;
    }
    // メッセージを rosbag2 用に包み替え
    auto bag_msg = std::make_shared<rosbag2_storage::SerializedBagMessage>();
    bag_msg->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
      new rcutils_uint8_array_t,
      [](rcutils_uint8_array_t * p) {
        rcutils_uint8_array_fini(p);
        delete p;
      }
    );
    *bag_msg->serialized_data = msg->release_rcl_serialized_message();
    bag_msg->topic_name = topic_name;
    if (rcutils_system_time_now(&bag_msg->time_stamp) != RCUTILS_RET_OK) {
      RCLCPP_ERROR(get_logger(), "Time stamp error: %s",
                   rcutils_get_error_string().str);
    }
    writer_->write(bag_msg);
  }

  // --- メンバ変数 ---
  std::string output_dir_;
  bool all_topics_;
  std::vector<std::string> topics_;

  std::unique_ptr<rosbag2_cpp::writers::SequentialWriter> writer_;
  std::vector<rclcpp::Subscription<rclcpp::SerializedMessage>::SharedPtr> subs_;
  std::unordered_map<std::string, std::string> topic_types_;
  bool recording_{false};

  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr trigger_sub_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Rosbag2TriggerNode>());
  rclcpp::shutdown();
  return 0;
}
