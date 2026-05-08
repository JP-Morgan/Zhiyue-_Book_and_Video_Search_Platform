#include "mainwindow.h"
#include "tagdialog.h"
#include "batchrenamedialog.h"
#include "statisticsdialog.h"
#include "metadata.h"
#include "databasemanager.h"
#include "dbconfigdialog.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QTimer>
#include <QDialog>
#include <QFormLayout>
#include <QTextEdit>
#include <QFrame>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QPainter>

// ===== Custom delegate for star column =====
class StarDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override {
        painter->save();
        bool starred = index.data(Qt::UserRole + 1).toBool();
        painter->setFont(QFont("Segoe UI Emoji", 14));
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, QColor(102, 126, 234, 80));
        }
        QRect textRect = option.rect.adjusted(0, 0, 0, 0);
        painter->drawText(textRect, Qt::AlignCenter, starred ? "⭐" : "☆");
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(30, 28);
    }
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    //setWindowIcon(QIcon("Image/favicon.ico"));

    setWindowTitle("知阅·书籍视频检索平台 v3.0");
    resize(1200, 750);
    setMinimumSize(800, 500);

    // Default config
    m_rootFolders = QStringList{};
    m_supportedExts = {
        ".pdf", ".epub", ".mobi", ".azw3", ".txt", ".doc", ".docx",
        ".mp4", ".avi", ".mkv", ".flv", ".wmv", ".mov", ".rmvb", ".rm",
        ".mpg", ".mpeg", ".webm", ".ts", ".m4v", ".3gp"
    };
    m_excludeFolders = { "System Volume Information", "$RECYCLE.BIN", ".git", "node_modules" };

    loadConfig();
    loadMetadata();
    initDatabase();

    setupUI();
    setupConnections();
    renderRootTags();
    updateStats();

    // Auto-scan on start (only if folders exist)
    if (!m_rootFolders.isEmpty()) {
        QTimer::singleShot(300, this, &MainWindow::startScan);
    }
}

MainWindow::~MainWindow() {
    if (m_scanner && m_scanner->isRunning()) {
        m_scanner->requestInterruption();
        m_scanner->wait(3000);
    }
    saveConfig();
    saveMetadata();
    flushMetadataToDB();
}

