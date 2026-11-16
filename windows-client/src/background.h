#pragma once
#include <QImage>
#include <memory>
class BackgroundBlur final {
public:
    BackgroundBlur();~BackgroundBlur();
    QImage apply(const QImage& image,int radius);
private:
    struct Engine;std::unique_ptr<Engine> engine;
};
