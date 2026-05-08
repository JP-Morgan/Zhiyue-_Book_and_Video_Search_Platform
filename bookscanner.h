/**
 * @file bookscanner.h
 * @brief 后台目录扫描 — 自动选择最优扫描策略
 *
 * 策略逻辑：
 * - 单根目录 → 单线程（线程开销大于收益）
 * - 多根目录 → 探测磁盘类型
 *   ├─ SSD/NVMe/RAM → 多线程并行
 *   └─ HDD/未知     → 单线程顺序
 *
 * 通用优化（无论单/多线程都生效）：
 * - 单遍扫描（QDirIterator 流式遍历，去掉双遍）
 * - 排除用 QSet（O(1) 查找）
 *
 * @version 4.0
 */

#ifndef BOOKSCANNER_H
#define BOOKSCANNER_H

#include <QObject>
#include <QThread>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QHash>
#include <QAtomicInt>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <fileapi.h>
#else
#include <QFile>
#include <QTextStream>
#endif

 // ======================== 数据结构 ========================

struct BookInfo {
    QString name;
    QString path;
    QString folder;
    qint64 size = 0;
    QDateTime dateModified;
    bool isReadOnly = false;
};

enum class DiskType { SSD, HDD, RAM, Unknown };

// ======================== 磁盘探测 ========================

class DiskDetector {
public:
    static DiskType detect(const QString& path) {
#ifdef Q_OS_WIN
        return detectWindows(path);
#else
        return detectLinux(path);
#endif
    }

    /**
     * @brief 判断是否应该启用多线程
     * @return true = 多线程有益
     */
    static bool shouldUseMultiThread(const QStringList& roots) {
        // 单根目录 → 不值得开线程
        if (roots.size() <= 1)
            return false;

        // 检查是否有 SSD/RAM
        for (const QString& root : roots) {
            DiskType dt = detect(root);
            if (dt == DiskType::SSD || dt == DiskType::RAM)
                return true;
        }

        // 全 HDD 或未知 → 单线程
        return false;
    }

private:
#ifdef Q_OS_LINUX
    static DiskType detectLinux(const QString& path) {
        // 检查 tmpfs
        if (isTmpfs(path))
            return DiskType::RAM;

        // 找块设备
        QString device = resolveBlockDevice(path);
        if (device.isEmpty())
            return DiskType::Unknown;

        // 读 rotational
        QString parent = stripPartition(device);
        QString sysPath = QStringLiteral("/sys/block/%1/queue/rotational")
            .arg(QFileInfo(parent).fileName());

        QFile f(sysPath);
        if (!f.open(QIODevice::ReadOnly))
            return DiskType::Unknown;

        QString val = f.readAll().trimmed();
        if (val == "0") return DiskType::SSD;
        if (val == "1") return DiskType::HDD;
        return DiskType::Unknown;
    }

    static QString resolveBlockDevice(const QString& path) {
        QFile mounts(QStringLiteral("/proc/self/mountinfo"));
        if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};

        QString bestMatch;
        QString bestDevice;
        QTextStream in(&mounts);

        while (!in.atEnd()) {
            QStringList parts = in.readLine().split(' ');
            if (parts.size() < 10) continue;

            int dashIdx = parts.indexOf("-");
            if (dashIdx < 0 || dashIdx + 2 >= parts.size()) continue;

            QString mountPoint = parts[4];
            QString device = parts[dashIdx + 2];

            if (path.startsWith(mountPoint) &&
                mountPoint.size() > bestMatch.size()) {
                bestMatch = mountPoint;
                bestDevice = device;
            }
        }
        return bestDevice;
    }

    static bool isTmpfs(const QString& path) {
        QFile mounts(QStringLiteral("/proc/mounts"));
        if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;

        QTextStream in(&mounts);
        while (!in.atEnd()) {
            QStringList parts = in.readLine().split(' ');
            if (parts.size() < 3) continue;
            if (parts[1] == path || path.startsWith(parts[1] + "/")) {
                if (parts[2] == "tmpfs" || parts[2] == "ramfs")
                    return true;
            }
        }
        return false;
    }

    static QString stripPartition(const QString& device) {
        QFileInfo fi(device);
        QString name = fi.fileName();

        static QRegularExpression nvmeRe("^(nvme\\d+n\\d+)p\\d+$");
        auto m = nvmeRe.match(name);
        if (m.hasMatch())
            return fi.absolutePath() + "/" + m.captured(1);

        static QRegularExpression sataRe("^(sd[a-z]+|hd[a-z]+|vd[a-z]+)\\d+$");
        m = sataRe.match(name);
        if (m.hasMatch())
            return fi.absolutePath() + "/" + m.captured(1);

        return device;
    }
