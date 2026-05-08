/**
 * @file dbconfigdialog.h
 * @brief MySQL 数据库配置对话框 — 连接参数编辑、测试连接、保存配置
 *
 * 功能：
 *   - 开关控制是否启用 MySQL
 *   - 连接参数输入（主机/端口/数据库名/用户/密码）
 *   - 测试连接（使用独立临时连接，不污染主连接）
 *   - 保存并连接（写入 QSettings + 初始化 DatabaseManager）
 *
 * 配置持久化：QSettings("BookManager", "MySQL")
 *
 * ⚠️ 安全注意：密码以明文存储在 QSettings 中。
 *    生产环境建议使用系统密钥链（macOS Keychain / Windows DPAPI / Linux Secret Service）。
 *
 * @version 3.0
 */

#ifndef DBCONFIGDIALOG_H
#define DBCONFIGDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSettings>
#include "databasemanager.h"

class DBConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit DBConfigDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("⚙️ MySQL 数据库配置");
        setMinimumWidth(460);
        loadSavedConfig();
        setupUI();
    }

    bool isUseMySQL() const { return m_useMySQL->isChecked(); }
    QString host() const { return m_hostEdit->text().trimmed(); }
    int port() const { return m_portSpin->value(); }
    QString dbName() const { return m_dbNameEdit->text().trimmed(); }
    QString user() const { return m_userEdit->text().trimmed(); }
    QString password() const { return m_passEdit->text(); }