// ===== UI Setup =====
void MainWindow::setupUI() {
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(15, 10, 15, 10);
    m_mainLayout->setSpacing(10);

    // === Header ===
    auto* headerLayout = new QHBoxLayout;
    m_headerLabel = new QLabel("📚 知阅 <span style='font-size:14px;color:#999;'>v3.0</span>");
    m_headerLabel->setStyleSheet("font-size:22px; font-weight:bold; color:#2c3e50; cursor:pointer;");
    headerLayout->addWidget(m_headerLabel);
    headerLayout->addStretch();

    auto addBtn = [&](const QString& text, const QString& color) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(QString(
            "QPushButton { background:%1; color:white; border:none; border-radius:8px; "
            "padding:8px 14px; font-size:12px; font-weight:bold; }"
            "QPushButton:hover { filter:brightness(1.15); }"
        ).arg(color));
        headerLayout->addWidget(btn);
        return btn;
        };

    auto* btnAddDir = addBtn("📁 添加目录", "#17a2b8");
    auto* btnRefresh = addBtn("🔄 刷新列表", "#27ae60");
    auto* btnOpenDir = addBtn("📂 打开目录", "#f39c12");
    auto* btnSelectAll = addBtn("☑️ 全选", "#3498db");
    auto* btnDeleteSel = addBtn("🗑️ 删除选中", "#e74c3c");
    auto* btnBatchRename = addBtn("✏️ 批量重命名", "#9b59b6");
    auto* btnExport = addBtn("📋 导出", "#6c757d");
    auto* btnConfig = addBtn("⚙️ 设置", "#17a2b8");
    auto* btnDBConfig = addBtn("🗄️ 数据库", "#6f42c1");
    auto* btnStats = addBtn("📊 统计", "#f39c12");

    m_mainLayout->addLayout(headerLayout);

    // === Root folders bar ===
    m_rootBar = new QWidget;
    m_rootBar->setStyleSheet("background:#f8f9fa; border:1px solid #e9ecef; border-radius:10px;");
    m_rootBarLayout = new QHBoxLayout(m_rootBar);
    m_rootBarLayout->setContentsMargins(10, 6, 10, 6);
    auto* rootLabel = new QLabel("📚 根目录:");
    rootLabel->setStyleSheet("font-weight:bold; color:#555;");
    m_rootBarLayout->addWidget(rootLabel);
    m_rootBarLayout->addStretch();
    auto* addRootBtn = new QPushButton("+ 添加");
    addRootBtn->setStyleSheet("QPushButton { background:#17a2b8; color:white; border:none; border-radius:10px; padding:3px 10px; font-size:11px; }");
    m_rootBarLayout->addWidget(addRootBtn);
    m_mainLayout->addWidget(m_rootBar);
    connect(addRootBtn, &QPushButton::clicked, this, &MainWindow::addRootFolder);

    // === Stats bar ===
    m_statsLabel = new QLabel;
    m_statsLabel->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #667eea, stop:1 #764ba2); "
        "color:white; border-radius:10px; padding:10px 20px; font-size:13px;");
    m_statsLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_statsLabel);

    // === Toolbar ===
    auto* toolbarLayout = new QHBoxLayout;
    m_searchBox = new QLineEdit;
    m_searchBox->setPlaceholderText("🔍 搜索书籍名称... 支持正则(勾选后) (Ctrl+F)");
    m_searchBox->setStyleSheet("QLineEdit { border:2px solid #3498db; border-radius:10px; padding:10px 15px; font-size:14px; }"
        "QLineEdit:focus { border-color:#2980b9; }");
    toolbarLayout->addWidget(m_searchBox, 1);

    auto addFilter = [&](const QString& tooltip) -> QComboBox* {
        auto* cb = new QComboBox;
        cb->setStyleSheet("QComboBox { padding:6px 10px; border:1px solid #ddd; border-radius:6px; font-size:12px; }");
        cb->setToolTip(tooltip);
        toolbarLayout->addWidget(cb);
        return cb;
        };

    m_filterType = addFilter("文件类型");
    m_filterType->addItem("全部类型", "");
    m_filterType->insertSeparator(m_filterType->count());
    m_filterType->addItem("PDF", ".pdf");
    m_filterType->addItem("EPUB", ".epub");
    m_filterType->addItem("MOBI", ".mobi");
    m_filterType->addItem("AZW3", ".azw3");
    m_filterType->addItem("TXT", ".txt");
    m_filterType->addItem("DOC/DOCX", ".doc");  // data 用 .doc，endsWith 匹配 .doc 和 .docx
    m_filterType->addItem("MP4", ".mp4");
    m_filterType->addItem("AVI", ".avi");
    m_filterType->addItem("MKV", ".mkv");

    m_filterTag = addFilter("标签筛选");
    m_filterTag->addItem("全部标签", "");

    m_filterStar = addFilter("收藏筛选");
    m_filterStar->addItem("全部收藏", "");
    m_filterStar->addItem("⭐ 已收藏", "starred");
    m_filterStar->addItem("未收藏", "unstarred");

    m_sortBy = addFilter("排序");
    m_sortBy->addItem("按名称", BookModel::ColName);
    m_sortBy->addItem("按大小", BookModel::ColSize);
    m_sortBy->addItem("按日期", BookModel::ColDate);
    m_sortBy->addItem("按目录", BookModel::ColFolder);

    m_useRegex = new QCheckBox("正则");
    m_useRegex->setStyleSheet("font-size:12px;");
    toolbarLayout->addWidget(m_useRegex);

    m_mainLayout->addLayout(toolbarLayout);

    // === Progress bar ===
    m_progressBar = new QProgressBar;
    m_progressBar->setFixedHeight(6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet("QProgressBar { background:#e9ecef; border-radius:3px; }"
        "QProgressBar::chunk { background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #667eea, stop:1 #764ba2); border-radius:3px; }");
    m_progressBar->hide();
    m_mainLayout->addWidget(m_progressBar);

    m_progressText = new QLabel;
    m_progressText->setStyleSheet("color:#7f8c8d; font-size:12px;");
    m_progressText->setAlignment(Qt::AlignCenter);
    m_progressText->hide();
    m_mainLayout->addWidget(m_progressText);

    // === Table view ===
    m_model = new BookModel(this);
    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableView->verticalHeader()->hide();
    m_tableView->setShowGrid(false);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(BookModel::ColName, QHeaderView::Stretch);
    m_tableView->horizontalHeader()->setSectionResizeMode(BookModel::ColCheck, QHeaderView::Fixed);
    m_tableView->setColumnWidth(BookModel::ColCheck, 30);
    m_tableView->setColumnWidth(BookModel::ColSize, 80);
    m_tableView->setColumnWidth(BookModel::ColDate, 100);
    m_tableView->setColumnWidth(BookModel::ColFolder, 150);
    m_tableView->setColumnWidth(BookModel::ColTags, 130);
    m_tableView->setColumnWidth(BookModel::ColStar, 40);
    m_tableView->setColumnWidth(BookModel::ColProgress, 80);
    m_tableView->setStyleSheet(
        "QTableView { background:white; border:1px solid #eee; border-radius:8px; }"
        "QTableView::item { padding:5px; }"
        "QTableView::item:selected { background:#f0f7ff; color:#2c3e50; }"
        "QTableView::item:hover { background:#f5f9ff; }"
        "QHeaderView::section { background:#f8f9fa; padding:8px 5px; border:none; border-bottom:2px solid #e9ecef; font-weight:bold; font-size:12px; color:#555; }"
    );
    m_tableView->setItemDelegateForColumn(BookModel::ColStar, new StarDelegate(this));

    // Hide check column (we use selection instead)
    m_tableView->setColumnHidden(BookModel::ColCheck, true);

    // === Folder filter panel (left) + Table view (right) in a splitter ===
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setStyleSheet("QSplitter::handle { background:#ddd; width:1px; }");

    // Folder panel
    m_folderPanel = new QWidget;
    m_folderPanel->setMinimumWidth(160);
    m_folderPanel->setMaximumWidth(280);
    m_folderPanelLayout = new QVBoxLayout(m_folderPanel);
    m_folderPanelLayout->setContentsMargins(8, 8, 8, 8);
    m_folderPanelLayout->setSpacing(6);

    auto* folderTitle = new QLabel("📂 文件夹分类");
    folderTitle->setStyleSheet("font-size:14px; font-weight:bold; color:#2c3e50;");
    m_folderPanelLayout->addWidget(folderTitle);

    // Folder search box
    m_folderSearchBox = new QLineEdit;
    m_folderSearchBox->setPlaceholderText("🔍 筛选文件夹...");
    m_folderSearchBox->setStyleSheet(
        "QLineEdit { border:1px solid #ddd; border-radius:6px; padding:5px 8px; font-size:12px; }"
        "QLineEdit:focus { border-color:#3498db; }");
    m_folderPanelLayout->addWidget(m_folderSearchBox);

    // Folder list with QCheckBox widgets in a scroll area
    m_folderScrollArea = new QScrollArea;
    m_folderScrollArea->setWidgetResizable(true);
    m_folderScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_folderScrollArea->setStyleSheet(
        "QScrollArea { border:1px solid #eee; border-radius:6px; background:white; }"
        "QScrollArea > QWidget > QWidget { background:white; }"
        "QScrollBar:vertical { width:6px; background:#f0f0f0; border-radius:3px; }"
        "QScrollBar::handle:vertical { background:#ccc; border-radius:3px; min-height:20px; }"
        "QScrollBar::handle:vertical:hover { background:#aaa; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");

    m_folderListContainer = new QWidget;
    m_folderListContainer->setStyleSheet("background:white;");
    m_folderListLayout = new QVBoxLayout(m_folderListContainer);
    m_folderListLayout->setContentsMargins(6, 4, 6, 4);
    m_folderListLayout->setSpacing(1);
    m_folderListLayout->addStretch();

    m_folderScrollArea->setWidget(m_folderListContainer);
    m_folderPanelLayout->addWidget(m_folderScrollArea, 1);

    // Folder action buttons
    auto* folderBtnLayout = new QHBoxLayout;
    folderBtnLayout->setSpacing(4);

    m_selectAllFoldersBtn = new QPushButton("全选");
    m_selectAllFoldersBtn->setStyleSheet(
        "QPushButton { background:#3498db; color:white; border:none; border-radius:4px; padding:4px 6px; font-size:11px; }"
        "QPushButton:hover { background:#2980b9; }");

    m_deselectAllFoldersBtn = new QPushButton("取消");
    m_deselectAllFoldersBtn->setStyleSheet(
        "QPushButton { background:#95a5a6; color:white; border:none; border-radius:4px; padding:4px 6px; font-size:11px; }"
        "QPushButton:hover { background:#7f8c8d; }");

    m_invertFoldersBtn = new QPushButton("反选");
    m_invertFoldersBtn->setStyleSheet(
        "QPushButton { background:#e67e22; color:white; border:none; border-radius:4px; padding:4px 6px; font-size:11px; }"
        "QPushButton:hover { background:#d35400; }");

    folderBtnLayout->addWidget(m_selectAllFoldersBtn);
    folderBtnLayout->addWidget(m_deselectAllFoldersBtn);
    folderBtnLayout->addWidget(m_invertFoldersBtn);
    m_folderPanelLayout->addLayout(folderBtnLayout);

    splitter->addWidget(m_folderPanel);
    splitter->addWidget(m_tableView);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    m_mainLayout->addWidget(splitter, 1);

    // === Footer ===
    auto* footerLayout = new QHBoxLayout;
    auto* footer = new QLabel(
        "<kbd>Ctrl+F</kbd> 搜索  <kbd>Ctrl+A</kbd> 全选  "
        "<kbd>Delete</kbd> 删除选中  <kbd>F5</kbd> 刷新  "
        "<kbd>Esc</kbd> 关闭面板  右键菜单可用");
    footer->setStyleSheet("color:#7f8c8d; font-size:11px; padding:5px;");
    footerLayout->addWidget(footer);
    footerLayout->addStretch();
    m_dbStatusLabel = new QLabel;
    m_dbStatusLabel->setStyleSheet("font-size:11px; padding:3px 8px; border-radius:4px;");
    updateDBStatusLabel();
    footerLayout->addWidget(m_dbStatusLabel);
    m_mainLayout->addLayout(footerLayout);

    // === Connect buttons ===
    connect(btnAddDir, &QPushButton::clicked, this, &MainWindow::addRootFolder);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(btnOpenDir, &QPushButton::clicked, this, [this]() {
        if (!m_rootFolders.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_rootFolders.first()));
        }
        });
    connect(btnSelectAll, &QPushButton::clicked, this, &MainWindow::selectAllVisible);
    connect(btnDeleteSel, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(btnBatchRename, &QPushButton::clicked, this, &MainWindow::openBatchRename);
    connect(btnExport, &QPushButton::clicked, this, [this]() {
        QMenu menu;
        menu.addAction("📄 导出为 CSV (Excel可打开)", this, &MainWindow::exportCSV);
        menu.addAction("📝 导出为 TXT (纯文本)", this, &MainWindow::exportTXT);
        menu.addAction("📋 复制到剪贴板", this, &MainWindow::exportClipboard);
        menu.exec(QCursor::pos());
        });
    connect(btnConfig, &QPushButton::clicked, this, &MainWindow::openConfig);
    connect(btnDBConfig, &QPushButton::clicked, this, &MainWindow::openDBConfig);
    connect(btnStats, &QPushButton::clicked, this, &MainWindow::showStats);

    connect(m_searchBox, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(m_filterType, &QComboBox::currentIndexChanged, this, &MainWindow::applyFilter);
    connect(m_filterTag, &QComboBox::currentIndexChanged, this, &MainWindow::applyFilter);
    connect(m_filterStar, &QComboBox::currentIndexChanged, this, &MainWindow::applyFilter);
    connect(m_useRegex, &QCheckBox::toggled, this, &MainWindow::applyFilter);
    connect(m_sortBy, &QComboBox::currentIndexChanged, this, [this]() {
        m_model->sortByColumn(m_sortBy->currentData().toInt());
        });

    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::openFile);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_tableView->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::onHeaderClicked);

    // 同步表格选择到 m_selectedPaths
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, [this]() {
            m_selectedPaths.clear();
            for (const auto& idx : m_tableView->selectionModel()->selectedRows()) {
                BookInfo book = m_model->bookAt(idx.row());
                if (!book.path.isEmpty()) {
                    m_selectedPaths.insert(book.path);
                }
            }
            updateStats();
        });

    // Folder filter connections
    connect(m_selectAllFoldersBtn, &QPushButton::clicked, this, &MainWindow::selectAllFolders);
    connect(m_deselectAllFoldersBtn, &QPushButton::clicked, this, &MainWindow::deselectAllFolders);
    connect(m_invertFoldersBtn, &QPushButton::clicked, this, &MainWindow::invertFolderSelection);
    connect(m_folderSearchBox, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (auto* cb : m_folderCheckBoxes) {
            cb->setVisible(text.isEmpty() ||
                cb->text().contains(text, Qt::CaseInsensitive));
        }
        });
}