#endif

#ifdef Q_OS_WIN
    static DiskType detectWindows(const QString& path) {
        wchar_t volume[MAX_PATH];
        if (!GetVolumePathNameW(
            reinterpret_cast<LPCWSTR>(path.utf16()),
            volume, MAX_PATH))
            return DiskType::Unknown;

        QString volPath = QString::fromWCharArray(volume);
        if (volPath.endsWith('\\'))
            volPath.chop(1);

        HANDLE hDevice = CreateFileW(
            reinterpret_cast<LPCWSTR>((volPath + ":").utf16()),
            0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

        if (hDevice == INVALID_HANDLE_VALUE)
            return DiskType::Unknown;

        // seek penalty
        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        query.QueryType = PropertyStandardQuery;

        DEVICE_SEEK_PENALTY_DESCRIPTOR seekPenalty{};
        DWORD bytesReturned = 0;

        BOOL ok = DeviceIoControl(
            hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
            &query, sizeof(query),
            &seekPenalty, sizeof(seekPenalty),
            &bytesReturned, nullptr);

        if (ok && seekPenalty.IncursSeekPenalty) {
            CloseHandle(hDevice);
            return DiskType::HDD;
        }

        // TRIM
        STORAGE_PROPERTY_QUERY trimQuery{};
        trimQuery.PropertyId = StorageDeviceTrimProperty;
        trimQuery.QueryType = PropertyStandardQuery;

        DEVICE_TRIM_DESCRIPTOR trimDesc{};
        ok = DeviceIoControl(
            hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
            &trimQuery, sizeof(trimQuery),
            &trimDesc, sizeof(trimDesc),
            &bytesReturned, nullptr);

        CloseHandle(hDevice);

        if (ok && trimDesc.TrimEnabled)
            return DiskType::SSD;

        return DiskType::Unknown;
    }
#endif
};

// ======================== 扫描器 ========================

class BookScanner : public QThread {
    Q_OBJECT
public:
    explicit BookScanner(QObject* parent = nullptr)
        : QThread(parent) {
    }

    void setRoots(const QStringList& roots) { m_roots = roots; }
    void setExtensions(const QStringList& exts) { m_extensions = exts; }
    void setExcludes(const QStringList& excl) { m_excludes = excl; }

    QList<BookInfo> results() const { return m_results; }

signals:
    void progressUpdated(int processed, int total);
    void scanFinished();

protected:
    void run() override {
        m_results.clear();

        // 构建扩展名集合
        QSet<QString> extSet;
        extSet.reserve(m_extensions.size());
        for (const auto& e : m_extensions)
            extSet.insert(e.toLower());

        // 构建排除集合
        QSet<QString> excludeSet;
        excludeSet.reserve(m_excludes.size());
        for (const auto& e : m_excludes)
            excludeSet.insert(e.toLower());

        // 过滤不存在的目录
        QStringList validRoots;
        for (const QString& root : m_roots) {
            if (QDir(root).exists())
                validRoots.append(root);
        }
        if (validRoots.isEmpty()) {
            emit scanFinished();
            return;
        }

        QAtomicInt processedCount{ 0 };

        // ===== 选择策略 =====
        bool useMultiThread = DiskDetector::shouldUseMultiThread(validRoots);

        if (useMultiThread) {
            qInfo() << "扫描策略: 多线程并行扫描";
            scanMultiThread(validRoots, extSet, excludeSet, processedCount);
        }
        else {
            qInfo() << "扫描策略: 单线程顺序扫描";
            scanSingleThread(validRoots, extSet, excludeSet, processedCount);
        }

        emit scanFinished();
    }

private:
    // ==================== 单线程 ====================
    void scanSingleThread(const QStringList& roots,
        const QSet<QString>& extSet,
        const QSet<QString>& excludeSet,
        QAtomicInt& processedCount) {
        m_results.reserve(10240);

        for (const QString& root : roots) {
            if (isInterruptionRequested()) return;

            QDirIterator dirIt(root,
                QDir::Files | QDir::Readable,
                QDirIterator::Subdirectories);

            while (dirIt.hasNext()) {
                if (isInterruptionRequested()) return;
                dirIt.next();

                const QFileInfo& fi = dirIt.fileInfo();

                // 排除检查
                if (shouldExclude(fi.absolutePath(), excludeSet))
                    continue;

                // 扩展名过滤
                if (!extSet.isEmpty()) {
                    QString ext = fi.suffix();
                    if (ext.isEmpty() ||
                        !extSet.contains('.' + ext.toLower()))
                        continue;
                }

                // 构建结果
                BookInfo book;
                book.name = fi.fileName();
                book.path = fi.absoluteFilePath();
                book.size = fi.size();
                book.dateModified = fi.lastModified();
                book.isReadOnly = !fi.isWritable();

                QString rel = fi.absolutePath();
                rel = rel.mid(root.length());
                book.folder = rel.isEmpty() ? QStringLiteral("(根目录)")
                    : rel;

                m_results.append(book);

                int count = processedCount.fetchAndAddOrdered(1) + 1;
                if (count % 200 == 0)
                    emit progressUpdated(count, 0);
            }
        }
    }

