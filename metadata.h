/**
 * @file metadata.h
 * @brief 元数据定义与存储 — 标签、收藏、阅读进度管理
 *
 * 核心类型：
 *   - TagInfo:       标签（名称 + 颜色）
 *   - ProgressInfo:  阅读进度（百分比或页码）
 *   - BookMeta:      单个文件的元数据（标签列表 + 收藏 + 进度）
 *   - MetadataStore: 全局单例，管理所有文件的元数据（内存 + JSON 持久化）
 *
 * 双写架构：
 *   MetadataStore 仅管理内存和 JSON 文件。MySQL 同步由 MainWindow 负责，
 *   通过 DatabaseManager::syncAllMetadata() 实现。
 *
 * @version 3.0
 */

#ifndef METADATA_H
#define METADATA_H

#include <QString>
#include <QStringList>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QList>
#include <QFile>

 // =============================================================================
 // TagInfo — 标签
 // =============================================================================

 /**
  * @struct TagInfo
  * @brief 一个标签的定义：名称 + 显示颜色
  *
  * 示例：{"name": "已读", "color": "#27ae60"}
  */
struct TagInfo {
    QString name;       ///< 标签名称（唯一标识）
    QColor color;       ///< 标签显示颜色

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["name"] = name;
        obj["color"] = color.name();
        return obj;
    }

    static TagInfo fromJson(const QJsonObject& obj) {
        TagInfo t;
        t.name = obj["name"].toString();
        t.color = QColor(obj["color"].toString("#3498db"));  // 默认蓝色
        return t;
    }
};

// =============================================================================
// ProgressInfo — 阅读进度
// =============================================================================

/**
 * @struct ProgressInfo
 * @brief 阅读进度信息
 *
 * 支持两种模式：
 *   - "percent": 百分比模式，value ∈ [0, 100]
 *   - "page":    页码模式，value = 当前页，total = 总页数
 *
 * JSON 示例：
 *   {"type": "percent", "value": 45, "total": 0, "note": "第三章"}
 *   {"type": "page", "value": 128, "total": 350, "note": ""}
 *
 * ⚠️ 空对象 {} 表示"未设置进度"（isValid() == false）
 */
struct ProgressInfo {
    QString type;       ///< 进度类型："percent" 或 "page"（空字符串 = 无效）
    int value = 0;      ///< 进度值（百分比 0-100 或当前页码）
    int total = 0;      ///< 总页数（仅页码模式有意义）
    QString note;       ///< 备注文字

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["type"] = type;
        obj["value"] = value;
        obj["total"] = total;
        obj["note"] = note;
        return obj;
    }

    /**
     * @brief 从 JSON 对象反序列化
     * ⚠️ 注意：空对象（obj.isEmpty()）返回无效进度，而非带默认值的进度
     */
    static ProgressInfo fromJson(const QJsonObject& obj) {
        ProgressInfo p;
        if (obj.isEmpty()) return p; // 空对象 → 无效进度
        p.type = obj["type"].toString("percent");
        p.value = obj["value"].toInt(0);
        p.total = obj["total"].toInt(0);
        p.note = obj["note"].toString();
        return p;
    }

    bool isValid() const { return !type.isEmpty(); }

    /**
     * @brief 统一转换为百分比（0-100）
     * 用于统计面板的进度条显示
     */
    int percent() const {
        if (type == "percent") return value;
        if (type == "page" && total > 0) return qRound(100.0 * value / total);
        return 0;
    }
};

// =============================================================================
// BookMeta — 单文件元数据
// =============================================================================

/**
 * @struct BookMeta
 * @brief 一个文件的完整元数据
 *
 * 包含：标签列表、收藏状态、阅读进度
 * 以文件绝对路径为键存储在 MetadataStore 中
 */
struct BookMeta {
    QList<TagInfo> tags;        ///< 标签列表
    bool starred = false;       ///< 是否收藏
    ProgressInfo progress;      ///< 阅读进度

    QJsonObject toJson() const {
        QJsonObject obj;
        QJsonArray tagsArr;
        for (const auto& t : tags) tagsArr.append(t.toJson());
        obj["tags"] = tagsArr;
        obj["starred"] = starred;
        obj["progress"] = progress.toJson();
        return obj;
    }

    static BookMeta fromJson(const QJsonObject& obj) {
        BookMeta m;
        QJsonArray tagsArr = obj["tags"].toArray();
        for (const auto& v : tagsArr) m.tags.append(TagInfo::fromJson(v.toObject()));
        m.starred = obj["starred"].toBool(false);
        m.progress = ProgressInfo::fromJson(obj["progress"].toObject());
        return m;
    }
};

