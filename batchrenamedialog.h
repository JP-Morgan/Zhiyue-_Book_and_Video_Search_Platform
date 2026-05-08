/**
 * @file batchrenamedialog.h
 * @brief 批量重命名对话框 — 支持模板/正则/前缀/后缀四种模式
 *
 * 重命名模式：
 *   1. 模板模式：{n}=序号, {name}=原名, {ext}=扩展名
 *   2. 正则替换：查找匹配的正则表达式并替换
 *   3. 添加前缀：在文件名前添加文字
 *   4. 添加后缀：在扩展名前添加文字
 *
 * 特性：
 *   - 实时预览所有重命名结果
 *   - 重复名称检测与警告
 *
 * ⚠️ BUG 已修复（模板模式）：
 *    旧代码使用链式 replace()，如果 {n} 的替换值包含 "{name}" 字符串，
 *    后续的 replace("{name}", ...) 会误替换替换值中的文本。
 *    例如：模板 "{n}-{name}"，文件名 "test-1"，序号 1：
 *      旧代码："{n}-{name}" → "1-{name}" → "1-test-1"（正确）
 *      但如果文件名是 "1-{name}"："{n}-{name}" → "1-{name}" → "1-1-{name}"
 *    修复方案：逐字符扫描模板，一次性替换占位符，避免二次替换。
 *
 * @version 3.0
 */

#ifndef BATCHRENAMEDIALOG_H
#define BATCHRENAMEDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include "bookscanner.h"
#include "metadata.h"
#include "bookmodel.h"

class BatchRenameDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchRenameDialog(const QList<BookInfo>& books, QWidget* parent = nullptr)
        : QDialog(parent), m_books(books)
    {
        setWindowTitle("✏️ 批量重命名");
        setMinimumSize(600, 450);
        setupUI();
        updatePreview();
    }

    /** @return 旧路径 → 新文件名 的映射 */
    QMap<QString, QString> renameMapping() const { return m_mapping; }

private slots:
    /**
     * @brief 更新预览（任何参数变化时触发）
     * 生成所有文件的新名称，检测重复
     */
    void updatePreview() {
        m_mapping.clear();
        QString mode = m_modeCombo->currentData().toString();
        QString preview;
        QSet<QString> usedNames;
        bool hasDuplicates = false;

        for (int i = 0; i < m_books.size(); i++) {
            const BookInfo& book = m_books.at(i);
            QFileInfo fi(book.path);
            QString oldName = fi.fileName();
            QString newName = generateName(oldName, i + 1, mode);
            m_mapping[book.path] = newName;

            if (usedNames.contains(newName.toLower())) {
                preview += QString("%1  →  %2  ⚠️ 重复名称!\n").arg(oldName, newName);
                hasDuplicates = true;
            }
            else {
                preview += QString("%1  →  %2\n").arg(oldName, newName);
            }
            usedNames.insert(newName.toLower());
        }

        if (hasDuplicates) {
            preview += "\n⚠️ 存在重复的新文件名，重命名时后处理的文件会失败！";
            m_preview->setStyleSheet("color: #e74c3c;");
        }
        else {
            m_preview->setStyleSheet("");
        }

        m_preview->setText(preview);
    }

