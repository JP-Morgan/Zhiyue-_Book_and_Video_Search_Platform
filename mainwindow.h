/**
 * @file mainwindow.h
 * @brief 主窗口类定义 — 知阅·书籍视频检索平台 v3.0
 *
 * 功能概要：
 *   - 多目录书籍/视频文件扫描与索引
 *   - 按名称/大小/日期/标签/收藏多维筛选
 *   - 标签管理、阅读进度追踪、收藏标记
 *   - CSV/TXT/剪贴板导出
 *   - MySQL 数据库双写同步（可选）
 *   - 批量重命名、统计分析
 *
 * @author 知阅团队
 * @version 3.0
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableView>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QSplitter>
#include <QGroupBox>
#include <QSet>
#include <QMap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QScrollArea>
#include <QKeyEvent>
#include <QCloseEvent>
#include "bookmodel.h"
#include "bookscanner.h"
#include "metadata.h"

 /**
  * @class MainWindow
  * @brief 应用程序主窗口，管理 UI 布局、扫描线程、元数据持久化和数据库同步
  *
  * 生命周期：
  *   1. 构造时加载 JSON 配置 → 加载 JSON 元数据 → 初始化 MySQL（可选）→ 构建 UI
  *   2. 运行时通过扫描线程更新书籍列表，用户操作修改元数据
  *   3. 关闭时：保存 JSON 配置 → 保存 JSON 元数据 → flush 脏数据到 MySQL
  */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void keyPressEvent(QKeyEvent* event) override;   ///< 键盘快捷键处理
    void closeEvent(QCloseEvent* event) override;     ///< 关闭前保存数据

private slots:
    // ===== 根目录管理 =====
    void addRootFolder();                           ///< 添加新的扫描根目录
    void removeRootFolder(const QString& path);     ///< 移除已添加的根目录

    // ===== 扫描相关 =====
    void startScan();                               ///< 启动后台扫描线程
    void onScanProgress(int current, int total);    ///< 扫描进度回调
    void onScanFinished();                          ///< 扫描完成回调，更新模型和数据库

    // ===== 搜索与筛选 =====
    void onSearchTextChanged();                     ///< 搜索框文字变化（带 200ms 防抖）
    void applyFilter();                             ///< 应用所有筛选条件
    void onFolderFilterChanged();                   ///< 文件夹复选框变化回调

    // ===== 选择操作 =====
    void selectAllVisible();                        ///< 全选/取消全选可见行
    void deleteSelected();                          ///< 删除选中文件（物理删除）

    // ===== 文件操作 =====
    void openFile(const QModelIndex& index);        ///< 双击打开文件
    void openContainingFolder(const QString& path); ///< 打开文件所在目录

    // ===== 右键菜单 =====
    void showContextMenu(const QPoint& pos);        ///< 显示右键上下文菜单
    void showFileDetail(const QString& path);       ///< 显示文件详情面板

    // ===== 元数据编辑 =====
    void editTagsForPath(const QString& path);      ///< 编辑文件标签
    void editProgressForPath(const QString& path);  ///< 编辑阅读进度
    void toggleStarForPath(const QString& path);    ///< 切换收藏状态

    // ===== 功能面板 =====
    void openBatchRename();                         ///< 打开批量重命名对话框
    void showStats();                               ///< 打开统计面板
    void openConfig();                              ///< 打开设置面板
    void openDBConfig();                            ///< 打开数据库配置对话框

    // ===== 导出功能 =====
    void exportCSV();                               ///< 导出为 CSV 文件
    void exportTXT();                               ///< 导出为 TXT 文件
    void exportClipboard();                         ///< 复制到剪贴板
    void importConfig();                            ///< 导入配置 JSON

    // ===== 数据管理 =====
    void clearAllData();                            ///< 清除所有配置和元数据

    // ===== 表头排序 =====
    void onHeaderClicked(int logicalIndex);         ///< 点击表头列排序

    // ===== 文件夹面板 =====
    void selectAllFolders();                        ///< 全选文件夹筛选
    void deselectAllFolders();                      ///< 取消全选文件夹
    void invertFolderSelection();                   ///< 反选文件夹

