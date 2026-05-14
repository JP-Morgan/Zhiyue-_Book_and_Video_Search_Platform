# 知阅 · 书籍视频检索平台


> 一款功能强大的本地书籍/视频文件管理工具，支持多目录扫描、智能筛选、标签管理和数据库同步。

---

## 功能特性

### 📁 文件扫描
- 支持多目录同时扫描
- 后台线程异步扫描，不阻塞UI
- 支持常见书籍和视频格式（PDF、EPUB、MOBI、MP4、MKV等）

### 🔍 智能搜索与筛选
- 关键词搜索（支持正则表达式）
- 按文件类型、标签、收藏状态筛选
- 按名称/大小/日期多维排序
- 文件夹级联筛选

### 🏷️ 元数据管理
- 自定义标签系统
- 阅读进度追踪（0-100%）
- 收藏标记功能
- 元数据持久化存储

### 📊 统计分析
- 文件数量统计
- 分类占比分析
- 阅读进度概览

### 📤 导出功能
- CSV格式导出
- TXT格式导出
- 一键复制到剪贴板

### 🗄️ 数据同步
- 可选MySQL数据库双写同步
- JSON本地备份
- 配置文件导入导出

### ✨ 批量操作
- 批量重命名
- 批量删除
- 批量标签管理

### 🤖 AI 智能分析
- 支持 OpenAI 兼容 API（OpenAI / DeepSeek / 通义千问 / 本地模型）
- 三种分析模式：读书大纲 / 详细介绍 / 思路理解
- 流式输出（SSE），实时显示生成内容
- 支持自定义 Prompt
- 生成结果可保存为 Markdown 文件 / 复制到剪贴板
- API 配置持久化（endpoint / key / model）

---

## 技术栈

- **框架**: Qt 6.x (QtWidgets)
- **语言**: C++17
- **数据库**: SQLite (默认) / MySQL (可选)
- **UI风格**: Fusion (跨平台一致外观)

---

## 架构设计

### 核心模块

| 模块 | 职责 | 关键特性 |
|------|------|----------|
| **MainWindow** | 主窗口，UI整合 | Model/View架构、MySQL双写同步、搜索防抖(200ms) |
| **BookScanner** | 后台文件扫描 | 磁盘类型探测(HDD/SSD)、智能多线程策略、QDirIterator流式遍历 |
| **BookModel** | 表格数据模型 | 双层数据(m_all/m_visible)、多维筛选、自定义排序 |
| **MetadataStore** | 元数据管理 | 单例模式、JSON持久化、惰性创建 |
| **AiAnalysisDialog** | AI分析对话框 | OpenAI兼容API、SSE流式输出、三种分析模式 |

### 数据流转

```
BookScanner(后台线程) → BookInfo列表 → BookModel::setBooks()
                                           ↓
                    m_all(完整列表) ──筛选──> m_visible(可见列表)
                                               ↓
                                         QTableView显示
                                               ↓
                                    用户操作 → MetadataStore(内存+JSON)
                                                    ↓
                                         MySQL同步(可选)
```

### 技术亮点

1. **智能扫描策略**：根据磁盘类型自动选择单/多线程，HDD用单线程避免磁头跳跃，SSD用多线程提高效率

2. **双层数据架构**：`m_all` 存储完整扫描结果，`m_visible` 存储筛选后结果，支持快速切换筛选条件

3. **元数据双写**：同时写入JSON文件和MySQL，保证数据安全

4. **AI流式输出**：支持SSE(Server-Sent Events)实时显示生成内容

5. **搜索防抖**：200ms延迟避免频繁搜索，提升用户体验

---

## 快速开始

### 环境要求

- Qt 6.5+ (推荐 6.5.x)
- Visual Studio 2022 (Windows)
- MySQL 8.0+ (如需启用数据库同步)

### 编译运行

```bash
# 克隆项目
git clone <repository-url>
cd My_book

# 使用Qt Creator打开项目
# 或使用MSBuild编译
msbuild My_book.slnx /p:Configuration=Release /p:Platform=x64
```

### 直接运行

已编译的可执行文件位于：
- `x64/Release/My_book.exe` - Release版本
- `x64/Debug/My_book.exe` - Debug版本

---

## 使用说明

### 基本操作

1. **添加目录**: 点击工具栏按钮添加书籍/视频所在目录
2. **开始扫描**: 点击扫描按钮，后台自动扫描文件
3. **搜索筛选**: 使用搜索框和下拉菜单进行筛选
4. **双击打开**: 双击列表项直接打开文件

### 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+F` | 聚焦搜索框 |
| `Ctrl+A` | 全选可见项 |
| `Delete` | 删除选中项 |
| `Ctrl+S` | 保存数据 |

---

## 配置说明

### MySQL配置（可选）

1. 点击设置按钮打开数据库配置对话框
2. 输入MySQL连接信息：
   - 主机地址
   - 端口（默认3306）
   - 用户名
   - 密码
   - 数据库名
3. 测试连接成功后启用双写同步

### 配置文件

配置文件位于程序运行目录：
- `.bookmanager.cfg` - 应用配置
- `.bookmanager.meta` - 元数据存储

---

## 项目结构

```
My_book/
├── Image/              # 图标资源
├── x64/                # 编译输出
│   ├── Debug/          # Debug版本
│   ├── Release/        # Release版本
│   └── 知阅 · 书籍视频检索平台/  # 发布包
├── aidialog.cpp/h      # AI对话框
├── batchrenamedialog.cpp/h  # 批量重命名对话框
├── bookmodel.cpp/h     # 书籍数据模型
├── bookscanner.cpp/h   # 文件扫描器
├── databasemanager.h   # 数据库管理器
├── dbconfigdialog.cpp/h # 数据库配置对话框
├── main.cpp            # 应用入口
├── mainwindow.cpp/h    # 主窗口
├── metadata.cpp/h      # 元数据管理
├── statisticsdialog.cpp/h # 统计对话框
├── tagdialog.cpp/h     # 标签对话框
└── mainwindow.ui       # UI设计文件
```

---

## 版本历史

| 版本 | 更新内容 | 日期 |
|------|----------|------|
| v3.5 | 添加MySQL同步、统计分析 | 2026.4 |
| v3.0 | 重构UI、添加批量重命名 | 2026.3.25 |
| v2.0 | 添加标签系统、阅读进度 | 2026.3.15 |
| v1.0 | 基础文件扫描与搜索 | 2026.3.13 |

---

---

## 贡献

欢迎提交Issue和Pull Request！

---
