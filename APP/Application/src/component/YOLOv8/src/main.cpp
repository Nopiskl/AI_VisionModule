#include "NeuralNetworkRuntime.hpp"
#include "YoloV8Processor.hpp"
#include "camera/common/BackendLock.hpp"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct Options {
    std::string model;
    std::string classes;
    std::string framebuffer{"/dev/fb0"};
    std::string backendLock{"/run/v851s-camera/backend.lock"};
    unsigned int memoryBytes{17U * 1024U * 1024U};
    int cameraIndex{0};
};

volatile sig_atomic_t gStopRequested = 0;

void requestStop(int) {
    gStopRequested = 1;
}

void printUsage(const char* program) {
    std::cerr
        << "Usage: " << program
        << " --model PATH --classes PATH [--memory BYTES] [--camera INDEX]"
           " [--framebuffer PATH] [--backend-lock PATH]\n"
        << "Legacy: " << program
        << " modelFilePath classesFilePath [nnRuntimeMemSize]\n";
}

bool parseUnsigned(const char* text, unsigned int* output) {
    try {
        std::size_t parsedCharacters = 0;
        const unsigned long value = std::stoul(text, &parsedCharacters, 10);
        if (text[parsedCharacters] != '\0' || value == 0 ||
            value > 0xffffffffUL) {
            return false;
        }
        *output = static_cast<unsigned int>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseCameraIndex(const char* text, int* output) {
    try {
        std::size_t parsedCharacters = 0;
        const long value = std::stol(text, &parsedCharacters, 10);
        if (text[parsedCharacters] != '\0' || value < 0 || value > 1024) {
            return false;
        }
        *output = static_cast<int>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseOptions(int argc, char** argv, Options* options) {
    if (argc >= 3 && argv[1][0] != '-') {
        options->model = argv[1];
        options->classes = argv[2];
        if (argc == 4 && !parseUnsigned(argv[3], &options->memoryBytes)) {
            return false;
        }
        return argc == 3 || argc == 4;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            printUsage(argv[0]);
            std::exit(EXIT_SUCCESS);
        }
        if (index + 1 >= argc) {
            return false;
        }
        const char* value = argv[++index];
        if (argument == "--model") {
            options->model = value;
        } else if (argument == "--classes") {
            options->classes = value;
        } else if (argument == "--framebuffer") {
            options->framebuffer = value;
        } else if (argument == "--backend-lock") {
            options->backendLock = value;
        } else if (argument == "--memory") {
            if (!parseUnsigned(value, &options->memoryBytes)) {
                return false;
            }
        } else if (argument == "--camera") {
            if (!parseCameraIndex(value, &options->cameraIndex)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return !options->model.empty() && !options->classes.empty() &&
           !options->framebuffer.empty() && !options->backendLock.empty();
}

std::string trim(const std::string& value) {
    const auto isSpace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto start = std::find_if_not(value.begin(), value.end(), isSpace);
    const auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return start < end ? std::string(start, end) : std::string();
}

std::vector<std::string> loadClasses(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Can't open classes file: " + path);
    }
    std::vector<std::string> classes;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) {
            classes.push_back(std::move(line));
        }
    }
    if (classes.empty()) {
        throw std::runtime_error("Classes file is empty: " + path);
    }
    return classes;
}

std::string parentPath(const std::string& path) {
    const std::size_t separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        return ".";
    }
    return separator == 0 ? "/" : path.substr(0, separator);
}

void ensureDirectory(const std::string& path) {
    if (path.empty() || path.front() != '/') {
        throw std::runtime_error("Backend lock directory must be absolute");
    }
    std::string current;
    std::size_t offset = 1;
    while (offset <= path.size()) {
        const std::size_t slash = path.find('/', offset);
        const std::string part = path.substr(
            offset, slash == std::string::npos ? std::string::npos
                                               : slash - offset);
        if (!part.empty()) {
            current += "/" + part;
            struct stat metadata {};
            if (::stat(current.c_str(), &metadata) == 0) {
                if (!S_ISDIR(metadata.st_mode)) {
                    throw std::runtime_error("Lock path component is not a directory: " +
                                             current);
                }
            } else if (errno != ENOENT || ::mkdir(current.c_str(), 0755) != 0) {
                throw std::runtime_error("Can't create lock directory " + current +
                                         ": " + std::strerror(errno));
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        offset = slash + 1;
    }
}

class FramebufferWriter {
public:
    explicit FramebufferWriter(const std::string& device) : device_(device) {
        descriptor_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
        if (descriptor_ < 0) {
            throw std::runtime_error("Can't open framebuffer " + device + ": " +
                                     std::strerror(errno));
        }
        if (::ioctl(descriptor_, FBIOGET_VSCREENINFO, &variable_) != 0 ||
            ::ioctl(descriptor_, FBIOGET_FSCREENINFO, &fixed_) != 0) {
            const std::string detail = std::strerror(errno);
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Can't query framebuffer " + device + ": " +
                                     detail);
        }
        if (variable_.xres == 0 || variable_.yres == 0) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Framebuffer has an empty visible area");
        }
        if (variable_.bits_per_pixel != 16) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Framebuffer must use 16-bit RGB565");
        }
        if (variable_.red.offset != 11 || variable_.red.length != 5 ||
            variable_.green.offset != 5 || variable_.green.length != 6 ||
            variable_.blue.offset != 0 || variable_.blue.length != 5) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error(
                "Framebuffer bit fields do not describe RGB565");
        }
        const std::uint64_t rowBytes =
            static_cast<std::uint64_t>(variable_.xres) * 2U;
        const std::uint64_t lastRowEnd =
            (static_cast<std::uint64_t>(variable_.yoffset) + variable_.yres - 1U) *
                fixed_.line_length +
            static_cast<std::uint64_t>(variable_.xoffset) * 2U + rowBytes;
        if (rowBytes > fixed_.line_length || lastRowEnd > fixed_.smem_len) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Framebuffer geometry exceeds stride/memory");
        }
        void* mapping = ::mmap(nullptr, fixed_.smem_len,
                               PROT_READ | PROT_WRITE, MAP_SHARED,
                               descriptor_, 0);
        if (mapping == MAP_FAILED) {
            const std::string detail = std::strerror(errno);
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Can't map framebuffer " + device + ": " +
                                     detail);
        }
        mapping_ = static_cast<unsigned char*>(mapping);
        std::cout << device_ << " " << variable_.xres << "x" << variable_.yres
                  << " RGB565 stride=" << fixed_.line_length << std::endl;
    }

    ~FramebufferWriter() {
        if (mapping_ != nullptr) {
            ::munmap(mapping_, fixed_.smem_len);
        }
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    FramebufferWriter(const FramebufferWriter&) = delete;
    FramebufferWriter& operator=(const FramebufferWriter&) = delete;

    cv::Size size() const {
        return cv::Size(static_cast<int>(variable_.xres),
                        static_cast<int>(variable_.yres));
    }

    void write(const cv::Mat& bgrFrame) {
        cv::Mat resized;
        if (bgrFrame.cols != static_cast<int>(variable_.xres) ||
            bgrFrame.rows != static_cast<int>(variable_.yres)) {
            cv::resize(bgrFrame, resized, size());
        } else {
            resized = bgrFrame;
        }

        cv::Mat rgb565;
        cv::cvtColor(resized, rgb565, cv::COLOR_BGR2BGR565);
        const std::size_t rowBytes = static_cast<std::size_t>(variable_.xres) * 2U;
        for (unsigned int row = 0; row < variable_.yres; ++row) {
            const std::size_t offset = static_cast<std::size_t>(
                (static_cast<std::uint64_t>(variable_.yoffset) + row) *
                    fixed_.line_length +
                static_cast<std::uint64_t>(variable_.xoffset) * 2U);
            std::memcpy(mapping_ + offset,
                        rgb565.ptr(static_cast<int>(row)), rowBytes);
        }
    }

private:
    std::string device_;
    int descriptor_{-1};
    unsigned char* mapping_{nullptr};
    fb_var_screeninfo variable_ {};
    fb_fix_screeninfo fixed_ {};
};

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, &options)) {
        printUsage(argv[0]);
        return 2;
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        ensureDirectory(parentPath(options.backendLock));
        camera::common::BackendLock backendLock;
        const camera::common::BackendLockResult lockResult =
            backendLock.acquire(options.backendLock, "camera-yolo-worker");
        if (!lockResult.ok) {
            std::cerr << lockResult.code << ": " << lockResult.detail << '\n';
            return lockResult.code == "backend_busy" ? 3 : 1;
        }

        std::vector<std::string> classes = loadClasses(options.classes);
        const std::size_t classCount = classes.size();
        NeuralNetworkRuntime::Config runtimeConfig;
        runtimeConfig.modelFilePath = options.model;
        runtimeConfig.memSize = options.memoryBytes;
        NeuralNetworkRuntime runtime(runtimeConfig);

        YoloV8Processor::Config processorConfig;
        processorConfig.classes = std::move(classes);
        processorConfig.imgSize = cv::Size(320, 320);
        YoloV8Processor processor(processorConfig);

        cv::VideoCapture capture;
        capture.open(options.cameraIndex, cv::VideoCaptureAPIs::CAP_V4L2);
        if (!capture.isOpened()) {
            throw std::runtime_error("Can't initialize camera capture index " +
                                     std::to_string(options.cameraIndex));
        }
        FramebufferWriter framebuffer(options.framebuffer);

        cv::Mat frame;
        while (gStopRequested == 0) {
            capture >> frame;
            if (frame.empty()) {
                throw std::runtime_error("Camera returned an empty frame");
            }
            if (frame.depth() != CV_8U || frame.channels() != 3) {
                throw std::runtime_error(
                    "Camera frame must be 8-bit, three-channel BGR");
            }

            bool inputLoaded = false;
            std::vector<std::vector<float>> results = runtime.run(
                [&frame, &processor, &inputLoaded](
                    int bufferIndex, void* buffer,
                    NeuralNetworkRuntime::InputDataFormat format,
                    std::size_t bufferBytes) {
                    if (inputLoaded || bufferIndex != 0 ||
                        format != NeuralNetworkRuntime::FORMAT_INT8) {
                        throw std::invalid_argument(
                            "YOLO model must expose one INT8 input tensor");
                    }
                    cv::Mat input = frame.clone();
                    processor.preProcess(input);
                    const std::size_t requiredBytes =
                        input.total() * input.elemSize();
                    if (input.cols != 320 || input.rows != 320 ||
                        input.type() != CV_8UC3 || !input.isContinuous() ||
                        bufferBytes != requiredBytes) {
                        throw std::invalid_argument(
                            "YOLO input tensor must be exactly 1x3x320x320 INT8");
                    }
                    inputLoaded = true;
                    const std::size_t pixels = input.total();
                    auto* destination = static_cast<std::int8_t*>(buffer);
                    for (int channel = 2; channel >= 0; --channel) {
                        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
                            destination[pixel +
                                        static_cast<std::size_t>(channel) * pixels] =
                                static_cast<std::int8_t>(
                                    static_cast<int>(
                                        input.data[pixel * 3U +
                                                   static_cast<std::size_t>(channel)]) -
                                    128);
                        }
                    }
                });
            constexpr std::size_t kStrideElements =
                (320U / 8U) * (320U / 8U) +
                (320U / 16U) * (320U / 16U) +
                (320U / 32U) * (320U / 32U);
            const std::size_t expectedOutputElements =
                (classCount + 4U) * kStrideElements;
            if (!inputLoaded || results.size() != 1U ||
                results.front().size() != expectedOutputElements) {
                throw std::runtime_error(
                    "YOLO model must expose one compatible output tensor");
            }

            std::vector<YoloV8Processor::Detection> detections =
                processor.postProcess(CV_32FC1,
                                      results.front().data());
            processor.drawBoundingBox(frame, detections);
            framebuffer.write(frame);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