void MainWindow::setupConnections() {
    // handled inline in setupUI
}

// ===== Root folder management =====
void MainWindow::renderRootTags() {
    // Clear all existing widgets from the layout
    QLayoutItem* child;
    while ((child = m_rootBarLayout->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget();
        delete child;
    }

    auto* rootLabel = new QLabel("📚 根目录:");
    rootLabel->setStyleSheet("font-weight:bold; color:#555;");
    m_rootBarLayout->addWidget(rootLabel);

    if (m_rootFolders.isEmpty()) {
        auto* hint = new QLabel("  请添加书籍目录开始使用");
        hint->setStyleSheet("color:#999; font-size:12px; font-style:italic;");
        m_rootBarLayout->addWidget(hint);
    }

    for (int i = 0; i < m_rootFolders.size(); i++) {
        // Container widget for tag + remove button
        auto* tagWidget = new QWidget;
        tagWidget->setStyleSheet(
            "background:#e8f4fd; border-radius:12px;");
        auto* tagLayout = new QHBoxLayout(tagWidget);
        tagLayout->setContentsMargins(10, 2, 2, 2);
        tagLayout->setSpacing(4);

        auto* pathLabel = new QLabel(m_rootFolders[i]);
        pathLabel->setStyleSheet("color:#3498db; font-size:12px; background:transparent;");
        pathLabel->setToolTip(m_rootFolders[i]);
        tagLayout->addWidget(pathLabel);

        auto* removeBtn = new QPushButton("✕");
        removeBtn->setFixedSize(20, 20);
        removeBtn->setStyleSheet(
            "QPushButton { background:transparent; color:#999; border:none; border-radius:10px; "
            "font-size:12px; font-weight:bold; }"
            "QPushButton:hover { background:#e74c3c; color:white; }");
        removeBtn->setToolTip("移除此目录");
        QString folderPath = m_rootFolders[i];
        connect(removeBtn, &QPushButton::clicked, this, [this, folderPath]() {
            removeRootFolder(folderPath);
            });
        tagLayout->addWidget(removeBtn);

        m_rootBarLayout->addWidget(tagWidget);
    }

    m_rootBarLayout->addStretch();

    auto* addRootBtn = new QPushButton("+ 添加");
    addRootBtn->setStyleSheet("QPushButton { background:#17a2b8; color:white; border:none; border-radius:10px; padding:3px 10px; font-size:11px; }");
    connect(addRootBtn, &QPushButton::clicked, this, &MainWindow::addRootFolder);
    m_rootBarLayout->addWidget(addRootBtn);
}

void MainWindow::addRootFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择书籍目录", QString(),
        QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty()) {
        if (!dir.endsWith('/') && !dir.endsWith('\\')) dir += '/';
        if (!m_rootFolders.contains(dir)) {
            m_rootFolders.append(dir);
            saveConfig();
            renderRootTags();
            startScan();
        }
        else {
            QMessageBox::information(this, "提示", "该目录已添加");
        }
    }
}

void MainWindow::removeRootFolder(const QString& path) {
    m_rootFolders.removeAll(path);
    saveConfig();
    renderRootTags();
    if (!m_rootFolders.isEmpty()) {
        startScan();
    }
    else {
        m_model->setBooks({});
        updateStats();
    }
}

// ===== Scanning =====
void MainWindow::startScan() {
    if (m_scanner && m_scanner->isRunning()) {
        m_scanner->requestInterruption();
        m_scanner->wait(5000);
    }

    m_progressBar->setValue(0);
    m_progressBar->show();
    m_progressText->setText("正在扫描目录结构...");
    m_progressText->show();

    m_scanner = new BookScanner(this);
    m_scanner->setRoots(m_rootFolders);
    m_scanner->setExtensions(m_supportedExts);
    m_scanner->setExcludes(m_excludeFolders);

    connect(m_scanner, &BookScanner::progressUpdated, this, &MainWindow::onScanProgress);
    connect(m_scanner, &BookScanner::scanFinished, this, &MainWindow::onScanFinished);
    connect(m_scanner, &QThread::finished, m_scanner, &QObject::deleteLater);

    m_scanner->start();
}