// =============================================================================
// MetadataStore — 全局元数据单例
// =============================================================================

/**
 * @class MetadataStore
 * @brief 线程安全的全局元数据存储（单例模式）
 *
 * ⚠️ 线程安全说明：
 *    当前实现**不是线程安全的**。BookScanner 在子线程中但不直接访问此类。
 *    所有 MetadataStore 操作都在主线程中进行。
 *    如果未来需要从子线程访问，需添加 QMutex 保护。
 *
 * JSON 持久化格式：
 *   {
 *     "/path/to/file1.pdf": { "tags": [...], "starred": true, "progress": {...} },
 *     "/path/to/file2.epub": { "tags": [...], "starred": false, "progress": {} }
 *   }
 */
class MetadataStore {
public:
    static MetadataStore& instance() {
        static MetadataStore inst;
        return inst;
    }

    /**
     * @brief 获取可修改的元数据引用
     * ⚠️ 如果路径不存在，会自动创建空的 BookMeta
     * 这是有意设计：修改操作总是需要确保条目存在
     */
    BookMeta& getMeta(const QString& path) {
        if (!m_data.contains(path)) {
            m_data[path] = BookMeta();
        }
        return m_data[path];
    }

    /**
     * @brief const 引用获取（不自动创建条目）
     * 路径不存在时返回静态空对象（安全的只读访问）
     */
    const BookMeta& getMeta(const QString& path) const {
        static const BookMeta emptyMeta;
        auto it = m_data.constFind(path);
        if (it != m_data.constEnd()) return it.value();
        return emptyMeta;
    }

    /**
     * @brief 只读查询（推荐用于展示场景）
     * 与 getMeta(const) 功能相同，但语义更清晰
     */
    const BookMeta& metaOf(const QString& path) const {
        static const BookMeta emptyMeta;
        auto it = m_data.constFind(path);
        if (it != m_data.constEnd()) return it.value();
        return emptyMeta;
    }

    bool hasMeta(const QString& path) const {
        return m_data.contains(path);
    }

    /**
     * @brief 删除指定路径的元数据
     * 通常在文件被物理删除后调用
     */
    void removePath(const QString& path) {
        m_data.remove(path);
    }

    /**
     * @brief 重命名路径（文件重命名时同步元数据键）
     * ⚠️ 如果 newPath 已有元数据，会被覆盖
     */
    void renamePath(const QString& oldPath, const QString& newPath) {
        if (m_data.contains(oldPath)) {
            m_data[newPath] = m_data[oldPath];
            m_data.remove(oldPath);
        }
    }

    QMap<QString, BookMeta> allData() const { return m_data; }

    /**
     * @brief 预设标签列表
     * 新用户可直接使用这些标签，无需手动创建
     */
    static QList<TagInfo> presetTags() {
        return {
            {"已读", QColor("#27ae60")}, {"待读", QColor("#f39c12")},
            {"在读", QColor("#3498db")}, {"经典", QColor("#e74c3c")},
            {"推荐", QColor("#9b59b6")}, {"科幻", QColor("#1abc9c")},
            {"文学", QColor("#e67e22")}, {"技术", QColor("#2c3e50")},
            {"历史", QColor("#8e44ad")}, {"哲学", QColor("#16a085")}
        };
    }

    /**
     * @brief 收集所有使用中的标签名称（去重、排序）
     * 用于标签筛选下拉框
     */
    QStringList allTagNames() const {
        QSet<QString> names;
        for (const auto& meta : m_data) {
            for (const auto& t : meta.tags) names.insert(t.name);
        }
        QStringList list = names.values();
        list.sort();
        return list;
    }

    /**
     * @brief 保存到 JSON 文件
     * 格式：Compact（单行），减少文件体积
     */
    void save(const QString& path) {
        QJsonObject root;
        for (auto it = m_data.begin(); it != m_data.end(); ++it) {
            root[it.key()] = it.value().toJson();
        }
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        }
    }

    /**
     * @brief 从 JSON 文件加载
     * ⚠️ 追加模式：不会清除现有数据，只覆盖同路径的条目
     */
    void load(const QString& path) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(f.readAll());
            auto obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                m_data[it.key()] = BookMeta::fromJson(it.value().toObject());
            }
        }
    }

    void clear() { m_data.clear(); }

private:
    MetadataStore() = default;
    QMap<QString, BookMeta> m_data;  ///< 路径 → 元数据映射
};

#endif // METADATA_H
