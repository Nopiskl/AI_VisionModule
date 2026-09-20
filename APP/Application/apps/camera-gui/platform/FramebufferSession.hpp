#pragma once
#include <QString>
namespace camera { namespace gui {
// Lifetime must enclose QApplication: prepare before QPA, release after QPA.
class FramebufferSession {
public:
    FramebufferSession(QString device, bool enabled);
    ~FramebufferSession();
    FramebufferSession(const FramebufferSession&) = delete;
    FramebufferSession& operator=(const FramebufferSession&) = delete;
    static bool release(const QString& device);
private:
    QString device_;
    bool enabled_;
};
}}