void MainWindow::onScanProgress(int current, int total) {
    if (total > 0) {
        int pct = qRound(100.0 * current / total);
        m_progressBar->setValue(pct);
        m_progressText->setText(QString("正在扫描: %1 / %2 个目录 (%3%)").arg(current).arg(total).arg(pct));
    }
}

void MainWindow::onScanFinished() {
    if (!m_scanner) return;
    auto results = m_scanner->results();
    m_model->setBooks(results);
    m_model->sortByColumn(m_sortBy->currentData().toInt());

    m_progressBar->hide();
    m_progressText->hide();

    updateTagFilterOptions();
    populateFolderList();
    updateStats();
    saveConfig();

    // 同步到MySQL数据库
    if (m_useMySQL && DatabaseManager::instance().isConnected()) {
        // 批量写入书籍数据
        DatabaseManager::instance().batchInsertBooks(results);
        // 同步元数据
        syncMetadataToDB();
        // 保存根目录到数据库
        DatabaseManager::instance().saveRootFoldersToDB(m_rootFolders);
        // 记录扫描日志
        qint64 totalSize = 0;
        for (const auto& b : results) totalSize += b.size;
        DatabaseManager::instance().logScan(
            m_rootFolders, results.size(), totalSize, 0, "success");
    }

    // Scanner will be deleted via deleteLater; clear the pointer
    m_scanner = nullptr;
}

void MainWindow::populateFolderList() {
    // Collect all unique folders from the model
    QSet<QString> folders;
    for (const auto& book : m_model->allBooks()) {
        folders.insert(book.folder);
    }

    // Remember currently checked folders
    QSet<QString> checkedFolders;
    for (auto* cb : m_folderCheckBoxes) {
        if (cb->isChecked()) {
            checkedFolders.insert(cb->property("folder").toString());
        }
    }

    // If nothing was checked before, default to all checked
    bool firstTime = checkedFolders.isEmpty() && m_folderCheckBoxes.isEmpty();

    // Clear existing checkboxes
    for (auto* cb : m_folderCheckBoxes) {
        m_folderListLayout->removeWidget(cb);
        delete cb;
    }
    m_folderCheckBoxes.clear();

    // Sort folders
    QStringList sorted = folders.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        if (a == "(根目录)") return true;
        if (b == "(根目录)") return false;
        return a.toLower() < b.toLower();
        });

    // Create checkboxes
    for (const auto& folder : sorted) {
        auto* cb = new QCheckBox(folder);
        cb->setProperty("folder", folder);
        cb->setStyleSheet(
            "QCheckBox { spacing:6px; padding:3px 2px; font-size:12px; color:#333; }"
            "QCheckBox::indicator { width:16px; height:16px; }"
            "QCheckBox::indicator:unchecked { border:2px solid #ccc; border-radius:3px; background:white; }"
            "QCheckBox::indicator:unchecked:hover { border-color:#3498db; }"
            "QCheckBox::indicator:checked { border:2px solid #3498db; border-radius:3px; background:#3498db; }"
            "QCheckBox::indicator:checked:hover { background:#2980b9; border-color:#2980b9; }"
        );

        if (firstTime || checkedFolders.contains(folder)) {
            cb->setChecked(true);
        }

        connect(cb, &QCheckBox::toggled, this, &MainWindow::onFolderFilterChanged);

        // Insert before the stretch at the end
        m_folderListLayout->insertWidget(m_folderListLayout->count() - 1, cb);
        m_folderCheckBoxes.append(cb);
    }

    // Apply filter to reflect folder selection
    applyFilter();
}

// ===== Filtering =====
void MainWindow::applyFilter() {
    // Collect selected folders from checkboxes
    QSet<QString> selectedFolders;
    for (auto* cb : m_folderCheckBoxes) {
        if (cb->isChecked() && cb->isVisible()) {
            selectedFolders.insert(cb->property("folder").toString());
        }
    }

    QString search = m_searchBox->text();
    QString type = m_filterType->currentData().toString();
    QString tag = m_filterTag->currentData().toString();
    QString star = m_filterStar->currentData().toString();
    m_model->setFilter(search, type, tag, star, m_useRegex->isChecked(), false, selectedFolders);
    m_model->sortByColumn(m_sortBy->currentData().toInt());
    updateStats();
}

void MainWindow::updateTagFilterOptions() {
    QString current = m_filterTag->currentData().toString();
    m_filterTag->clear();
    m_filterTag->addItem("全部标签", "");
    for (const auto& name : MetadataStore::instance().allTagNames()) {
        m_filterTag->addItem(name, name);
    }
    // Restore selection
    int idx = m_filterTag->findData(current);
    if (idx >= 0) m_filterTag->setCurrentIndex(idx);
}

// ===== Folder filter slots =====
void MainWindow::onFolderFilterChanged() {
    applyFilter();
}

void MainWindow::selectAllFolders() {
    for (auto* cb : m_folderCheckBoxes) {
        if (cb->isVisible()) {
            cb->setChecked(true);
        }
    }
}

void MainWindow::deselectAllFolders() {
    for (auto* cb : m_folderCheckBoxes) {
        if (cb->isVisible()) {
            cb->setChecked(false);
        }
    }
}

void MainWindow::invertFolderSelection() {
    for (auto* cb : m_folderCheckBoxes) {
        if (cb->isVisible()) {
            cb->setChecked(!cb->isChecked());
        }
    }
}

// ===== Stats update =====
void MainWindow::updateStats() {
    const auto& all = m_model->allBooks();
    const auto& vis = m_model->visibleBooks();

    qint64 totalSize = 0;
    int starred = 0;
    QSet<QString> folders;
    for (const auto& b : all) {
        totalSize += b.size;
        folders.insert(b.folder);
        if (MetadataStore::instance().metaOf(b.path).starred) starred++;
    }

    m_statsLabel->setText(
        QString("总文件数: %1  |  分类数: %2  |  已选中: %3  |  总大小: %4  |  已收藏: %5")
        .arg(all.size())
        .arg(folders.size())
        .arg(m_selectedPaths.size())
        .arg(BookModel::formatSize(totalSize))
        .arg(starred)
    );
}