private:
    void setupUI() {
        auto* layout = new QVBoxLayout(this);

        // 模式选择器
        auto* modeLayout = new QHBoxLayout;
        modeLayout->addWidget(new QLabel("重命名方式："));
        m_modeCombo = new QComboBox;
        m_modeCombo->addItem("模板模式", "template");
        m_modeCombo->addItem("正则替换", "regex");
        m_modeCombo->addItem("添加前缀", "prefix");
        m_modeCombo->addItem("添加后缀", "suffix");
        connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this]() {
            updateModeFields();
            updatePreview();
            });
        modeLayout->addWidget(m_modeCombo);
        layout->addLayout(modeLayout);

        // 模板输入
        m_templateWidget = new QWidget;
        auto* tplLayout = new QVBoxLayout(m_templateWidget);
        tplLayout->setContentsMargins(0, 0, 0, 0);
        tplLayout->addWidget(new QLabel("模板（{n}=序号, {name}=原名, {ext}=扩展名）："));
        m_templateEdit = new QLineEdit("{n}-{name}");
        connect(m_templateEdit, &QLineEdit::textChanged, this, &BatchRenameDialog::updatePreview);
        tplLayout->addWidget(m_templateEdit);
        layout->addWidget(m_templateWidget);

        // 正则输入
        m_regexWidget = new QWidget;
        auto* regLayout = new QVBoxLayout(m_regexWidget);
        regLayout->setContentsMargins(0, 0, 0, 0);
        regLayout->addWidget(new QLabel("查找（正则）："));
        m_findEdit = new QLineEdit;
        connect(m_findEdit, &QLineEdit::textChanged, this, &BatchRenameDialog::updatePreview);
        regLayout->addWidget(m_findEdit);
        regLayout->addWidget(new QLabel("替换为："));
        m_replaceEdit = new QLineEdit;
        connect(m_replaceEdit, &QLineEdit::textChanged, this, &BatchRenameDialog::updatePreview);
        regLayout->addWidget(m_replaceEdit);
        m_regexWidget->hide();
        layout->addWidget(m_regexWidget);

        // 前缀输入
        m_prefixWidget = new QWidget;
        auto* preLayout = new QVBoxLayout(m_prefixWidget);
        preLayout->setContentsMargins(0, 0, 0, 0);
        preLayout->addWidget(new QLabel("前缀："));
        m_prefixEdit = new QLineEdit;
        connect(m_prefixEdit, &QLineEdit::textChanged, this, &BatchRenameDialog::updatePreview);
        preLayout->addWidget(m_prefixEdit);
        m_prefixWidget->hide();
        layout->addWidget(m_prefixWidget);

        // 后缀输入
        m_suffixWidget = new QWidget;
        auto* sufLayout = new QVBoxLayout(m_suffixWidget);
        sufLayout->setContentsMargins(0, 0, 0, 0);
        sufLayout->addWidget(new QLabel("后缀："));
        m_suffixEdit = new QLineEdit;
        connect(m_suffixEdit, &QLineEdit::textChanged, this, &BatchRenameDialog::updatePreview);
        sufLayout->addWidget(m_suffixEdit);
        m_suffixWidget->hide();
        layout->addWidget(m_suffixWidget);

        // 预览区
        layout->addWidget(new QLabel("预览："));
        m_preview = new QTextEdit;
        m_preview->setReadOnly(true);
        m_preview->setFontFamily("Consolas");
        m_preview->setMaximumHeight(180);
        layout->addWidget(m_preview);

        // 按钮
        auto* btnLayout = new QHBoxLayout;
        auto* confirmBtn = new QPushButton("确认重命名");
        confirmBtn->setStyleSheet("QPushButton { background:#27ae60; color:white; border:none; border-radius:8px; padding:8px 20px; font-weight:bold; }");
        connect(confirmBtn, &QPushButton::clicked, this, &QDialog::accept);
        auto* cancelBtn = new QPushButton("取消");
        cancelBtn->setStyleSheet("QPushButton { background:#6c757d; color:white; border:none; border-radius:8px; padding:8px 20px; }");
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        btnLayout->addStretch();
        btnLayout->addWidget(confirmBtn);
        btnLayout->addWidget(cancelBtn);
        layout->addLayout(btnLayout);
    }

    void updateModeFields() {
        QString mode = m_modeCombo->currentData().toString();
        m_templateWidget->setVisible(mode == "template");
        m_regexWidget->setVisible(mode == "regex");
        m_prefixWidget->setVisible(mode == "prefix");
        m_suffixWidget->setVisible(mode == "suffix");
    }

    /**
     * @brief 根据模式生成新文件名
     *
     * ⚠️ 模板模式已修复（逐字符扫描替换）：
     *    旧代码用链式 replace() 会导致 {n} 替换值意外匹配 {name}。
     *    新代码逐字符扫描，遇到 {xxx} 占位符时精确替换。
     *
     * @param oldName 原始文件名（含扩展名）
     * @param index   文件在列表中的序号（1-based）
     * @param mode    重命名模式
     * @return 新文件名
     */
    QString generateName(const QString& oldName, int index, const QString& mode) {
        int dotIdx = oldName.lastIndexOf('.');
        QString namePart = dotIdx > 0 ? oldName.left(dotIdx) : oldName;
        QString extPart = dotIdx > 0 ? oldName.mid(dotIdx) : "";

        if (mode == "template") {
            // ✅ 修复：逐字符扫描模板，精确替换占位符
            QString tpl = m_templateEdit->text();
            QString result;
            int i = 0;
            while (i < tpl.length()) {
                if (tpl[i] == '{') {
                    // 查找对应的 }
                    int closeIdx = tpl.indexOf('}', i + 1);
                    if (closeIdx > 0) {
                        QString placeholder = tpl.mid(i + 1, closeIdx - i - 1);
                        if (placeholder == "n") {
                            result += QString::number(index);
                            i = closeIdx + 1;
                            continue;
                        }
                        else if (placeholder == "name") {
                            result += namePart;
                            i = closeIdx + 1;
                            continue;
                        }
                        else if (placeholder == "ext") {
                            result += extPart;
                            i = closeIdx + 1;
                            continue;
                        }
                        // 未知占位符，保留原文
                    }
                }
                result += tpl[i];
                i++;
            }
            // 如果模板中没有 {ext}，自动追加扩展名
            if (!tpl.contains("{ext}")) result += extPart;
            return result;
        }
        else if (mode == "regex") {
            QString find = m_findEdit->text();
            QString replace = m_replaceEdit->text();
            if (find.isEmpty()) return oldName;
            QRegularExpression re(find);
            if (re.isValid()) return QString(oldName).replace(re, replace);
            return oldName;
        }
        else if (mode == "prefix") {
            return m_prefixEdit->text() + oldName;
        }
        else if (mode == "suffix") {
            if (dotIdx > 0) return oldName.left(dotIdx) + m_suffixEdit->text() + oldName.mid(dotIdx);
            return oldName + m_suffixEdit->text();
        }
        return oldName;
    }

    QList<BookInfo> m_books;
    QComboBox* m_modeCombo;
    QLineEdit* m_templateEdit;
    QLineEdit* m_findEdit, * m_replaceEdit;
    QLineEdit* m_prefixEdit, * m_suffixEdit;
    QWidget* m_templateWidget, * m_regexWidget, * m_prefixWidget, * m_suffixWidget;
    QTextEdit* m_preview;
    QMap<QString, QString> m_mapping;
};

#endif // BATCHRENAMEDIALOG_H
