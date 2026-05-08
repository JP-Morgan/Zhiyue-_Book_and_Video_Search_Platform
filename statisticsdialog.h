/**
 * @file statisticsdialog.h
 * @brief 统计分析对话框 — 按格式/目录/标签三个维度展示书籍分布
 *
 * 选项卡：
 *   1. 按格式：统计各文件类型（PDF/EPUB/MP4 等）的数量、占比、总大小
 *   2. 按目录：统计各子目录的文件数和大小
 *   3. 按标签：统计各标签被使用的次数
 *
 * @version 3.0
 */

#ifndef STATISTICSDIALOG_H
#define STATISTICSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QMap>
#include "bookscanner.h"
#include "metadata.h"
#include "bookmodel.h"

class StatisticsDialog : public QDialog {
    Q_OBJECT
public:
    explicit StatisticsDialog(const QList<BookInfo>& books, QWidget* parent = nullptr)
        : QDialog(parent), m_books(books)
    {
        setWindowTitle("📊 书籍统计");
        setMinimumSize(550, 400);
        setupUI();
    }

private:
    void setupUI() {
        auto* layout = new QVBoxLayout(this);

        // 汇总信息
        qint64 totalSize = 0;
        for (const auto& b : m_books) totalSize += b.size;
        auto* summary = new QLabel(
            QString("总文件数: %1 个  |  总大小: %2")
            .arg(m_books.size())
            .arg(BookModel::formatSize(totalSize)));
        summary->setStyleSheet("font-size:14px; font-weight:bold; color:#2c3e50; padding:10px;");
        layout->addWidget(summary);

        auto* tabs = new QTabWidget;
        tabs->addTab(createTypeTab(), "按格式");
        tabs->addTab(createFolderTab(), "按目录");
        tabs->addTab(createTagTab(), "按标签");

        layout->addWidget(tabs);

        auto* closeBtn = new QPushButton("关闭");
        closeBtn->setStyleSheet("QPushButton { background:#3498db; color:white; border:none; border-radius:8px; padding:8px 30px; font-weight:bold; }");
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
        layout->addWidget(closeBtn, 0, Qt::AlignCenter);
    }

    /** @brief 按文件格式统计 */
    QWidget* createTypeTab() {
        QMap<QString, int> counts;      // 格式 → 数量
        QMap<QString, qint64> sizes;    // 格式 → 总大小
        for (const auto& b : m_books) {
            QString ext = QFileInfo(b.path).suffix().toLower();
            counts[ext]++;
            sizes[ext] += b.size;
        }

        auto* table = new QTableWidget(counts.size(), 4);
        table->setHorizontalHeaderLabels({ "格式", "数量", "占比", "大小" });
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);

        int row = 0;
        auto keys = counts.keys();
        // 按数量降序排列
        std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
            return counts[a] > counts[b];
            });
        for (const auto& ext : keys) {
            double pct = m_books.size() > 0 ? 100.0 * counts[ext] / m_books.size() : 0;
            table->setItem(row, 0, new QTableWidgetItem(ext.toUpper()));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(counts[ext])));
            table->item(row, 1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 2, new QTableWidgetItem(QString("%1%").arg(pct, 0, 'f', 1)));
            table->item(row, 2)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 3, new QTableWidgetItem(BookModel::formatSize(sizes[ext])));
            table->item(row, 3)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row++;
        }
        return table;
    }

    /** @brief 按目录统计 */
    QWidget* createFolderTab() {
        QMap<QString, int> counts;
        QMap<QString, qint64> sizes;
        for (const auto& b : m_books) {
            counts[b.folder]++;
            sizes[b.folder] += b.size;
        }

        auto* table = new QTableWidget(counts.size(), 3);
        table->setHorizontalHeaderLabels({ "目录", "文件数", "大小" });
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);

        int row = 0;
        auto keys = counts.keys();
        std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
            return counts[a] > counts[b];
            });
        for (const auto& folder : keys) {
            table->setItem(row, 0, new QTableWidgetItem("📁 " + folder));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(counts[folder])));
            table->item(row, 1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 2, new QTableWidgetItem(BookModel::formatSize(sizes[folder])));
            table->item(row, 2)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row++;
        }
        return table;
    }

    /** @brief 按标签统计 */
    QWidget* createTagTab() {
        QMap<QString, int> counts;      // 标签名 → 使用次数
        QMap<QString, QColor> colors;   // 标签名 → 颜色
        for (const auto& b : m_books) {
            const BookMeta& meta = MetadataStore::instance().getMeta(b.path);
            for (const auto& t : meta.tags) {
                counts[t.name]++;
                colors[t.name] = t.color;
            }
        }

        if (counts.isEmpty()) {
            auto* lbl = new QLabel("暂无标签数据");
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color:#999; font-size:14px; padding:30px;");
            return lbl;
        }

        auto* table = new QTableWidget(counts.size(), 2);
        table->setHorizontalHeaderLabels({ "标签", "书籍数" });
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);

        int row = 0;
        auto keys = counts.keys();
        std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
            return counts[a] > counts[b];
            });
        for (const auto& tag : keys) {
            auto* item = new QTableWidgetItem(tag);
            item->setBackground(colors.value(tag, QColor("#999")));
            item->setForeground(Qt::white);
            table->setItem(row, 0, item);
            table->setItem(row, 1, new QTableWidgetItem(QString::number(counts[tag])));
            table->item(row, 1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row++;
        }
        return table;
    }

    QList<BookInfo> m_books;
};

#endif // STATISTICSDIALOG_H