// ===== Selection =====
void MainWindow::selectAllVisible() {
    const auto& vis = m_model->visibleBooks();

    // 检查是否所有可见行都已选中
    bool allSelected = true;
    for (const auto& b : vis) {
        if (!m_selectedPaths.contains(b.path)) { allSelected = false; break; }
    }

    auto* selModel = m_tableView->selectionModel();
    if (allSelected) {
        // 取消选中所有可见行
        selModel->clearSelection();
        m_selectedPaths.clear();
    }
    else {
        // 选中所有可见行（通过表格选择模型，会自动触发 selectionChanged 同步 m_selectedPaths）
        selModel->blockSignals(true);
        selModel->clearSelection();
        m_selectedPaths.clear();
        for (int row = 0; row < m_model->rowCount(); row++) {
            selModel->select(m_model->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
            BookInfo book = m_model->bookAt(row);
            if (!book.path.isEmpty()) {
                m_selectedPaths.insert(book.path);
            }
        }
        selModel->blockSignals(false);
    }
    updateStats();
}

void MainWindow::deleteSelected() {
    if (m_selectedPaths.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要删除的文件！");
        return;
    }

    auto ret = QMessageBox::warning(this, "确认删除",
        QString("确定要删除 %1 个文件吗？\n\n文件将被永久删除。").arg(m_selectedPaths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    QStringList failed;
    for (const QString& path : m_selectedPaths) {
        QFile file(path);
        if (file.exists()) {
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            if (!file.remove()) {
                failed.append(QFileInfo(path).fileName());
            }
        }
        MetadataStore::instance().removePath(path);
    }

    m_selectedPaths.clear();
    saveMetadata();
    flushMetadataToDB();

    if (!failed.isEmpty()) {
        QMessageBox::warning(this, "删除失败", QString("部分文件删除失败:\n") + failed.join("\n"));
    }

    startScan();
}

// ===== File operations =====
void MainWindow::openFile(const QModelIndex& index) {
    int row = index.row();
    BookInfo book = m_model->bookAt(row);
    if (!book.path.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(book.path));
    }
}

void MainWindow::openContainingFolder(const QString& path) {
    QFileInfo fi(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

// ===== Context menu =====
void MainWindow::showContextMenu(const QPoint& pos) {
    QModelIndex idx = m_tableView->indexAt(pos);
    if (!idx.isValid()) return;

    BookInfo book = m_model->bookAt(idx.row());
    if (book.path.isEmpty()) return;
    m_contextPath = book.path;

    QMenu menu;
    menu.addAction("📄 打开文件", this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_contextPath));
        });
    menu.addAction("📂 打开所在文件夹", this, [this]() {
        openContainingFolder(m_contextPath);
        });
    menu.addSeparator();
    menu.addAction("📋 复制文件路径", this, [this]() {
        QApplication::clipboard()->setText(m_contextPath);
        });
    menu.addAction("📋 复制文件名", this, [this]() {
        QApplication::clipboard()->setText(QFileInfo(m_contextPath).fileName());
        });
    menu.addSeparator();
    menu.addAction("ℹ️ 查看详情", this, [this]() {
        showFileDetail(m_contextPath);
        });
    menu.addAction("🏷️ 编辑标签", this, [this]() {
        editTagsForPath(m_contextPath);
        });
    menu.addAction("📊 阅读进度", this, [this]() {
        editProgressForPath(m_contextPath);
        });
    menu.addAction("⭐ 收藏/取消收藏", this, [this]() {
        toggleStarForPath(m_contextPath);
        });
    menu.addSeparator();
    menu.addAction("✏️ 重命名", this, [this]() {
        QFileInfo fi(m_contextPath);
        QString newName = QInputDialog::getText(this, "重命名", "新文件名:", QLineEdit::Normal, fi.fileName());
        if (!newName.isEmpty() && newName != fi.fileName()) {
            QString newPath = fi.absolutePath() + "/" + newName;
            QFile::setPermissions(m_contextPath, QFile::ReadOwner | QFile::WriteOwner);
            if (QFile::rename(m_contextPath, newPath)) {
                MetadataStore::instance().renamePath(m_contextPath, newPath);
                saveMetadata();
                flushMetadataToDB();
                startScan();
            }
            else {
                QMessageBox::warning(this, "失败", "重命名失败");
            }
        }
        });
    menu.addAction("🔒 切换只读", this, [this]() {
        QFile f(m_contextPath);
        auto perms = f.permissions();
        if (perms & QFile::WriteUser) {
            f.setPermissions(perms & ~QFile::WriteUser);
        }
        else {
            f.setPermissions(perms | QFile::WriteUser);
        }
        m_model->refresh();
        });
    menu.addSeparator();
    menu.addAction("🗑️ 删除文件", this, [this]() {
        auto ret = QMessageBox::warning(this, "确认", QString("确定要删除此文件吗？\n") + QFileInfo(m_contextPath).fileName(),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            QFile::setPermissions(m_contextPath, QFile::ReadOwner | QFile::WriteOwner);
            if (QFile::remove(m_contextPath)) {
                MetadataStore::instance().removePath(m_contextPath);
                saveMetadata();
                flushMetadataToDB();
                startScan();
            }
        }
        });

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

// ===== Detail panel =====
void MainWindow::showFileDetail(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return;

    const BookMeta& meta = MetadataStore::instance().metaOf(path);

    QDialog dlg(this);
    dlg.setWindowTitle(QString("文件详情 - ") + fi.fileName());
    dlg.setMinimumWidth(450);
    auto* layout = new QVBoxLayout(&dlg);

    auto addRow = [&](const QString& label, const QString& value) {
        auto* row = new QHBoxLayout;
        auto* l = new QLabel(label);
        l->setStyleSheet("color:#999; min-width:80px;");
        auto* v = new QLabel(value);
        v->setStyleSheet("font-weight:bold; color:#333;");
        v->setWordWrap(true);
        row->addWidget(l);
        row->addWidget(v, 1);
        layout->addLayout(row);
        };

    addRow("文件名", fi.fileName());
    addRow("完整路径", fi.absoluteFilePath());
    addRow("文件大小", BookModel::formatSize(fi.size()));
    addRow("修改时间", fi.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
    addRow("创建时间", fi.birthTime().toString("yyyy-MM-dd hh:mm:ss"));
    addRow("只读", fi.isWritable() ? "否" : "是");

    // Tags
    QStringList tagNames;
    for (const auto& t : meta.tags) tagNames << t.name;
    addRow("标签", tagNames.isEmpty() ? "无" : tagNames.join(", "));

    addRow("收藏", meta.starred ? "⭐ 已收藏" : "未收藏");

    QString progressStr = "未记录";
    if (meta.progress.isValid()) {
        if (meta.progress.type == "percent")
            progressStr = QString("%1%").arg(meta.progress.value);
        else if (meta.progress.total > 0)
            progressStr = QString("第%1页 / 共%2页").arg(meta.progress.value).arg(meta.progress.total);
        else
            progressStr = QString("第%1页").arg(meta.progress.value);
        if (!meta.progress.note.isEmpty()) progressStr += " (" + meta.progress.note + ")";
    }
    addRow("阅读进度", progressStr);

    auto* btnLayout = new QHBoxLayout;
    auto* tagBtn = new QPushButton("🏷️ 编辑标签");
    auto* progBtn = new QPushButton("📊 阅读进度");
    btnLayout->addWidget(tagBtn);
    btnLayout->addWidget(progBtn);
    layout->addLayout(btnLayout);

    connect(tagBtn, &QPushButton::clicked, &dlg, [&dlg, path, this]() {
        editTagsForPath(path);
        });
    connect(progBtn, &QPushButton::clicked, &dlg, [&dlg, path, this]() {
        editProgressForPath(path);
        });

    dlg.exec();
    m_model->refresh();
    updateStats();
}

// ===== Tag editing =====
void MainWindow::editTagsForPath(const QString& path) {
    TagDialog dlg(path, this);
    connect(&dlg, &TagDialog::tagsChanged, this, [this]() {
        updateTagFilterOptions();
        m_model->refresh();
        updateStats();
        });
    dlg.exec();
    saveMetadata();
    flushMetadataToDB(); // 标签修改后立即同步
}

// ===== Progress editing =====
void MainWindow::editProgressForPath(const QString& path) {
    BookMeta& meta = MetadataStore::instance().getMeta(path);

    QDialog dlg(this);
    dlg.setWindowTitle(QString("⚙️ 记录阅读进度 - ") + QFileInfo(path).fileName());
    dlg.setMinimumWidth(350);
    auto* layout = new QVBoxLayout(&dlg);

    auto* typeCombo = new QComboBox;
    typeCombo->addItem("百分比", "percent");
    typeCombo->addItem("页码", "page");
    layout->addWidget(new QLabel("进度类型："));
    layout->addWidget(typeCombo);

    QWidget* percentWidget = new QWidget;
    auto* percentLayout = new QFormLayout(percentWidget);
    auto* percentEdit = new QLineEdit;
    percentLayout->addRow("进度百分比 (0-100):", percentEdit);
    layout->addWidget(percentWidget);

    QWidget* pageWidget = new QWidget;
    auto* pageLayout = new QFormLayout(pageWidget);
    auto* curPageEdit = new QLineEdit;
    auto* totPageEdit = new QLineEdit;
    pageLayout->addRow("当前页码:", curPageEdit);
    pageLayout->addRow("总页数:", totPageEdit);
    pageWidget->hide();
    layout->addWidget(pageWidget);

    auto* noteEdit = new QLineEdit;
    layout->addWidget(new QLabel("备注："));
    layout->addWidget(noteEdit);

    // Load existing
    if (meta.progress.isValid()) {
        typeCombo->setCurrentIndex(typeCombo->findData(meta.progress.type));
        if (meta.progress.type == "percent") {
            percentEdit->setText(QString::number(meta.progress.value));
        }
        else {
            curPageEdit->setText(QString::number(meta.progress.value));
            totPageEdit->setText(QString::number(meta.progress.total));
        }
        noteEdit->setText(meta.progress.note);
    }

    connect(typeCombo, &QComboBox::currentIndexChanged, [=]() {
        QString t = typeCombo->currentData().toString();
        percentWidget->setVisible(t == "percent");
        pageWidget->setVisible(t == "page");
        });

    auto* btnLayout = new QHBoxLayout;
    auto* saveBtn = new QPushButton("保存");
    saveBtn->setStyleSheet("QPushButton { background:#27ae60; color:white; border:none; border-radius:8px; padding:8px 20px; font-weight:bold; }");
    auto* cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("QPushButton { background:#6c757d; color:white; border:none; border-radius:8px; padding:8px 20px; }");
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, [&]() {
        QString type = typeCombo->currentData().toString();
        if (type == "percent") {
            bool ok;
            int val = percentEdit->text().toInt(&ok);
            if (!ok || percentEdit->text().isEmpty()) {
                meta.progress = ProgressInfo();
            }
            else {
                meta.progress.type = "percent";
                meta.progress.value = qBound(0, val, 100);
                meta.progress.note = noteEdit->text();
            }
        }
        else {
            if (curPageEdit->text().isEmpty()) {
                meta.progress = ProgressInfo();
            }
            else {
                meta.progress.type = "page";
                meta.progress.value = curPageEdit->text().toInt();
                meta.progress.total = totPageEdit->text().toInt();
                meta.progress.note = noteEdit->text();
            }
        }
        dlg.accept();
        });

    if (dlg.exec() == QDialog::Accepted) {
        saveMetadata();
        flushMetadataToDB();
        m_model->refresh();
        updateStats();
    }
}

// ===== Star toggle =====
void MainWindow::toggleStarForPath(const QString& path) {
    BookMeta& meta = MetadataStore::instance().getMeta(path);
    meta.starred = !meta.starred;
    saveMetadata();
    flushMetadataToDB();
    m_model->refresh();
    updateStats();
}

// ===== Batch rename =====
void MainWindow::openBatchRename() {
    if (m_selectedPaths.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要重命名的文件！\n可使用 Ctrl+A 全选。");
        return;
    }

    QList<BookInfo> selected;
    for (const auto& book : m_model->allBooks()) {
        if (m_selectedPaths.contains(book.path)) selected.append(book);
    }

    BatchRenameDialog dlg(selected, this);
    if (dlg.exec() == QDialog::Accepted) {
        auto mapping = dlg.renameMapping();
        QStringList failed;
        for (auto it = mapping.begin(); it != mapping.end(); ++it) {
            QString oldPath = it.key();
            QString newName = it.value();
            QFileInfo fi(oldPath);
            if (fi.fileName() == newName) continue;

            QString newPath = fi.absolutePath() + "/" + newName;
            QFile::setPermissions(oldPath, QFile::ReadOwner | QFile::WriteOwner);
            if (QFile::rename(oldPath, newPath)) {
                MetadataStore::instance().renamePath(oldPath, newPath);
            }
            else {
                failed.append(fi.fileName());
            }
        }
        saveMetadata();
        flushMetadataToDB();
        if (!failed.isEmpty()) {
            QMessageBox::warning(this, "重命名失败", QString("部分文件重命名失败:\n") + failed.join("\n"));
        }
        m_selectedPaths.clear();
        startScan();
    }
}

// ===== Statistics =====
void MainWindow::showStats() {
    if (m_model->allBooks().isEmpty()) {
        QMessageBox::information(this, "提示", "没有书籍数据");
        return;
    }
    StatisticsDialog dlg(m_model->allBooks(), this);
    dlg.exec();
}

// ===== Export =====
void MainWindow::exportCSV() {
    QString path = QFileDialog::getSaveFileName(this, "导出CSV", "书单导出.csv", "CSV文件 (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "失败", "无法创建文件");
        return;
    }

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "\uFEFF"; // BOM for Excel
    out << "文件名,路径,分类,大小,修改时间,标签,收藏,阅读进度\n";

    // CSV helper: escape quotes by doubling them
    auto csvEscape = [](const QString& s) -> QString {
        QString t = s;
        t.replace('"', "\"\"");
        return QString("\"") + t + QString("\"");
        };

    for (const auto& b : m_model->allBooks()) {
        const BookMeta& meta = MetadataStore::instance().metaOf(b.path);
        QStringList tagNames;
        for (const auto& t : meta.tags) tagNames << t.name;

        QString progress;
        if (meta.progress.isValid()) {
            if (meta.progress.type == "percent")
                progress = QString("%1%").arg(meta.progress.value);
            else
                progress = QString("%1/%2页").arg(meta.progress.value).arg(meta.progress.total);
        }

        QStringList fields;
        fields << csvEscape(b.name)
            << csvEscape(b.path)
            << csvEscape(b.folder)
            << csvEscape(BookModel::formatSize(b.size))
            << csvEscape(b.dateModified.toString("yyyy-MM-dd"))
            << csvEscape(tagNames.join("/"))
            << csvEscape(meta.starred ? "是" : "否")
            << csvEscape(progress);
        out << fields.join(",") << "\n";
    }
    f.close();
    QMessageBox::information(this, "成功", QString("导出成功！\n") + path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void MainWindow::exportTXT() {
    QString path = QFileDialog::getSaveFileName(this, "导出TXT", "书单导出.txt", "文本文件 (*.txt)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "失败", "无法创建文件");
        return;
    }

    QTextStream out(&f);
    out << "===== 书籍管理器 - 书单导出 =====\n";
    out << "导出时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    out << "总书籍数: " << m_model->allBooks().size() << "\n";
    out << "================================\n\n";

    for (const auto& b : m_model->allBooks()) {
        const BookMeta& meta = MetadataStore::instance().metaOf(b.path);
        QString star = meta.starred ? "⭐" : "  ";
        out << star << " " << b.name << " (" << BookModel::formatSize(b.size) << ") [" << b.folder << "]\n";
    }
    f.close();
    QMessageBox::information(this, "成功", QString("导出成功！\n") + path);
}

void MainWindow::exportClipboard() {
    QString text = QString("书籍清单 (%1 本)\n").arg(m_model->allBooks().size());
    text += "导出时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "\n\n";

    for (const auto& b : m_model->allBooks()) {
        const BookMeta& meta = MetadataStore::instance().metaOf(b.path);
        QString star = meta.starred ? "⭐" : "";
        text += star + " " + b.name + "\n";
    }

    QApplication::clipboard()->setText(text);
    QMessageBox::information(this, "成功", "已复制到剪贴板！");
}

// ===== Config =====
void MainWindow::openConfig() {
    QDialog dlg(this);
    dlg.setWindowTitle("⚙️ 配置");
    dlg.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dlg);

    // Supported extensions
    layout->addWidget(new QLabel("📁 支持的文件格式："));
    auto* extEdit = new QLineEdit(m_supportedExts.join(", "));
    extEdit->setPlaceholderText("用逗号分隔，如 .pdf,.epub,.txt");
    layout->addWidget(extEdit);

    // Exclude folders
    layout->addWidget(new QLabel("🚫 排除的文件夹名称："));
    auto* exclEdit = new QLineEdit(m_excludeFolders.join(", "));
    exclEdit->setPlaceholderText("用逗号分隔");
    layout->addWidget(exclEdit);

    // Data management
    layout->addWidget(new QLabel("📊 数据管理："));
    auto* dataBtnLayout = new QHBoxLayout;
    auto* exportBtn = new QPushButton("📤 导出配置");
    auto* importBtn = new QPushButton("📥 导入配置");
    auto* clearBtn = new QPushButton("🗑️ 清除所有数据");
    clearBtn->setStyleSheet("QPushButton { color:#e74c3c; }");
    dataBtnLayout->addWidget(exportBtn);
    dataBtnLayout->addWidget(importBtn);
    dataBtnLayout->addWidget(clearBtn);
    layout->addLayout(dataBtnLayout);

    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "导出配置", "书籍管理器配置备份.json", "JSON (*.json)");
        if (path.isEmpty()) return;
        QJsonObject backup;
        QJsonArray roots; for (const auto& r : m_rootFolders) roots.append(r);
        backup["rootFolders"] = roots;
        QJsonArray exts; for (const auto& e : m_supportedExts) exts.append(e);
        backup["supportedExts"] = exts;
        QJsonArray excl; for (const auto& e : m_excludeFolders) excl.append(e);
        backup["excludeFolders"] = excl;

        QJsonObject metaObj;
        for (auto it = MetadataStore::instance().allData().begin();
            it != MetadataStore::instance().allData().end(); ++it) {
            metaObj[it.key()] = it.value().toJson();
        }
        backup["metadata"] = metaObj;
        backup["exportDate"] = QDateTime::currentDateTime().toString();

        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(backup).toJson());
            QMessageBox::information(this, "成功", "配置导出成功！");
        }
        });

    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importConfig);

    connect(clearBtn, &QPushButton::clicked, this, [this, &dlg]() {
        if (QMessageBox::question(this, "确认", "确定要清除所有配置和标签数据吗？\n不会删除您的书籍文件。",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            clearAllData();
            dlg.accept();
        }
        });

    // Buttons
    auto* btnLayout = new QHBoxLayout;
    auto* saveBtn = new QPushButton("保存配置");
    saveBtn->setStyleSheet("QPushButton { background:#3498db; color:white; border:none; border-radius:8px; padding:8px 20px; font-weight:bold; }");
    auto* cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("QPushButton { background:#6c757d; color:white; border:none; border-radius:8px; padding:8px 20px; }");
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, [&]() {
        QStringList exts = extEdit->text().replace(" ", "").split(",", Qt::SkipEmptyParts);
        m_supportedExts.clear();
        for (auto& e : exts) {
            e = e.toLower();
            if (!e.startsWith('.')) e.prepend('.');
            m_supportedExts.append(e);
        }
        m_excludeFolders = exclEdit->text().replace(" ", "").split(",", Qt::SkipEmptyParts);
        saveConfig();
        dlg.accept();
        startScan();
        });

    dlg.exec();
}