    // ==================== 多线程 ====================
    void scanMultiThread(const QStringList& roots,
        const QSet<QString>& extSet,
        const QSet<QString>& excludeSet,
        QAtomicInt& processedCount) {
        QMutex resultMutex;

        int maxThreads = qBound(2, QThread::idealThreadCount(), 8);
        QThreadPool pool;
        pool.setMaxThreadCount(maxThreads);

        QList<QFuture<void>> futures;

        for (const QString& root : roots) {
            if (isInterruptionRequested()) return;

            futures.append(QtConcurrent::run(&pool,
                [=, &extSet, &excludeSet, &processedCount, &resultMutex]() {

                    if (isInterruptionRequested()) return;

                    QList<BookInfo> local;
                    local.reserve(2048);

                    QDirIterator dirIt(root,
                        QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);

                    while (dirIt.hasNext()) {
                        if (isInterruptionRequested()) return;
                        dirIt.next();

                        const QFileInfo& fi = dirIt.fileInfo();

                        if (shouldExclude(fi.absolutePath(), excludeSet))
                            continue;

                        if (!extSet.isEmpty()) {
                            QString ext = fi.suffix();
                            if (ext.isEmpty() ||
                                !extSet.contains('.' + ext.toLower()))
                                continue;
                        }

                        BookInfo book;
                        book.name = fi.fileName();
                        book.path = fi.absoluteFilePath();
                        book.size = fi.size();
                        book.dateModified = fi.lastModified();
                        book.isReadOnly = !fi.isWritable();

                        QString rel = fi.absolutePath();
                        rel = rel.mid(root.length());
                        book.folder = rel.isEmpty()
                            ? QStringLiteral("(根目录)") : rel;

                        local.append(book);

                        int count = processedCount.fetchAndAddOrdered(1) + 1;
                        if (count % 200 == 0)
                            emit progressUpdated(count, 0);
                    }

                    // 局部结果一次性合并
                    QMutexLocker lock(&resultMutex);
                    m_results.append(local);
                }));
        }

        // 等待全部完成
        for (auto& f : futures) {
            if (isInterruptionRequested()) {
                pool.clear();
                break;
            }
            f.waitForFinished();
        }
    }

    // ==================== 通用 ====================

    static bool shouldExclude(const QString& path,
        const QSet<QString>& excludeSet) {
        static const QChar slash = '/';
        static const QChar backslash = '\\';
        int start = 0;
        while (start < path.size()) {
            int end = start;
            while (end < path.size() &&
                path[end] != slash && path[end] != backslash)
                end++;
            if (end > start) {
                QString seg = path.mid(start, end - start).toLower();
                if (seg.startsWith('.')) return true;
                if (excludeSet.contains(seg)) return true;
            }
            start = end + 1;
        }
        return false;
    }

    QStringList m_roots;
    QStringList m_extensions;
    QStringList m_excludes;
    QList<BookInfo> m_results;
};

#endif // BOOKSCANNER_H