private:
    void setupUI() {
        auto* mainLayout = new QVBoxLayout(this);

        // MySQL 开关
        m_useMySQL = new QCheckBox("启用 MySQL 数据库存储（关闭则使用本地JSON文件）");
        m_useMySQL->setChecked(m_savedUseMySQL);
        m_useMySQL->setStyleSheet("font-size:13px; font-weight:bold; padding:5px;");
        mainLayout->addWidget(m_useMySQL);

        // 连接参数组
        auto* groupBox = new QGroupBox("数据库连接参数");
        auto* formLayout = new QFormLayout(groupBox);

        m_hostEdit = new QLineEdit(m_savedHost);
        m_hostEdit->setPlaceholderText("如: 127.0.0.1 或 localhost");
        formLayout->addRow("主机地址:", m_hostEdit);

        m_portSpin = new QSpinBox;
        m_portSpin->setRange(1, 65535);
        m_portSpin->setValue(m_savedPort);
        formLayout->addRow("端口:", m_portSpin);

        m_dbNameEdit = new QLineEdit(m_savedDbName);
        m_dbNameEdit->setPlaceholderText("数据库名，如 book_manager");
        formLayout->addRow("数据库名:", m_dbNameEdit);

        m_userEdit = new QLineEdit(m_savedUser);
        m_userEdit->setPlaceholderText("MySQL 用户名");
        formLayout->addRow("用户名:", m_userEdit);

        m_passEdit = new QLineEdit(m_savedPassword);
        m_passEdit->setEchoMode(QLineEdit::Password);
        m_passEdit->setPlaceholderText("MySQL 密码");
        formLayout->addRow("密码:", m_passEdit);

        mainLayout->addWidget(groupBox);

        // 提示
        auto* hint = new QLabel(
            "💡 提示：连接成功后将自动创建所需的数据表，无需手动建表\n"
            "   请确保MySQL服务已启动且指定的数据库已存在"
        );
        hint->setStyleSheet("color:#666; font-size:11px; padding:5px; background:#f8f9fa; border-radius:4px;");
        hint->setWordWrap(true);
        mainLayout->addWidget(hint);

        // 测试连接按钮
        auto* testBtn = new QPushButton("🔗 测试连接");
        testBtn->setStyleSheet(
            "QPushButton { background:#17a2b8; color:white; border:none; border-radius:8px; "
            "padding:8px 20px; font-weight:bold; }"
            "QPushButton:hover { background:#138496; }");
        mainLayout->addWidget(testBtn, 0, Qt::AlignCenter);
        connect(testBtn, &QPushButton::clicked, this, &DBConfigDialog::testConnection);

        // 底部按钮
        auto* btnLayout = new QHBoxLayout;
        auto* saveBtn = new QPushButton("保存并连接");
        saveBtn->setStyleSheet(
            "QPushButton { background:#27ae60; color:white; border:none; border-radius:8px; "
            "padding:10px 25px; font-weight:bold; font-size:13px; }"
            "QPushButton:hover { background:#219a52; }");
        auto* cancelBtn = new QPushButton("取消");
        cancelBtn->setStyleSheet(
            "QPushButton { background:#6c757d; color:white; border:none; border-radius:8px; "
            "padding:10px 25px; }"
            "QPushButton:hover { background:#5a6268; }");
        btnLayout->addStretch();
        btnLayout->addWidget(saveBtn);
        btnLayout->addWidget(cancelBtn);
        mainLayout->addLayout(btnLayout);

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, &DBConfigDialog::saveAndConnect);

        // 开关联动：取消勾选时禁用连接参数输入
        connect(m_useMySQL, &QCheckBox::toggled, groupBox, &QWidget::setEnabled);
        groupBox->setEnabled(m_useMySQL->isChecked());
    }

    void loadSavedConfig() {
        QSettings settings("BookManager", "MySQL");
        m_savedUseMySQL = settings.value("useMySQL", false).toBool();
        m_savedHost = settings.value("host", "127.0.0.1").toString();
        m_savedPort = settings.value("port", 3306).toInt();
        m_savedDbName = settings.value("dbName", "book_manager").toString();
        m_savedUser = settings.value("user", "root").toString();
        m_savedPassword = settings.value("password", "").toString();
    }

    void saveConfig() {
        QSettings settings("BookManager", "MySQL");
        settings.setValue("useMySQL", m_useMySQL->isChecked());
        settings.setValue("host", host());
        settings.setValue("port", port());
        settings.setValue("dbName", dbName());
        settings.setValue("user", user());
        settings.setValue("password", password());
    }

    /**
     * @brief 使用独立临时连接测试
     *
     * ⚠️ 关键：使用不同的连接名 "test_connection"，避免覆盖主连接 "book_manager"。
     *    测试完成后 removeDatabase 清理临时连接。
     */
    void testConnection() {
        if (!m_useMySQL->isChecked()) {
            QMessageBox::information(this, "提示", "请先勾选「启用 MySQL」");
            return;
        }

        QSqlDatabase testDb = QSqlDatabase::addDatabase("QMYSQL", "test_connection");
        testDb.setHostName(host());
        testDb.setPort(port());
        testDb.setDatabaseName(dbName());
        testDb.setUserName(user());
        testDb.setPassword(password());

        if (testDb.open()) {
            QMessageBox::information(this, "✅ 连接成功", "MySQL 数据库连接测试通过！");
            testDb.close();
        }
        else {
            QMessageBox::warning(this, "❌ 连接失败",
                QString("无法连接到 MySQL：\n%1").arg(testDb.lastError().text()));
        }
        QSqlDatabase::removeDatabase("test_connection");
    }

    void saveAndConnect() {
        saveConfig();

        if (!m_useMySQL->isChecked()) {
            accept();
            return;
        }

        auto& db = DatabaseManager::instance();
        if (db.initialize(host(), port(), dbName(), user(), password())) {
            accept();
        }
        else {
            QMessageBox::warning(this, "连接失败",
                QString("无法连接到 MySQL：\n%1\n\n请检查配置后重试。").arg(db.lastError()));
        }
    }

    // UI 元素
    QCheckBox* m_useMySQL;
    QLineEdit* m_hostEdit;
    QSpinBox* m_portSpin;
    QLineEdit* m_dbNameEdit;
    QLineEdit* m_userEdit;
    QLineEdit* m_passEdit;

    // 保存的配置（从 QSettings 加载）
    bool m_savedUseMySQL;
    QString m_savedHost;
    int m_savedPort;
    QString m_savedDbName;
    QString m_savedUser;
    QString m_savedPassword;
};

#endif // DBCONFIGDIALOG_H