void MainWindow::importConfig() {
    QString path = QFileDialog::getOpenFileName(this, "导入配置", "", "JSON (*.json)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(f.readAll());
        auto obj = doc.object();

        if (obj.contains("rootFolders")) {
            m_rootFolders.clear();
            for (const auto& v : obj["rootFolders"].toArray()) m_rootFolders.append(v.toString());
        }
        if (obj.contains("supportedExts")) {
            m_supportedExts.clear();
            for (const auto& v : obj["supportedExts"].toArray()) m_supportedExts.append(v.toString());
        }
        if (obj.contains("excludeFolders")) {
            m_excludeFolders.clear();
            for (const auto& v : obj["excludeFolders"].toArray()) m_excludeFolders.append(v.toString());
        }
        if (obj.contains("metadata")) {
            auto metaObj = obj["metadata"].toObject();
            for (auto it = metaObj.begin(); it != metaObj.end(); ++it) {
                BookMeta imported = BookMeta::fromJson(it.value().toObject());
                BookMeta& existing = MetadataStore::instance().getMeta(it.key());
                // Merge: imported data overwrites, but keeps existing fields not in import
                if (!imported.tags.isEmpty()) existing.tags = imported.tags;
                existing.starred = imported.starred;
                if (imported.progress.isValid()) existing.progress = imported.progress;
            }
        }

        saveConfig();
        saveMetadata();
        flushMetadataToDB();
        renderRootTags();
        QMessageBox::information(this, "成功", "配置导入成功！将刷新列表。");
        startScan();
    }
}

