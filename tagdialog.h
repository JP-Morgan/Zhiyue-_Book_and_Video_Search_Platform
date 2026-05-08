/**
 * @file tagdialog.h
 * @brief 标签编辑对话框 — 为单个文件添加/删除标签
 *
 * 功能：
 *   - 显示当前文件的标签（点击删除）
 *   - 预设标签快速添加
 *   - 自定义标签（名称 + 颜色选择器）
 *
 * 信号：
 *   tagsChanged — 标签变更后发射，通知主窗口刷新界面
 *
 * @version 3.0
 */

#ifndef TAGDIALOG_H
#define TAGDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QColorDialog>
#include <QFrame>
#include "metadata.h"

class TagDialog : public QDialog {
    Q_OBJECT
public:
    explicit TagDialog(const QString& filePath, QWidget* parent = nullptr)
        : QDialog(parent), m_filePath(filePath)
    {
        setWindowTitle("🏷️ 编辑标签");
        setMinimumWidth(420);
        setupUI();
    }

    void setupUI() {
        auto* mainLayout = new QVBoxLayout(this);
        BookMeta& meta = MetadataStore::instance().getMeta(m_filePath);

        // 当前标签区域
        mainLayout->addWidget(new QLabel("当前标签："));
        m_currentTagsFrame = new QFrame;
        m_currentTagsFrame->setFrameShape(QFrame::StyledPanel);
        m_currentTagsLayout = new QHBoxLayout(m_currentTagsFrame);
        m_currentTagsLayout->setContentsMargins(5, 5, 5, 5);
        mainLayout->addWidget(m_currentTagsFrame);
        refreshCurrentTags();

        // 预设标签区域
        mainLayout->addWidget(new QLabel("预设标签（点击添加）："));
        auto* presetFrame = new QFrame;
        auto* presetLayout = new QHBoxLayout(presetFrame);
        presetLayout->setContentsMargins(0, 0, 0, 0);
        const auto presets = MetadataStore::presetTags();
        for (int i = 0; i < presets.size(); i++) {
            const auto& pt = presets.at(i);
            auto* btn = new QPushButton(pt.name);
            btn->setStyleSheet(QString(
                "QPushButton { background:%1; color:white; border:none; border-radius:10px; "
                "padding:3px 10px; font-size:12px; }"
                "QPushButton:hover { opacity:0.8; }"
            ).arg(pt.color.name()));
            presetLayout->addWidget(btn);
            connect(btn, &QPushButton::clicked, this, [this, pt]() {
                addTag(pt.name, pt.color);
                });
        }
        presetLayout->addStretch();
        mainLayout->addWidget(presetFrame);

        // 自定义标签输入区域
        auto* customLayout = new QHBoxLayout;
        m_customName = new QLineEdit;
        m_customName->setPlaceholderText("自定义标签名称");
        m_customColorBtn = new QPushButton;
        m_customColorBtn->setFixedSize(36, 30);
        m_tagColor = QColor("#3498db");
        updateColorButton();
        connect(m_customColorBtn, &QPushButton::clicked, this, [this]() {
            QColor c = QColorDialog::getColor(m_tagColor, this, "选择标签颜色");
            if (c.isValid()) { m_tagColor = c; updateColorButton(); }
            });
        auto* addBtn = new QPushButton("添加");
        addBtn->setStyleSheet("QPushButton { background:#27ae60; color:white; border:none; border-radius:5px; padding:5px 12px; }");
        // 回车键也可以添加标签
        connect(addBtn, &QPushButton::clicked, this, [this]() {
            QString name = m_customName->text().trimmed();
            if (!name.isEmpty()) { addTag(name, m_tagColor); m_customName->clear(); }
            });
        customLayout->addWidget(m_customName);
        customLayout->addWidget(m_customColorBtn);
        customLayout->addWidget(addBtn);
        mainLayout->addLayout(customLayout);

        // 完成按钮
        auto* closeBtn = new QPushButton("完成");
        closeBtn->setStyleSheet("QPushButton { background:#3498db; color:white; border:none; border-radius:8px; padding:8px 30px; font-weight:bold; }");
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
        mainLayout->addWidget(closeBtn, 0, Qt::AlignCenter);
    }

private:
    void updateColorButton() {
        m_customColorBtn->setStyleSheet(
            QString("background:%1; border:1px solid #ccc; border-radius:4px;").arg(m_tagColor.name()));
    }

    /**
     * @brief 刷新当前标签显示
     *
     * ⚠️ BUG 已修复（旧版本用索引 i 删除标签）：
     *    如果同时删除多个标签，索引会在删除后偏移导致越界或删除错误标签。
     *    修复方案：使用标签名称匹配删除（removeTagByName）。
     */
    void refreshCurrentTags() {
        // 清除布局中的所有 widgets
        QLayoutItem* child;
        while ((child = m_currentTagsLayout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }

        const BookMeta& meta = MetadataStore::instance().getMeta(m_filePath);
        for (int i = 0; i < meta.tags.size(); i++) {
            const auto& tag = meta.tags.at(i);
            auto* btn = new QPushButton(tag.name + " ×");
            btn->setStyleSheet(QString(
                "QPushButton { background:%1; color:white; border:none; border-radius:10px; "
                "padding:3px 10px; font-size:12px; }"
                "QPushButton:hover { background:#e74c3c; }"
            ).arg(tag.color.name()));
            m_currentTagsLayout->addWidget(btn);
            // ✅ 修复：用标签名匹配而非索引，避免删除后索引越界
            QString tagName = tag.name;
            connect(btn, &QPushButton::clicked, this, [this, tagName]() {
                removeTagByName(tagName);
                });
        }

        if (meta.tags.isEmpty()) {
            auto* lbl = new QLabel("无标签");
            lbl->setStyleSheet("color:#999; font-size:12px;");
            m_currentTagsLayout->addWidget(lbl);
        }
        m_currentTagsLayout->addStretch();
    }

    /**
     * @brief 添加标签（去重检查）
     */
    void addTag(const QString& name, const QColor& color) {
        BookMeta& meta = MetadataStore::instance().getMeta(m_filePath);
        for (const auto& t : meta.tags) {
            if (t.name == name) return; // 已存在，跳过
        }
        TagInfo tag;
        tag.name = name;
        tag.color = color;
        meta.tags.append(tag);
        refreshCurrentTags();
        emit tagsChanged();
    }

    /**
     * @brief 按名称删除标签
     * 遍历查找匹配的标签名并移除
     */
    void removeTagByName(const QString& name) {
        BookMeta& meta = MetadataStore::instance().getMeta(m_filePath);
        for (int i = 0; i < meta.tags.size(); i++) {
            if (meta.tags[i].name == name) {
                meta.tags.removeAt(i);
                refreshCurrentTags();
                emit tagsChanged();
                return;
            }
        }
    }

signals:
    void tagsChanged();

private:
    QString m_filePath;
    QFrame* m_currentTagsFrame;
    QHBoxLayout* m_currentTagsLayout;
    QLineEdit* m_customName;
    QPushButton* m_customColorBtn;
    QColor m_tagColor;
};

#endif // TAGDIALOG_H
