/**
 * @file bookmodel.h
 * @brief 表格数据模型 — 将 BookInfo 列表适配为 Qt 的 QAbstractTableModel
 *
 * 支持：
 *   - 8 列显示（复选框/文件名/大小/日期/目录/标签/收藏/进度）
 *   - 多维筛选（搜索/类型/标签/收藏/文件夹）
 *   - 多列排序
 *   - 元数据（标签/收藏/进度）与文件信息的合并展示
 *
 * @see BookScanner 提供原始 BookInfo 数据
 * @see MetadataStore 提供标签/收藏/进度等元数据
 *
 * @version 3.0
 */

#ifndef BOOKMODEL_H
#define BOOKMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QSet>
#include <QIcon>
#include "bookscanner.h"
#include "metadata.h"

class BookModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /**
     * @enum Column
     * @brief 表格列定义
     */
    enum Column {
        ColCheck = 0,   ///< 复选框列（已隐藏，使用行选择代替）
        ColName,        ///< 文件名
        ColSize,        ///< 文件大小
        ColDate,        ///< 修改日期
        ColFolder,      ///< 所在目录
        ColTags,        ///< 标签
        ColStar,        ///< 收藏状态
        ColProgress,    ///< 阅读进度
        ColCount        ///< 列总数（哨兵值）
    };

    explicit BookModel(QObject* parent = nullptr)
        : QAbstractTableModel(parent) {
    }

    // ===== QAbstractTableModel 接口实现 =====

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        // ⚠️ 关键：parent 必须返回 0（表格模型没有树形结构）
        return parent.isValid() ? 0 : m_visible.size();
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : ColCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        switch (section) {
        case ColCheck:    return "";
        case ColName:     return "文件名";
        case ColSize:     return "大小";
        case ColDate:     return "修改日期";
        case ColFolder:   return "目录";
        case ColTags:     return "标签";
        case ColStar:     return "⭐";
        case ColProgress: return "进度";
        }
        return {};
    }

    /**
     * @brief 提供单元格数据
     *
     * 使用的 Qt 角色：
     *   - DisplayRole:    显示文本
     *   - UserRole:       文件路径（整行）
     *   - UserRole+1:     收藏状态布尔值（ColStar 列）
     *   - ToolTipRole:    完整路径（ColName 列）
     *   - ForegroundRole: 灰色字体（大小/日期/目录列）
     *   - TextAlignmentRole: 对齐方式
     */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_visible.size()) return {};

        const BookInfo& book = m_visible.at(index.row());
        const BookMeta& meta = MetadataStore::instance().metaOf(book.path);

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColName:   return book.name;
            case ColSize:   return formatSize(book.size);
            case ColDate:   return book.dateModified.toString("yyyy-MM-dd");
            case ColFolder: return book.folder;
            case ColTags: {
                QStringList names;
                for (const auto& t : meta.tags) names << t.name;
                return names.isEmpty() ? "-" : names.join(", ");
            }
            case ColStar:   return meta.starred ? "⭐" : "☆";
            case ColProgress: {
                if (!meta.progress.isValid()) return "-";
                if (meta.progress.type == "percent")
                    return QString("%1%").arg(meta.progress.value);
                if (meta.progress.total > 0)
                    return QString("%1/%2").arg(meta.progress.value).arg(meta.progress.total);
                return QString::number(meta.progress.value);
            }
            }
        }

        if (role == Qt::UserRole) {
            return book.path;  // 整行通用数据：文件路径
        }

        if (role == Qt::UserRole + 1 && index.column() == ColStar) {
            return meta.starred;  // StarDelegate 使用此角色绘制星标
        }

        if (role == Qt::ToolTipRole) {
            if (index.column() == ColName) return book.path;
        }

        if (role == Qt::ForegroundRole) {
            if (index.column() == ColSize) return QColor("#999");
            if (index.column() == ColDate) return QColor("#999");
            if (index.column() == ColFolder) return QColor("#999");
        }

        if (role == Qt::TextAlignmentRole) {
            if (index.column() == ColSize) return int(Qt::AlignRight | Qt::AlignVCenter);
            if (index.column() == ColDate) return int(Qt::AlignRight | Qt::AlignVCenter);
            if (index.column() == ColStar) return int(Qt::AlignCenter);
            if (index.column() == ColProgress) return int(Qt::AlignCenter);
        }

        return {};
    }

    // ===== 数据操作 =====

    /**
     * @brief 设置完整书籍列表（扫描完成后调用）
     * 重置模型，所有书籍同时进入 m_all 和 m_visible
     */
    void setBooks(const QList<BookInfo>& books) {
        beginResetModel();
        m_all = books;
        m_visible = books;
        endResetModel();
    }

    /**
     * @brief 应用筛选条件
     *
     * @param searchText     搜索关键字（普通文本或正则表达式）
     * @param typeFilter     文件类型后缀筛选（如 ".pdf"）
     * @param tagFilter      标签名筛选
     * @param starFilter     收藏状态筛选（"starred"/"unstarred"/""）
     * @param useRegex       是否使用正则表达式
     * @param contentSearch  （预留）是否搜索文件内容
     * @param selectedFolders 选中的文件夹集合（空 = 全部）
     *
     * ⚠️ 性能注意：每次筛选都遍历 m_all 并调用 beginResetModel/endResetModel。
     *    对于大量数据（>10000），可考虑增量更新优化。
     */
    void setFilter(const QString& searchText, const QString& typeFilter,
        const QString& tagFilter, const QString& starFilter,
        bool useRegex, bool contentSearch,
        const QSet<QString>& selectedFolders = {}) {
        beginResetModel();
        m_visible.clear();

        // 预编译正则（仅在启用正则且搜索非空时）
        QRegularExpression regex;
        if (useRegex && !searchText.isEmpty()) {
            regex = QRegularExpression(searchText, QRegularExpression::CaseInsensitiveOption);
            if (!regex.isValid()) regex = QRegularExpression(); // 无效正则降级为普通搜索
        }

        for (const BookInfo& book : m_all) {
            const BookMeta& meta = MetadataStore::instance().metaOf(book.path);

            // 文件夹筛选（多选）
            if (!selectedFolders.isEmpty() && !selectedFolders.contains(book.folder))
                continue;

            // 搜索筛选
            bool matchSearch = true;
            if (!searchText.isEmpty()) {
                if (useRegex && regex.isValid()) {
                    matchSearch = regex.match(book.name).hasMatch();
                }
                else {
                    matchSearch = book.name.contains(searchText, Qt::CaseInsensitive);
                }
            }

            // 类型筛选（endsWith 匹配，".doc" 同时匹配 .doc 和 .docx）
            bool matchType = typeFilter.isEmpty() ||
                book.name.endsWith(typeFilter, Qt::CaseInsensitive);

            // 标签筛选
            bool matchTag = true;
            if (!tagFilter.isEmpty()) {
                matchTag = false;
                for (const auto& t : meta.tags) {
                    if (t.name == tagFilter) { matchTag = true; break; }
                }
            }

            // 收藏筛选
            bool matchStar = true;
            if (starFilter == "starred") matchStar = meta.starred;
            else if (starFilter == "unstarred") matchStar = !meta.starred;

            if (matchSearch && matchType && matchTag && matchStar) {
                m_visible.append(book);
            }
        }
        endResetModel();
    }

    /**
     * @brief 排序
     *
     * ⚠️ 使用 beginResetModel/endResetModel 会导致视图丢失选择和滚动位置。
     *    改进方案：使用 layoutAboutToBeChanged/layoutChanged 保持选择状态。
     */
    void sortByColumn(int column, Qt::SortOrder order = Qt::AscendingOrder) {
        beginResetModel();
        switch (column) {
        case ColName:
            if (order == Qt::AscendingOrder)
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.name.toLower() < b.name.toLower(); });
            else
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.name.toLower() > b.name.toLower(); });
            break;
        case ColSize:
            if (order == Qt::AscendingOrder)
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.size < b.size; });
            else
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.size > b.size; });
            break;
        case ColDate:
            if (order == Qt::AscendingOrder)
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.dateModified < b.dateModified; });
            else
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.dateModified > b.dateModified; });
            break;
        case ColFolder:
            if (order == Qt::AscendingOrder)
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.folder < b.folder; });
            else
                std::sort(m_visible.begin(), m_visible.end(),
                    [](const BookInfo& a, const BookInfo& b) { return a.folder > b.folder; });
            break;
        }
        endResetModel();
    }

    // ===== 数据访问 =====

    const QList<BookInfo>& allBooks() const { return m_all; }
    const QList<BookInfo>& visibleBooks() const { return m_visible; }

    BookInfo bookAt(int row) const {
        if (row >= 0 && row < m_visible.size())
            return m_visible.at(row);
        return {};
    }

    /**
     * @brief 刷新视图（不改变数据，仅触发视图重绘）
     * 用于元数据（标签/收藏/进度）修改后的界面刷新
     */
    void refresh() {
        beginResetModel();
        endResetModel();
    }

    /**
     * @brief 格式化文件大小为人类可读字符串
     *
     * @param bytes 文件大小（字节）
     * @return 格式化字符串，如 "1.5 MB"、"2.30 GB"
     */
    static QString formatSize(qint64 bytes) {
        if (bytes < 1024) return QString("%1 B").arg(bytes);
        if (bytes < 1048576) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        if (bytes < 1073741824LL) return QString("%1 MB").arg(bytes / 1048576.0, 0, 'f', 1);
        return QString("%1 GB").arg(bytes / 1073741824.0, 0, 'f', 2);
    }

private:
    QList<BookInfo> m_all;      ///< 完整书籍列表（扫描结果的全集）
    QList<BookInfo> m_visible;  ///< 筛选后的可见列表（显示在表格中）
};

#endif // BOOKMODEL_H
