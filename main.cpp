/**
 * @file main.cpp
 * @brief 应用程序入口 — 知阅·书籍视频检索平台 v3.0
 *
 * 初始化：
 *   1. QApplication 创建
 *   2. 设置应用名称/版本/组织名
 *   3. 设置全局字体（优先 Microsoft YaHei，fallback Segoe UI）
 *   4. 设置 Fusion 风格 + 自定义调色板（跨平台一致外观）
 *   5. 创建并显示主窗口
 *   6. 进入事件循环
 *
 * @version 3.0
 */

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 应用元信息
    app.setApplicationName("书籍管理器");
    app.setApplicationVersion("3.0");
    app.setOrganizationName("BookManager");

    // 全局字体：优先微软雅黑，fallback Segoe UI
    QFont font("Microsoft YaHei", 10);
    if (!font.exactMatch()) {
        font = QFont("Segoe UI", 10);
    }
    app.setFont(font);

    // Fusion 风格：跨平台一致的现代外观
    app.setStyle(QStyleFactory::create("Fusion"));

    // 自定义调色板：深色文字 + 浅色背景 + 紫蓝高亮
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(240, 240, 240));
    palette.setColor(QPalette::WindowText, QColor(44, 62, 80));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipText, QColor(44, 62, 80));
    palette.setColor(QPalette::Text, QColor(44, 62, 80));
    palette.setColor(QPalette::Button, QColor(240, 240, 240));
    palette.setColor(QPalette::ButtonText, QColor(44, 62, 80));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Link, QColor(52, 152, 219));
    palette.setColor(QPalette::Highlight, QColor(102, 126, 234));     // 紫蓝主色调
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    app.setPalette(palette);

    MainWindow window;
    window.show();

    return app.exec();
}
