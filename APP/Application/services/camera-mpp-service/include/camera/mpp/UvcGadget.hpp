#pragma once

#include <cstdint>

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/videodev2.h>

namespace camera {
namespace mpp {
namespace uvc {

// Userspace ABI exposed by the Linux UVC gadget video node. These definitions
// mirror the ABI used by sun8iw21 sample_uvcout; they are intentionally kept
// separate from configfs gadget creation, which is a deployment concern.
constexpr std::uint32_t kEventConnect = V4L2_EVENT_PRIVATE_START + 0;
constexpr std::uint32_t kEventDisconnect = V4L2_EVENT_PRIVATE_START + 1;
constexpr std::uint32_t kEventStreamOn = V4L2_EVENT_PRIVATE_START + 2;
constexpr std::uint32_t kEventStreamOff = V4L2_EVENT_PRIVATE_START + 3;
constexpr std::uint32_t kEventSetup = V4L2_EVENT_PRIVATE_START + 4;
constexpr std::uint32_t kEventData = V4L2_EVENT_PRIVATE_START + 5;

constexpr std::uint8_t kControlInterface = 0;
constexpr std::uint8_t kStreamingInterface = 1;

struct RequestData {
    std::int32_t length;
    std::uint8_t data[60];
};

struct Event {
    union {
        enum usb_device_speed speed;
        struct usb_ctrlrequest request;
        RequestData data;
    };
};

constexpr unsigned long kSendResponse = _IOW('U', 1, RequestData);

static_assert(sizeof(RequestData) == 64, "unexpected UVC request ABI size");
static_assert(sizeof(Event) == 64, "unexpected UVC event ABI size");

}  // namespace uvc
}  // namespace mpp
}  // namespace camera