void MainWindow::clearAllData() {
    m_rootFolders = QStringList{};
    m_supportedExts = { ".pdf", ".epub", ".mobi", ".azw3", ".txt", ".doc", ".docx",
                        ".mp4", ".avi", ".mkv", ".flv", ".wmv", ".mov", ".rmvb", ".rm",
                        ".mpg", ".mpeg", ".webm", ".ts", ".m4v", ".3gp" };
    m_excludeFolders = { "System Volume Information", "$RECYCLE.BIN", ".git", "node_modules" };
    MetadataStore::instance().clear();
    saveConfig();
    saveMetadata();
    flushMetadataToDB();
    renderRootTags();
    startScan();
}

// ===== Header click sorting =====
void MainWindow::onHeaderClicked(int logicalIndex) {
    if (logicalIndex == BookModel::ColCheck) return;
    if (m_sortColumn == logicalIndex) {
        m_sortOrder = (m_sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
    }
    else {
        m_sortColumn = logicalIndex;
        m_sortOrder = Qt::AscendingOrder;
    }
    // 同步排序下拉框
    int comboIdx = m_sortBy->findData(logicalIndex);
    if (comboIdx >= 0) m_sortBy->setCurrentIndex(comboIdx);
    m_model->sortByColumn(logicalIndex, m_sortOrder);
}

// ===== Keyboard shortcuts =====
void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Ctrl+F: focus search
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_F) {
        m_searchBox->setFocus();
        m_searchBox->selectAll();
        event->accept();
        return;
    }
    // Ctrl+A: select all
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_A) {
        if (focusWidget() != m_searchBox) {
            selectAllVisible();
            event->accept();
            return;
        }
    }
    // Delete: delete selected
    if (event->key() == Qt::Key_Delete && focusWidget() != m_searchBox) {
        deleteSelected();
        event->accept();
        return;
    }
    // F5: refresh
    if (event->key() == Qt::Key_F5) {
        startScan();
        event->accept();
        return;
    }
    // Escape: close dialogs
    if (event->key() == Qt::Key_Escape) {
        m_searchBox->clear();
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveConfig();
    saveMetadata();
    flushMetadataToDB(); // 关闭前确保MySQL同步
    if (m_scanner && m_scanner->isRunning()) {
        m_scanner->requestInterruption();
        m_scanner->wait(3000);
    }
    event->accept();
}