private:
    // ===== UI 初始化 =====
    void setupUI();                ///< 构建完整 UI 布局
    void setupConnections();       ///< （已内联在 setupUI 中，预留接口）
    void renderRootTags();         ///< 渲染根目录标签栏

    // ===== 数据刷新 =====
    void populateFolderList();     ///< 刷新左侧面板文件夹列表
    void updateStats();            ///< 更新统计信息标签
    void updateTagFilterOptions(); ///< 刷新标签下拉框选项
    void updateDBStatusLabel();    ///< 更新底部数据库连接状态

    // ===== 配置与元数据持久化 =====
    void loadConfig();             ///< 从 JSON 文件加载配置
    void saveConfig();             ///< 保存配置到 JSON 文件
    void loadMetadata();           ///< 从 JSON 文件加载元数据
    void saveMetadata();           ///< 保存元数据到 JSON 文件，标记 m_metadataDirty
    QString configPath() const;    ///< 配置文件路径
    QString metaPath() const;      ///< 元数据文件路径

    // ===== MySQL 数据库集成 =====
    void initDatabase();           ///< 初始化 MySQL 连接（如果启用）
    void syncMetadataToDB();       ///< 将本地元数据全量同步到 MySQL
    void loadMetadataFromDB();     ///< 从 MySQL 加载元数据覆盖本地
    void flushMetadataToDB();      ///< 仅在 m_metadataDirty=true 时触发同步

    // ========== UI 控件 ==========

    QWidget* m_centralWidget;            ///< 中央容器
    QVBoxLayout* m_mainLayout;           ///< 主垂直布局
    QLabel* m_headerLabel;               ///< 顶部标题标签

    // 根目录标签栏
    QWidget* m_rootBar;                  ///< 根目录标签容器
    QHBoxLayout* m_rootBarLayout;        ///< 根目录水平布局

    // 统计信息
    QLabel* m_statsLabel;                ///< 统计信息展示标签

    // 搜索与筛选控件
    QLineEdit* m_searchBox;              ///< 搜索框（支持正则）
    QComboBox* m_filterType;             ///< 文件类型下拉筛选
    QComboBox* m_filterTag;              ///< 标签下拉筛选
    QComboBox* m_filterStar;             ///< 收藏状态下拉筛选
    QComboBox* m_sortBy;                 ///< 排序方式下拉
    QCheckBox* m_useRegex;               ///< 启用正则搜索复选框

    // 进度显示
    QProgressBar* m_progressBar;         ///< 扫描进度条
    QLabel* m_progressText;              ///< 进度文字描述

    // 表格视图
    QTableView* m_tableView;             ///< 书籍列表表格视图
    BookModel* m_model;                  ///< 表格数据模型

    // 左侧文件夹筛选面板
    QWidget* m_folderPanel;              ///< 文件夹面板容器
    QVBoxLayout* m_folderPanelLayout;    ///< 文件夹面板布局
    QLineEdit* m_folderSearchBox;        ///< 文件夹搜索框
    QScrollArea* m_folderScrollArea;     ///< 文件夹列表滚动区域
    QWidget* m_folderListContainer;      ///< 文件夹列表容器
    QVBoxLayout* m_folderListLayout;     ///< 文件夹列表布局
    QPushButton* m_selectAllFoldersBtn;  ///< 全选文件夹按钮
    QPushButton* m_deselectAllFoldersBtn;///< 取消全选按钮
    QPushButton* m_invertFoldersBtn;     ///< 反选文件夹按钮
    QList<QCheckBox*> m_folderCheckBoxes;///< 文件夹复选框列表

    // ========== 运行时数据 ==========

    QStringList m_rootFolders;           ///< 用户添加的根目录列表
    QStringList m_supportedExts;         ///< 支持的文件扩展名白名单
    QStringList m_excludeFolders;        ///< 排除的文件夹名称黑名单
    BookScanner* m_scanner = nullptr;    ///< 后台扫描线程指针（扫描中有效，完成后自动清理）
    QSet<QString> m_selectedPaths;       ///< 当前选中文件的路径集合
    int m_sortColumn = BookModel::ColName;       ///< 当前排序列
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder; ///< 当前排序方向

    QString m_contextPath;               ///< 右键菜单目标文件路径

    // MySQL 集成状态
    bool m_useMySQL = false;             ///< 是否启用 MySQL 双写
    bool m_metadataDirty = false;        ///< 元数据是否有未同步的修改
    QLabel* m_dbStatusLabel = nullptr;   ///< 数据库状态指示标签
};

#endif // MAINWINDOW_H
