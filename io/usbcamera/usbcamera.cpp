#include "usbcamera.hpp"

#include <yaml-cpp/yaml.h>

#include <stdexcept>

#include "tools/logger.hpp"

using namespace std::chrono_literals;

namespace io
{
USBCamera::USBCamera(const std::string & open_name, const std::string & config_path)
: open_name_(open_name), quit_(false), ok_(false), queue_(1)
{
  auto yaml = YAML::LoadFile(config_path);
  image_width_ = yaml["image_width"].as<double>();
  image_height_ = yaml["image_height"].as<double>();
  usb_exposure_ = yaml["usb_exposure"].as<double>();
  usb_frame_rate_ = yaml["usb_frame_rate"].as<double>();
  usb_gamma_ = yaml["usb_gamma_"].as<double>();
  usb_gain_ = yaml["usb_gain_"].as<double>();
  try_open();

  // 守护线程
  daemon_thread_ = std::thread{[this] {
    while (!quit_) {
      std::this_thread::sleep_for(100ms);

      if (ok_) continue;

      if (capture_thread_.joinable()) capture_thread_.join();

      close();
      try_open();
    }
  }};
}

USBCamera::~USBCamera()
{
  quit_ = true;
  if (daemon_thread_.joinable()) daemon_thread_.join();
  if (capture_thread_.joinable()) capture_thread_.join();
  close();
  tools::logger()->info("USBCamera destructed.");
}

cv::Mat USBCamera::read()
{
  if (!cap_.isOpened()) {
    tools::logger()->warn("Error: Failed to read {} USB camera", this->device_name);
    return cv::Mat();
  }
  cap_ >> img_;
  return img_;
}

void USBCamera::read(cv::Mat & img, std::chrono::steady_clock::time_point & timestamp)
{
  CameraData data;
  queue_.pop(data);

  img = data.img;
  timestamp = data.timestamp;
}

void USBCamera::open()
{
  std::string ture_device_name = "/dev/" + open_name_;
  cap_.open(ture_device_name, cv::CAP_V4L);  // 使用V4L2后端打开相机
  if (!cap_.isOpened()) {
    tools::logger()->warn("Error: Failed to open USB camera");
    return;
  }
  sharpness_ = cap_.get(cv::CAP_PROP_SHARPNESS);

  if (sharpness_ == 2)
    device_name = "left";
  else if (sharpness_ == 3)
    device_name = "back";
  else
    device_name = "right";

  cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));  // 选择MJPG格式
  cap_.set(cv::CAP_PROP_FPS, usb_frame_rate_);                                 // 设置帧率
  cap_.set(cv::CAP_PROP_FRAME_WIDTH, image_width_);                            // 设置帧宽
  cap_.set(cv::CAP_PROP_FRAME_HEIGHT, image_height_);                          // 设置帧高
  cap_.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);                                     // 关闭自动曝光
  cap_.set(cv::CAP_PROP_EXPOSURE, usb_exposure_);  // 设置曝光时间为100us
  cap_.set(cv::CAP_PROP_GAMMA, usb_gamma_);
  cap_.set(cv::CAP_PROP_GAIN, usb_gain_);
  tools::logger()->info("{} USBCamera opened", device_name);
  tools::logger()->info("USBCamera exposure time:{}", cap_.get(cv::CAP_PROP_EXPOSURE));
  tools::logger()->info("USBCamera fps:{}", cap_.get(cv::CAP_PROP_FPS));
  tools::logger()->info("USBCamera gamma:{}", cap_.get(cv::CAP_PROP_GAMMA));
  // 检查相机是否打开成功
  // 取图线程
  capture_thread_ = std::thread{[this] {
    ok_ = true;
    while (!quit_) {
      std::this_thread::sleep_for(1ms);

      cv::Mat img;
      cap_ >> img;
      auto timestamp = std::chrono::steady_clock::now();

      queue_.push({img, timestamp});
    }
  }};
}

void USBCamera::try_open()
{
  try {
    open();
  } catch (const std::exception & e) {
    tools::logger()->warn("{}", e.what());
  }
}

void USBCamera::close() { cap_.release(); }

}  // namespace io