// ===== Config persistence =====
QString MainWindow::configPath() const {
    return QCoreApplication::applicationDirPath() + "/.bookmanager.cfg";
}

QString MainWindow::metaPath() const {
    return QCoreApplication::applicationDirPath() + "/.bookmanager.meta";
}

void MainWindow::loadConfig() {
    QFile f(configPath());
    if (f.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(f.readAll());
        auto obj = doc.object();

        if (obj.contains("rootFolders")) {
            m_rootFolders.clear();
            for (const auto& v : obj["rootFolders"].toArray()) m_rootFolders.append(v.toString());
        }
        if (obj.contains("supportedExts")) {
            m_supportedExts.clear();
            for (const auto& v : obj["supportedExts"].toArray()) m_supportedExts.append(v.toString());
        }
        if (obj.contains("excludeFolders")) {
            m_excludeFolders.clear();
            for (const auto& v : obj["excludeFolders"].toArray()) m_excludeFolders.append(v.toString());
        }
    }

    // Validate root folders
    QMutableListIterator<QString> it(m_rootFolders);
    while (it.hasNext()) {
        if (!QDir(it.next()).exists()) it.remove();
    }
}

void MainWindow::saveConfig() {
    QJsonObject obj;
    QJsonArray roots; for (const auto& r : m_rootFolders) roots.append(r);
    obj["rootFolders"] = roots;
    QJsonArray exts; for (const auto& e : m_supportedExts) exts.append(e);
    obj["supportedExts"] = exts;
    QJsonArray excl; for (const auto& e : m_excludeFolders) excl.append(e);
    obj["excludeFolders"] = excl;

    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
}

void MainWindow::loadMetadata() {
    MetadataStore::instance().load(metaPath());
}

void MainWindow::saveMetadata() {
    MetadataStore::instance().save(metaPath());
    // 同步到MySQL（标记脏数据，实际同步由定时器或关键操作触发）
    m_metadataDirty = true;
}

void MainWindow::onSearchTextChanged() {
    QTimer::singleShot(200, this, &MainWindow::applyFilter);
}

// 在关键操作完成后检查并同步脏数据到MySQL
void MainWindow::flushMetadataToDB() {
    if (m_metadataDirty) {
        syncMetadataToDB();
        m_metadataDirty = false;
    }
}

// ===== MySQL 数据库集成 =====

void MainWindow::initDatabase() {
    QSettings settings("BookManager", "MySQL");
    m_useMySQL = settings.value("useMySQL", false).toBool();

    if (!m_useMySQL) return;

    QString host = settings.value("host", "127.0.0.1").toString();
    int port = settings.value("port", 3306).toInt();
    QString dbName = settings.value("dbName", "book_manager").toString();
    QString user = settings.value("user", "root").toString();
    QString password = settings.value("password", "").toString();

    auto& db = DatabaseManager::instance();
    if (db.initialize(host, port, dbName, user, password)) {
        qDebug() << "MySQL数据库初始化成功";
        // 从MySQL加载元数据
        loadMetadataFromDB();
        // 从MySQL恢复根目录（如果本地JSON没有配置）
        if (m_rootFolders.isEmpty()) {
            m_rootFolders = db.loadRootFoldersFromDB();
        }
    }
    else {
        qWarning() << "MySQL数据库初始化失败，回退到JSON存储:" << db.lastError();
        m_useMySQL = false;
    }
}

void MainWindow::syncMetadataToDB() {
    if (!m_useMySQL || !DatabaseManager::instance().isConnected()) return;
    DatabaseManager::instance().syncAllMetadata(MetadataStore::instance().allData());
}

void MainWindow::loadMetadataFromDB() {
    if (!m_useMySQL || !DatabaseManager::instance().isConnected()) return;

    auto dbMeta = DatabaseManager::instance().loadAllMetadata();
    // MySQL 为权威源：完整替换本地元数据（而非仅添加式合并）
    MetadataStore::instance().clear();
    for (auto it = dbMeta.begin(); it != dbMeta.end(); ++it) {
        if (!it.key().isEmpty()) {
            MetadataStore::instance().getMeta(it.key()) = it.value();
        }
    }
}

void MainWindow::updateDBStatusLabel() {
    if (!m_dbStatusLabel) return;

    if (m_useMySQL && DatabaseManager::instance().isConnected()) {
        m_dbStatusLabel->setText("🗄️ MySQL 已连接");
        m_dbStatusLabel->setStyleSheet(
            "color:#27ae60; font-size:11px; padding:3px 8px; "
            "background:#eafaf1; border:1px solid #a9dfbf; border-radius:4px;");
    }
    else if (m_useMySQL) {
        m_dbStatusLabel->setText("🗄️ MySQL 连接断开");
        m_dbStatusLabel->setStyleSheet(
            "color:#e74c3c; font-size:11px; padding:3px 8px; "
            "background:#fdedec; border:1px solid #f5b7b1; border-radius:4px;");
    }
    else {
        m_dbStatusLabel->setText("💾 JSON本地存储");
        m_dbStatusLabel->setStyleSheet(
            "color:#7f8c8d; font-size:11px; padding:3px 8px; "
            "background:#f8f9fa; border:1px solid #e9ecef; border-radius:4px;");
    }
}

void MainWindow::openDBConfig() {
    DBConfigDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        m_useMySQL = dlg.isUseMySQL();

        if (m_useMySQL) {
            // 用户启用MySQL
            auto& db = DatabaseManager::instance();
            if (db.isConnected()) {
                // 同步当前数据到MySQL
                syncMetadataToDB();
            }
        }

        updateDBStatusLabel();
    }
}

