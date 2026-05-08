/**
 * @file databasemanager.h
 * @brief MySQL 数据库管理器 — 全局单例，提供书籍和元数据的数据库 CRUD 操作
 *
 * 职责：
 *   - MySQL 连接管理（初始化、断开、状态检查）
 *   - 建表（tags/books/book_tags/scan_logs/root_folders/app_config）
 *   - 标签 CRUD
 *   - 书籍 CRUD（单条 + 批量）
 *   - 元数据同步（收藏/进度/标签关联）
 *   - 扫描日志记录
 *   - 根目录和应用配置持久化
 *
 * 线程安全：
 *   所有公开方法通过 m_mutex 互斥锁保护，支持多线程调用。
 *   内部方法（带 Internal 后缀）不加锁，供已在锁内的方法调用，避免死锁。
 *
 * ⚠️ 依赖：
 *   - Qt SQL 模块（QT += sql）
 *   - MySQL 驱动（qsqlmysql，需要 libmysql/libmariadb 运行时库）
 *
 * @version 3.0
 */

#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QMutex>
#include <QMap>
#include <QList>
#include <QDebug>
#include "bookscanner.h"
#include "metadata.h"

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager& instance() {
        static DatabaseManager inst;
        return inst;
    }

    // ====== 连接管理 ======

    /**
     * @brief 初始化 MySQL 连接
     *
     * 流程：
     *   1. 清理旧连接（释放 QSqlDatabase 引用 + removeDatabase）
     *   2. 创建新连接（QMYSQL 驱动，连接名 "book_manager"）
     *   3. 设置 MYSQL_OPT_RECONNECT 自动重连
     *   4. 打开连接 → 建表
     *
     * ⚠️ 锁管理：成功时在函数末尾 unlock()，失败时在 emit 前 unlock()
     *    （避免信号处理函数中再次调用 DatabaseManager 方法时死锁）
     *
     * @return true 连接成功并建表完成
     */
    bool initialize(const QString& host,
        int port,
        const QString& dbName,
        const QString& user,
        const QString& password) {
        m_mutex.lock();

        // 清理旧连接，避免连接名冲突和资源泄漏
        if (m_db.isValid()) {
            if (m_db.isOpen()) m_db.close();
            QString connName = m_db.connectionName();
            m_db = QSqlDatabase(); // 释放旧引用
            QSqlDatabase::removeDatabase(connName);
        }

        m_db = QSqlDatabase::addDatabase("QMYSQL", "book_manager");
        m_db.setHostName(host);
        m_db.setPort(port);
        m_db.setDatabaseName(dbName);
        m_db.setUserName(user);
        m_db.setPassword(password);
        m_db.setConnectOptions("MYSQL_OPT_RECONNECT=1");  // 断线自动重连

        if (!m_db.open()) {
            QString errMsg = m_db.lastError().text();
            qWarning() << "MySQL连接失败:" << errMsg;
            m_mutex.unlock(); // 在发射信号前释放锁，避免死锁
            emit connectionError(errMsg);
            return false;
        }

        qDebug() << "MySQL连接成功:" << host << ":" << port << "/" << dbName;
        createTables();
        m_mutex.unlock();
        return true;
    }

    bool isConnected() const {
        return m_db.isValid() && m_db.isOpen();
    }

    void disconnect() {
        QMutexLocker lock(&m_mutex);
        if (m_db.isOpen()) {
            m_db.close();
        }
    }

    QString lastError() const {
        return m_db.lastError().text();
    }

    // ====== 建表 ======

    /**
     * @brief 创建所有数据表（IF NOT EXISTS）
     *
     * 表结构：
     *   - tags:         标签定义（name UNIQUE + color）
     *   - books:        书籍文件信息 + 收藏/进度（file_path UNIQUE）
     *   - book_tags:    书籍-标签多对多关联（CASCADE 删除）
     *   - scan_logs:    扫描历史记录
     *   - root_folders: 根目录配置
     *   - app_config:   键值对应用配置
     *
     * 所有表使用 InnoDB + utf8mb4 字符集
     */
    void createTables() {
        QSqlQuery query(m_db);

        // --- 标签表 ---
        query.exec(R"(
            CREATE TABLE IF NOT EXISTS tags (
                id          INT AUTO_INCREMENT PRIMARY KEY,
                name        VARCHAR(100) NOT NULL UNIQUE,
                color       VARCHAR(20) NOT NULL DEFAULT '#3498db',
                created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_tag_name (name)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");
        if (query.lastError().isValid()) qWarning() << "建表 tags 错误:" << query.lastError().text();

        // --- 书籍文件表 ---
        query.exec(R"(
            CREATE TABLE IF NOT EXISTS books (
                id              INT AUTO_INCREMENT PRIMARY KEY,
                file_name       VARCHAR(500) NOT NULL,
                file_path       VARCHAR(1000) NOT NULL UNIQUE,
                folder          VARCHAR(500) DEFAULT '(根目录)',
                file_size       BIGINT DEFAULT 0,
                date_modified   DATETIME,
                is_readonly     TINYINT(1) DEFAULT 0,
                starred         TINYINT(1) DEFAULT 0,
                progress_type   VARCHAR(20) DEFAULT NULL,
                progress_value  INT DEFAULT 0,
                progress_total  INT DEFAULT 0,
                progress_note   VARCHAR(500) DEFAULT '',
                created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
                updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                INDEX idx_file_name (file_name),
                INDEX idx_folder (folder),
                INDEX idx_starred (starred),
                INDEX idx_date_modified (date_modified)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");
        if (query.lastError().isValid()) qWarning() << "建表 books 错误:" << query.lastError().text();

        // --- 书籍-标签关联表（多对多） ---
        query.exec(R"(
            CREATE TABLE IF NOT EXISTS book_tags (
                book_id     INT NOT NULL,
                tag_id      INT NOT NULL,
                PRIMARY KEY (book_id, tag_id),
                FOREIGN KEY (book_id) REFERENCES books(id) ON DELETE CASCADE,
                FOREIGN KEY (tag_id)  REFERENCES tags(id)  ON DELETE CASCADE,
                INDEX idx_book_id (book_id),
                INDEX idx_tag_id (tag_id)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");
        if (query.lastError().isValid()) qWarning() << "建表 book_tags 错误:" << query.lastError().text();

        // --- 扫描记录表 ---
        query.exec(R"(
            CREATE TABLE IF NOT EXISTS scan_logs (
                id              INT AUTO_INCREMENT PRIMARY KEY,
                scan_time       DATETIME DEFAULT CURRENT_TIMESTAMP,
                root_folders    TEXT,
                total_files     INT DEFAULT 0,
                total_size      BIGINT DEFAULT 0,
                duration_ms     INT DEFAULT 0,
                status          VARCHAR(20) DEFAULT 'success',
                message         TEXT
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");
        if (query.lastError().isValid()) qWarning() << "建表 scan_logs 错误:" << query.lastError().text();

        // --- 根目录配置表 ---
        query.exec(R"(
            CREATE TABLE IF NOT EXISTS root_folders (
                id          INT AUTO_INCREMENT PRIMARY KEY,
                folder_path VARCHAR(1000) NOT NULL UNIQUE,
                added_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
                is_active   TINYINT(1) DEFAULT 1
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");
        if (query.lastError().isValid()) qWarning() << "建表 root_folders 错误:" << query.lastError().text();

        // --- 用户配置表（键值对） ---
        query.exec(R"(
            CREATE TABLE IF NOT EXISTS app_config (
                config_key   VARCHAR(100) PRIMARY KEY,
                config_value TEXT,
                updated_at   DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");
        if (query.lastError().isValid()) qWarning() << "建表 app_config 错误:" << query.lastError().text();
    }

    // ====== 标签 CRUD ======

    /**
     * @brief 内部版插入标签（不加锁）
     * 供已在 m_mutex 锁内的方法调用（如 syncBookTags、syncAllMetadata）
     * @return 标签 ID，失败返回 -1
     */
    int insertTagInternal(const QString& name, const QColor& color) {
        QSqlQuery query(m_db);
        query.prepare("INSERT INTO tags (name, color) VALUES (:name, :color) "
            "ON DUPLICATE KEY UPDATE color = :color2");
        query.bindValue(":name", name);
        query.bindValue(":color", color.name());
        query.bindValue(":color2", color.name());
        if (!query.exec()) {
            qWarning() << "插入标签失败:" << query.lastError().text();
            return -1;
        }
        // 获取标签 ID（支持 INSERT 和 ON DUPLICATE KEY UPDATE 场景）
        QSqlQuery idQuery(m_db);
        idQuery.prepare("SELECT id FROM tags WHERE name = :name");
        idQuery.bindValue(":name", name);
        if (idQuery.exec() && idQuery.next()) {
            return idQuery.value(0).toInt();
        }
        return -1;
    }

    int insertTag(const QString& name, const QColor& color) {
        QMutexLocker lock(&m_mutex);
        return insertTagInternal(name, color);
    }

    QList<TagInfo> getAllTags() {
        QMutexLocker lock(&m_mutex);
        QList<TagInfo> tags;
        QSqlQuery query(m_db);
        query.exec("SELECT name, color FROM tags ORDER BY name");
        while (query.next()) {
            TagInfo t;
            t.name = query.value(0).toString();
            t.color = QColor(query.value(1).toString());
            tags.append(t);
        }
        return tags;
    }

    bool deleteTag(const QString& name) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare("DELETE FROM tags WHERE name = :name");
        query.bindValue(":name", name);
        return query.exec();
    }

    QStringList allTagNames() {
        QMutexLocker lock(&m_mutex);
        QStringList names;
        QSqlQuery query(m_db);
        query.exec("SELECT DISTINCT name FROM tags ORDER BY name");
        while (query.next()) {
            names << query.value(0).toString();
        }
        return names;
    }

    // ====== 书籍 CRUD ======

    /**
     * @brief 插入单条书籍记录
     * 使用 ON DUPLICATE KEY UPDATE 实现 upsert 语义
     * @return 书籍 ID，失败返回 -1
     */
    int insertBook(const BookInfo& book) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT INTO books (file_name, file_path, folder, file_size, date_modified, is_readonly)
            VALUES (:name, :path, :folder, :size, :modified, :readonly)
            ON DUPLICATE KEY UPDATE
                file_name = :name2, folder = :folder2, file_size = :size2,
                date_modified = :modified2, is_readonly = :readonly2, updated_at = NOW()
        )");
        query.bindValue(":name", book.name);
        query.bindValue(":path", book.path);
        query.bindValue(":folder", book.folder);
        query.bindValue(":size", book.size);
        query.bindValue(":modified", book.dateModified);
        query.bindValue(":readonly", book.isReadOnly ? 1 : 0);
        query.bindValue(":name2", book.name);
        query.bindValue(":folder2", book.folder);
        query.bindValue(":size2", book.size);
        query.bindValue(":modified2", book.dateModified);
        query.bindValue(":readonly2", book.isReadOnly ? 1 : 0);
        if (!query.exec()) {
            qWarning() << "插入书籍失败:" << query.lastError().text();
            return -1;
        }
        // 获取 id
        QSqlQuery idQuery(m_db);
        idQuery.prepare("SELECT id FROM books WHERE file_path = :path");
        idQuery.bindValue(":path", book.path);
        if (idQuery.exec() && idQuery.next()) {
            return idQuery.value(0).toInt();
        }
        return -1;
    }

    /**
     * @brief 批量插入书籍（事务包裹）
     *
     * 使用 VALUES() 函数引用 INSERT 值，避免重复绑定参数。
     * 任一记录失败则整体回滚。
     *
     * ⚠️ 注意：批量插入不处理元数据（starred/progress/tags），
     *    需后续调用 syncAllMetadata 同步。
     */
    bool batchInsertBooks(const QList<BookInfo>& books) {
        if (books.isEmpty()) return true;

        QMutexLocker lock(&m_mutex);
        m_db.transaction();

        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT INTO books (file_name, file_path, folder, file_size, date_modified, is_readonly)
            VALUES (:name, :path, :folder, :size, :modified, :readonly)
            ON DUPLICATE KEY UPDATE
                file_name = VALUES(file_name), folder = VALUES(folder),
                file_size = VALUES(file_size), date_modified = VALUES(date_modified),
                is_readonly = VALUES(is_readonly), updated_at = NOW()
        )");

        for (const auto& book : books) {
            query.bindValue(":name", book.name);
            query.bindValue(":path", book.path);
            query.bindValue(":folder", book.folder);
            query.bindValue(":size", book.size);
            query.bindValue(":modified", book.dateModified);
            query.bindValue(":readonly", book.isReadOnly ? 1 : 0);
            if (!query.exec()) {
                qWarning() << "批量插入失败:" << query.lastError().text();
                m_db.rollback();
                return false;
            }
        }

        m_db.commit();
        return true;
    }

    QList<BookInfo> getAllBooks() {
        QMutexLocker lock(&m_mutex);
        QList<BookInfo> books;
        QSqlQuery query(m_db);
        query.exec("SELECT file_name, file_path, folder, file_size, date_modified, is_readonly "
            "FROM books ORDER BY file_name");
        while (query.next()) {
            BookInfo book;
            book.name = query.value(0).toString();
            book.path = query.value(1).toString();
            book.folder = query.value(2).toString();
            book.size = query.value(3).toLongLong();
            book.dateModified = query.value(4).toDateTime();
            book.isReadOnly = query.value(5).toBool();
            books.append(book);
        }
        return books;
    }

    bool removeBook(const QString& path) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare("DELETE FROM books WHERE file_path = :path");
        query.bindValue(":path", path);
        return query.exec();
    }

    bool renameBook(const QString& oldPath, const QString& newPath,
        const QString& newName) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare("UPDATE books SET file_path = :newPath, file_name = :newName "
            "WHERE file_path = :oldPath");
        query.bindValue(":newPath", newPath);
        query.bindValue(":newName", newName);
        query.bindValue(":oldPath", oldPath);
        return query.exec();
    }

    int bookCount() {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.exec("SELECT COUNT(*) FROM books");
        if (query.next()) return query.value(0).toInt();
        return 0;
    }

    // ====== 元数据 CRUD（收藏/进度） ======

    bool updateStarred(const QString& path, bool starred) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare("UPDATE books SET starred = :starred WHERE file_path = :path");
        query.bindValue(":starred", starred ? 1 : 0);
        query.bindValue(":path", path);
        return query.exec();
    }

    /**
     * @brief 更新阅读进度
     * ⚠️ 进度为空时，progress_type 设为 NULL（而非空字符串）
     */
    bool updateProgress(const QString& path, const ProgressInfo& progress) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(R"(
            UPDATE books SET progress_type = :type, progress_value = :value,
                progress_total = :total, progress_note = :note
            WHERE file_path = :path
        )");
        if (progress.isValid())
            query.bindValue(":type", progress.type);
        else
            query.bindValue(":type", QVariant(QMetaType::fromType<QString>()));
        query.bindValue(":value", progress.value);
        query.bindValue(":total", progress.total);
        query.bindValue(":note", progress.note);
        query.bindValue(":path", path);
        return query.exec();
    }

    // ====== 书籍-标签关联 ======

    /**
     * @brief 为书籍添加标签关联
     * 使用 INSERT IGNORE 防止重复关联报错
     */
    bool addTagToBook(const QString& bookPath, const QString& tagName) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT IGNORE INTO book_tags (book_id, tag_id)
            SELECT b.id, t.id FROM books b, tags t
            WHERE b.file_path = :path AND t.name = :tag
        )");
        query.bindValue(":path", bookPath);
        query.bindValue(":tag", tagName);
        return query.exec();
    }

    bool removeTagFromBook(const QString& bookPath, const QString& tagName) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(R"(
            DELETE bt FROM book_tags bt
            INNER JOIN books b ON bt.book_id = b.id
            INNER JOIN tags t ON bt.tag_id = t.id
            WHERE b.file_path = :path AND t.name = :tag
        )");
        query.bindValue(":path", bookPath);
        query.bindValue(":tag", tagName);
        return query.exec();
    }

    QList<TagInfo> getBookTags(const QString& bookPath) {
        QMutexLocker lock(&m_mutex);
        QList<TagInfo> tags;
        QSqlQuery query(m_db);
        query.prepare(R"(
            SELECT t.name, t.color FROM tags t
            INNER JOIN book_tags bt ON t.id = bt.tag_id
            INNER JOIN books b ON bt.book_id = b.id
            WHERE b.file_path = :path
        )");
        query.bindValue(":path", bookPath);
        query.exec();
        while (query.next()) {
            TagInfo tag;
            tag.name = query.value(0).toString();
            tag.color = QColor(query.value(1).toString());
            tags.append(tag);
        }
        return tags;
    }

    /**
     * @brief 同步书籍标签（删除旧关联 + 插入新关联）
     * 在事务内执行，确保原子性
     */
    bool syncBookTags(const QString& bookPath, const QList<TagInfo>& tags) {
        QMutexLocker lock(&m_mutex);
        m_db.transaction();

        // 先删除该书所有标签关联
        QSqlQuery delQuery(m_db);
        delQuery.prepare(R"(
            DELETE bt FROM book_tags bt
            INNER JOIN books b ON bt.book_id = b.id
            WHERE b.file_path = :path
        )");
        delQuery.bindValue(":path", bookPath);
        delQuery.exec();

        // 插入新的关联
        QSqlQuery insQuery(m_db);
        insQuery.prepare(R"(
            INSERT IGNORE INTO book_tags (book_id, tag_id)
            SELECT b.id, t.id FROM books b, tags t
            WHERE b.file_path = :path AND t.name = :tag
        )");

        for (const auto& tag : tags) {
            // 确保标签存在（使用不加锁版本，当前已持有 m_mutex）
            insertTagInternal(tag.name, tag.color);
            insQuery.bindValue(":path", bookPath);
            insQuery.bindValue(":tag", tag.name);
            insQuery.exec();
        }

        m_db.commit();
        return true;
    }

    // ====== 全量同步元数据（JSON → MySQL） ======

    /**
     * @brief 将本地 MetadataStore 的所有元数据同步到 MySQL
     *
     * 流程（在单个事务内）：
     *   遍历所有 (path, meta) 对：
     *     1. UPDATE books SET starred, progress...
     *     2. DELETE FROM book_tags WHERE book_path = path（清除旧关联）
     *     3. INSERT INTO book_tags（建立新关联）
     *
     * ⚠️ 性能注意：对于大量元数据（>10000 条），事务可能很大。
     *    可考虑分批提交（每 1000 条 commit 一次）。
     *
     * ⚠️ 数据一致性：UPDATE 只影响 books 表中已有的记录。
     *    如果 book 尚未插入（未扫描过），UPDATE 无效果但不报错。
     */
    void syncAllMetadata(const QMap<QString, BookMeta>& allMeta) {
        QMutexLocker lock(&m_mutex);
        m_db.transaction();

        for (auto it = allMeta.begin(); it != allMeta.end(); ++it) {
            const QString& path = it.key();
            const BookMeta& meta = it.value();

            // 更新收藏和进度
            QSqlQuery q(m_db);
            q.prepare(R"(
                UPDATE books SET starred = :starred,
                    progress_type = :ptype, progress_value = :pvalue,
                    progress_total = :ptotal, progress_note = :pnote
                WHERE file_path = :path
            )");
            q.bindValue(":starred", meta.starred ? 1 : 0);
            if (meta.progress.isValid())
                q.bindValue(":ptype", meta.progress.type);
            else
                q.bindValue(":ptype", QVariant(QMetaType::fromType<QString>()));
            q.bindValue(":pvalue", meta.progress.value);
            q.bindValue(":ptotal", meta.progress.total);
            q.bindValue(":pnote", meta.progress.note);
            q.bindValue(":path", path);
            q.exec();

            // 删除旧的标签关联
            QSqlQuery del(m_db);
            del.prepare(R"(
                DELETE bt FROM book_tags bt
                INNER JOIN books b ON bt.book_id = b.id
                WHERE b.file_path = :path
            )");
            del.bindValue(":path", path);
            del.exec();

            // 插入新的标签关联
            for (const auto& tag : meta.tags) {
                int tagId = insertTagInternal(tag.name, tag.color); // 不加锁版本，避免死锁
                if (tagId < 0) continue; // 插入标签失败，跳过此关联
                QSqlQuery ins(m_db);
                ins.prepare(R"(
                    INSERT IGNORE INTO book_tags (book_id, tag_id)
                    SELECT b.id, t.id FROM books b, tags t
                    WHERE b.file_path = :path AND t.name = :tag
                )");
                ins.bindValue(":path", path);
                ins.bindValue(":tag", tag.name);
                ins.exec();
            }
        }

        m_db.commit();
    }

    // ====== 从 MySQL 加载完整元数据 ======

    /**
     * @brief 从 MySQL 加载所有元数据
     * 返回的 QMap 可直接替换 MetadataStore 的内容
     *
     * ⚠️ 注意：加载结果中可能包含 books 表中所有记录，
     *    包括那些在 MetadataStore 中没有的路径。
     *    MainWindow::loadMetadataFromDB 会 clear() 后完全替换。
     */
    QMap<QString, BookMeta> loadAllMetadata() {
        QMutexLocker lock(&m_mutex);
        QMap<QString, BookMeta> result;

        // 加载所有书籍的收藏和进度
        QSqlQuery query(m_db);
        query.exec(R"(
            SELECT file_path, starred, progress_type, progress_value, progress_total, progress_note
            FROM books
        )");
        while (query.next()) {
            QString path = query.value(0).toString();
            BookMeta meta;
            meta.starred = query.value(1).toBool();
            QString ptype = query.value(2).toString();
            if (!ptype.isEmpty()) {
                meta.progress.type = ptype;
                meta.progress.value = query.value(3).toInt();
                meta.progress.total = query.value(4).toInt();
                meta.progress.note = query.value(5).toString();
            }
            result[path] = meta;
        }

        // 加载所有标签关联
        QSqlQuery tagQuery(m_db);
        tagQuery.exec(R"(
            SELECT b.file_path, t.name, t.color
            FROM book_tags bt
            INNER JOIN books b ON bt.book_id = b.id
            INNER JOIN tags t ON bt.tag_id = t.id
        )");
        while (tagQuery.next()) {
            QString path = tagQuery.value(0).toString();
            TagInfo tag;
            tag.name = tagQuery.value(1).toString();
            tag.color = QColor(tagQuery.value(2).toString());
            if (!result.contains(path)) result[path] = BookMeta();
            result[path].tags.append(tag);
        }

        return result;
    }

    // ====== 扫描日志 ======

    void logScan(const QStringList& rootFolders, int totalFiles,
        qint64 totalSize, int durationMs,
        const QString& status = "success",
        const QString& message = "") {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT INTO scan_logs (root_folders, total_files, total_size, duration_ms, status, message)
            VALUES (:folders, :files, :size, :duration, :status, :message)
        )");
        query.bindValue(":folders", rootFolders.join(";"));
        query.bindValue(":files", totalFiles);
        query.bindValue(":size", totalSize);
        query.bindValue(":duration", durationMs);
        query.bindValue(":status", status);
        query.bindValue(":message", message);
        query.exec();
    }

    // ====== 根目录配置 ======

    /**
     * @brief 保存根目录到数据库
     * 先将所有旧记录标记为 inactive，再 upsert 当前活跃目录
     */
    bool saveRootFoldersToDB(const QStringList& folders) {
        QMutexLocker lock(&m_mutex);
        m_db.transaction();

        QSqlQuery clearQuery(m_db);
        clearQuery.exec("UPDATE root_folders SET is_active = 0");

        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT INTO root_folders (folder_path, is_active)
            VALUES (:path, 1)
            ON DUPLICATE KEY UPDATE is_active = 1, added_at = NOW()
        )");

        for (const auto& f : folders) {
            query.bindValue(":path", f);
            if (!query.exec()) {
                m_db.rollback();
                return false;
            }
        }

        m_db.commit();
        return true;
    }

    QStringList loadRootFoldersFromDB() {
        QMutexLocker lock(&m_mutex);
        QStringList folders;
        QSqlQuery query(m_db);
        query.exec("SELECT folder_path FROM root_folders WHERE is_active = 1 ORDER BY added_at");
        while (query.next()) {
            folders << query.value(0).toString();
        }
        return folders;
    }

    // ====== 应用配置（键值对） ======

    bool saveConfig(const QString& key, const QString& value) {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT INTO app_config (config_key, config_value)
            VALUES (:key, :value)
            ON DUPLICATE KEY UPDATE config_value = :value2
        )");
        query.bindValue(":key", key);
        query.bindValue(":value", value);
        query.bindValue(":value2", value);
        return query.exec();
    }

    QString loadConfig(const QString& key, const QString& defaultVal = "") {
        QMutexLocker lock(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare("SELECT config_value FROM app_config WHERE config_key = :key");
        query.bindValue(":key", key);
        if (query.exec() && query.next()) {
            return query.value(0).toString();
        }
        return defaultVal;
    }

signals:
    void connectionError(const QString& errorMsg);

private:
    /**
     * @brief 私有构造函数（单例模式）
     * ⚠️ 注意：QObject 子类的私有构造函数可能导致某些编译器警告
     */
    DatabaseManager(QObject* parent = nullptr) : QObject(parent) {}
    ~DatabaseManager() { disconnect(); }

    // 禁止拷贝（单例）
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    QSqlDatabase m_db;       ///< MySQL 数据库连接
    QMutex m_mutex;          ///< 操作互斥锁
};

#endif // DATABASEMANAGER_H
