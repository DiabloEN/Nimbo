#pragma once

#include <QMainWindow>
#include <memory>

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& dataDirectory = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
