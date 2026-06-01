#include "trainingrepository.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString dateOnly(const QDate &date)
{
    return date.toString(Qt::ISODate);
}

QString safeText(const QString &value, const QString &fallback)
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

void updateWeakestMetric(TrainingTrendWindow *trend)
{
    if (!trend) {
        return;
    }

    const QVector<std::pair<QString, int>> metrics = {
        {QStringLiteral("关键点"), trend->detectionScore},
        {QStringLiteral("对称"), trend->symmetryScore},
        {QStringLiteral("重心"), trend->balanceScore},
        {QStringLiteral("稳定"), trend->stabilityScore},
        {QStringLiteral("3D"), trend->depthScore}
    };

    auto weakest = metrics.cbegin();
    for (auto it = metrics.cbegin(); it != metrics.cend(); ++it) {
        if (it->second < weakest->second) {
            weakest = it;
        }
    }
    trend->weakestMetricName = weakest->first;
    trend->weakestMetricScore = weakest->second;
}

bool bindAndExec(QSqlQuery &query, const QVariantList &args)
{
    for (const QVariant &arg : args) {
        query.addBindValue(arg);
    }
    return query.exec();
}

} // namespace

TrainingRepository::TrainingRepository()
    : m_connectionName(QStringLiteral("iskating_training_%1").arg(newId()))
{
}

TrainingRepository::~TrainingRepository()
{
    if (m_db.isValid()) {
        const QString connectionName = m_db.connectionName();
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool TrainingRepository::open(QString *errorMessage)
{
    if (m_db.isOpen()) {
        return true;
    }

    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.trimmed().isEmpty()) {
        appDataPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data"));
    }
    QDir dir(appDataPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        m_lastError = QStringLiteral("无法创建数据目录：%1").arg(appDataPath);
        if (errorMessage) {
            *errorMessage = m_lastError;
        }
        return false;
    }

    m_databasePath = dir.absoluteFilePath(QStringLiteral("iskating.db"));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_databasePath);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        if (errorMessage) {
            *errorMessage = m_lastError;
        }
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    if (!migrate(errorMessage)) {
        return false;
    }
    if (!seedDefaults(errorMessage)) {
        return false;
    }
    if (!migrateLegacyTrainingHistory(errorMessage)) {
        return false;
    }

    m_lastError.clear();
    return true;
}

bool TrainingRepository::isOpen() const
{
    return m_db.isOpen();
}

QString TrainingRepository::databasePath() const
{
    return m_databasePath;
}

QString TrainingRepository::lastError() const
{
    return m_lastError;
}

bool TrainingRepository::execute(const QString &sql, QString *errorMessage) const
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        const QString error = query.lastError().text();
        if (errorMessage) {
            *errorMessage = error;
        }
        return false;
    }
    return true;
}

bool TrainingRepository::migrate(QString *errorMessage)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_meta ("
                       "key TEXT PRIMARY KEY,"
                       "value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS athletes ("
                       "id TEXT PRIMARY KEY,"
                       "name TEXT NOT NULL,"
                       "code TEXT,"
                       "age_group TEXT,"
                       "height_cm REAL DEFAULT 0,"
                       "weight_kg REAL DEFAULT 0,"
                       "discipline TEXT,"
                       "level TEXT,"
                       "preferred_rotation TEXT,"
                       "preferred_takeoff_foot TEXT,"
                       "injury_notes TEXT,"
                       "goals TEXT,"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS coaches ("
                       "id TEXT PRIMARY KEY,"
                       "name TEXT NOT NULL,"
                       "code TEXT,"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS coach_athletes ("
                       "coach_id TEXT NOT NULL,"
                       "athlete_id TEXT NOT NULL,"
                       "created_at TEXT NOT NULL,"
                       "PRIMARY KEY (coach_id, athlete_id),"
                       "FOREIGN KEY (coach_id) REFERENCES coaches(id),"
                       "FOREIGN KEY (athlete_id) REFERENCES athletes(id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS action_categories ("
                       "id TEXT PRIMARY KEY,"
                       "code TEXT NOT NULL UNIQUE,"
                       "name TEXT NOT NULL,"
                       "sort_order INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS action_standards ("
                       "id TEXT PRIMARY KEY,"
                       "code TEXT NOT NULL UNIQUE,"
                       "name TEXT NOT NULL,"
                       "category_id TEXT NOT NULL,"
                       "level TEXT,"
                       "purpose TEXT,"
                       "version INTEGER NOT NULL DEFAULT 1,"
                       "target_reps INTEGER NOT NULL DEFAULT 10,"
                       "target_score INTEGER NOT NULL DEFAULT 80,"
                       "set_count INTEGER NOT NULL DEFAULT 1,"
                       "rest_seconds INTEGER NOT NULL DEFAULT 60,"
                       "arm_threshold REAL NOT NULL DEFAULT 0.28,"
                       "release_threshold REAL NOT NULL DEFAULT 0.18,"
                       "debounce_ms INTEGER NOT NULL DEFAULT 900,"
                       "detection_weight REAL NOT NULL DEFAULT 0.28,"
                       "symmetry_weight REAL NOT NULL DEFAULT 0.18,"
                       "balance_weight REAL NOT NULL DEFAULT 0.22,"
                       "stability_weight REAL NOT NULL DEFAULT 0.17,"
                       "depth_weight REAL NOT NULL DEFAULT 0.15,"
                       "detection_min INTEGER NOT NULL DEFAULT 55,"
                       "symmetry_min INTEGER NOT NULL DEFAULT 60,"
                       "balance_min INTEGER NOT NULL DEFAULT 60,"
                       "stability_min INTEGER NOT NULL DEFAULT 60,"
                       "depth_min INTEGER NOT NULL DEFAULT 55,"
                       "phases TEXT,"
                       "key_points TEXT,"
                       "issue_title TEXT,"
                       "issue_body_part TEXT,"
                       "issue_cause TEXT,"
                       "issue_correction TEXT,"
                       "issue_priority INTEGER NOT NULL DEFAULT 2,"
                       "active INTEGER NOT NULL DEFAULT 1,"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL,"
                       "FOREIGN KEY (category_id) REFERENCES action_categories(id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS training_plans ("
                       "id TEXT PRIMARY KEY,"
                       "athlete_id TEXT NOT NULL,"
                       "coach_id TEXT,"
                       "name TEXT NOT NULL,"
                       "training_date TEXT NOT NULL,"
                       "site TEXT,"
                       "training_phase TEXT,"
                       "goal TEXT,"
                       "status TEXT NOT NULL DEFAULT 'active',"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL,"
                       "FOREIGN KEY (athlete_id) REFERENCES athletes(id),"
                       "FOREIGN KEY (coach_id) REFERENCES coaches(id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS training_tasks ("
                       "id TEXT PRIMARY KEY,"
                       "plan_id TEXT NOT NULL,"
                       "action_standard_id TEXT NOT NULL,"
                       "standard_version INTEGER NOT NULL DEFAULT 1,"
                       "target_reps INTEGER NOT NULL DEFAULT 10,"
                       "target_score INTEGER NOT NULL DEFAULT 80,"
                       "set_count INTEGER NOT NULL DEFAULT 1,"
                       "rest_seconds INTEGER NOT NULL DEFAULT 60,"
                       "status TEXT NOT NULL DEFAULT 'active',"
                       "sort_order INTEGER NOT NULL DEFAULT 0,"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL,"
                       "FOREIGN KEY (plan_id) REFERENCES training_plans(id),"
                       "FOREIGN KEY (action_standard_id) REFERENCES action_standards(id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS training_sessions ("
                       "id TEXT PRIMARY KEY,"
                       "athlete_id TEXT NOT NULL,"
                       "coach_id TEXT,"
                       "plan_id TEXT,"
                       "task_id TEXT,"
                       "action_standard_id TEXT NOT NULL,"
                       "standard_version INTEGER NOT NULL DEFAULT 1,"
                       "legacy_qsettings_id INTEGER DEFAULT 0,"
                       "started_at TEXT NOT NULL,"
                       "saved_at TEXT NOT NULL,"
                       "duration_sec INTEGER NOT NULL DEFAULT 0,"
                       "total_reps INTEGER NOT NULL DEFAULT 0,"
                       "valid_reps INTEGER NOT NULL DEFAULT 0,"
                       "average_score INTEGER NOT NULL DEFAULT 0,"
                       "best_score INTEGER NOT NULL DEFAULT 0,"
                       "camera INTEGER NOT NULL DEFAULT 1,"
                       "model_precision TEXT,"
                       "fps INTEGER NOT NULL DEFAULT 30,"
                       "detection_score INTEGER NOT NULL DEFAULT 0,"
                       "symmetry_score INTEGER NOT NULL DEFAULT 0,"
                       "balance_score INTEGER NOT NULL DEFAULT 0,"
                       "stability_score INTEGER NOT NULL DEFAULT 0,"
                       "depth_score INTEGER NOT NULL DEFAULT 0,"
                       "site TEXT,"
                       "training_phase TEXT,"
                       "goal TEXT,"
                       "target_reps INTEGER NOT NULL DEFAULT 0,"
                       "target_score INTEGER NOT NULL DEFAULT 0,"
                       "set_count INTEGER NOT NULL DEFAULT 1,"
                       "rest_seconds INTEGER NOT NULL DEFAULT 60,"
                       "video_source TEXT,"
                       "video_fallback_source TEXT,"
                       "video_camera_name TEXT,"
                       "feedback TEXT,"
                       "notes TEXT,"
                       "coach_comment TEXT,"
                       "FOREIGN KEY (athlete_id) REFERENCES athletes(id),"
                       "FOREIGN KEY (coach_id) REFERENCES coaches(id),"
                       "FOREIGN KEY (plan_id) REFERENCES training_plans(id),"
                       "FOREIGN KEY (task_id) REFERENCES training_tasks(id),"
                       "FOREIGN KEY (action_standard_id) REFERENCES action_standards(id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS action_repetitions ("
                       "id TEXT PRIMARY KEY,"
                       "session_id TEXT NOT NULL,"
                       "action_standard_id TEXT NOT NULL,"
                       "standard_version INTEGER NOT NULL DEFAULT 1,"
                       "started_ms INTEGER NOT NULL DEFAULT 0,"
                       "ended_ms INTEGER NOT NULL DEFAULT 0,"
                       "valid INTEGER NOT NULL DEFAULT 0,"
                       "score INTEGER NOT NULL DEFAULT 0,"
                       "detection_score INTEGER NOT NULL DEFAULT 0,"
                       "symmetry_score INTEGER NOT NULL DEFAULT 0,"
                       "balance_score INTEGER NOT NULL DEFAULT 0,"
                       "stability_score INTEGER NOT NULL DEFAULT 0,"
                       "depth_score INTEGER NOT NULL DEFAULT 0,"
                       "error_codes TEXT,"
                       "feedback TEXT,"
                       "key_frame_ms INTEGER NOT NULL DEFAULT 0,"
                       "video_clip_start_ms INTEGER NOT NULL DEFAULT 0,"
                       "video_clip_end_ms INTEGER NOT NULL DEFAULT 0,"
                       "FOREIGN KEY (session_id) REFERENCES training_sessions(id),"
                       "FOREIGN KEY (action_standard_id) REFERENCES action_standards(id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS athlete_action_baselines ("
                       "athlete_id TEXT NOT NULL,"
                       "action_standard_id TEXT NOT NULL,"
                       "session_count INTEGER NOT NULL DEFAULT 0,"
                       "average_score INTEGER NOT NULL DEFAULT 0,"
                       "average_valid_reps INTEGER NOT NULL DEFAULT 0,"
                       "updated_at TEXT NOT NULL,"
                       "PRIMARY KEY (athlete_id, action_standard_id),"
                       "FOREIGN KEY (athlete_id) REFERENCES athletes(id),"
                       "FOREIGN KEY (action_standard_id) REFERENCES action_standards(id))")
    };

    for (const QString &statement : statements) {
        if (!execute(statement, errorMessage)) {
            return false;
        }
    }
    if (!ensureColumn(QStringLiteral("training_sessions"),
                      QStringLiteral("video_source"),
                      QStringLiteral("TEXT"),
                      errorMessage)
        || !ensureColumn(QStringLiteral("training_sessions"),
                         QStringLiteral("video_fallback_source"),
                         QStringLiteral("TEXT"),
                         errorMessage)
        || !ensureColumn(QStringLiteral("training_sessions"),
                         QStringLiteral("video_camera_name"),
                         QStringLiteral("TEXT"),
                         errorMessage)
        || !ensureColumn(QStringLiteral("training_sessions"),
                         QStringLiteral("coach_comment"),
                         QStringLiteral("TEXT"),
                         errorMessage)
        || !ensureColumn(QStringLiteral("action_repetitions"),
                         QStringLiteral("video_clip_start_ms"),
                         QStringLiteral("INTEGER NOT NULL DEFAULT 0"),
                         errorMessage)
        || !ensureColumn(QStringLiteral("action_repetitions"),
                         QStringLiteral("video_clip_end_ms"),
                         QStringLiteral("INTEGER NOT NULL DEFAULT 0"),
                         errorMessage)) {
        return false;
    }
    return setMetaValue(QStringLiteral("schemaVersion"), QStringLiteral("2"), errorMessage);
}

bool TrainingRepository::seedDefaults(QString *errorMessage)
{
    if (!m_db.transaction()) {
        if (errorMessage) {
            *errorMessage = m_db.lastError().text();
        }
        return false;
    }

    auto rollback = [this, errorMessage](const QString &message) {
        m_db.rollback();
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO athletes "
                                 "(id, name, code, age_group, discipline, level, goals, created_at, updated_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    if (!bindAndExec(query,
                     {QStringLiteral("athlete_default"),
                      QStringLiteral("默认运动员"),
                      QStringLiteral("ATH-DEFAULT"),
                      QStringLiteral("青年组"),
                      QStringLiteral("滑冰"),
                      QStringLiteral("基础"),
                      QStringLiteral("建立稳定动作标准闭环"),
                      nowIso(),
                      nowIso()})) {
        return rollback(query.lastError().text());
    }

    query.prepare(QStringLiteral("INSERT OR IGNORE INTO coaches "
                                 "(id, name, code, created_at, updated_at) VALUES (?, ?, ?, ?, ?)"));
    if (!bindAndExec(query,
                     {QStringLiteral("coach_default"),
                      QStringLiteral("默认教练"),
                      QStringLiteral("COACH-DEFAULT"),
                      nowIso(),
                      nowIso()})) {
        return rollback(query.lastError().text());
    }

    query.prepare(QStringLiteral("INSERT OR IGNORE INTO coach_athletes "
                                 "(coach_id, athlete_id, created_at) VALUES (?, ?, ?)"));
    if (!bindAndExec(query,
                     {QStringLiteral("coach_default"),
                      QStringLiteral("athlete_default"),
                      nowIso()})) {
        return rollback(query.lastError().text());
    }

    struct CategorySeed {
        QString id;
        QString code;
        QString name;
        int order = 0;
    };
    const QVector<CategorySeed> categories = {
        {QStringLiteral("cat_basic_glide"), QStringLiteral("basic_glide"), QStringLiteral("基础滑行"), 10},
        {QStringLiteral("cat_push"), QStringLiteral("push"), QStringLiteral("蹬冰"), 20},
        {QStringLiteral("cat_cross"), QStringLiteral("cross"), QStringLiteral("压步"), 30},
        {QStringLiteral("cat_turn_prep"), QStringLiteral("turn_prep"), QStringLiteral("转体准备"), 40},
        {QStringLiteral("cat_jump_prep"), QStringLiteral("jump_prep"), QStringLiteral("跳跃准备"), 50},
        {QStringLiteral("cat_landing"), QStringLiteral("landing"), QStringLiteral("落冰控制"), 60},
        {QStringLiteral("cat_spin"), QStringLiteral("spin"), QStringLiteral("旋转姿态"), 70},
        {QStringLiteral("cat_steps"), QStringLiteral("steps"), QStringLiteral("步法"), 80}
    };

    for (const CategorySeed &category : categories) {
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO action_categories "
                                     "(id, code, name, sort_order) VALUES (?, ?, ?, ?)"));
        if (!bindAndExec(query, {category.id, category.code, category.name, category.order})) {
            return rollback(query.lastError().text());
        }
    }

    struct StandardSeed {
        QString id;
        QString code;
        QString name;
        QString categoryId;
        QString level;
        QString purpose;
        int targetReps;
        int targetScore;
        int setCount;
        int restSeconds;
        double arm;
        double release;
        int debounce;
        double detectionWeight;
        double symmetryWeight;
        double balanceWeight;
        double stabilityWeight;
        double depthWeight;
        int detectionMin;
        int symmetryMin;
        int balanceMin;
        int stabilityMin;
        int depthMin;
        QString phases;
        QString keyPoints;
        QString issueTitle;
        QString issueBodyPart;
        QString issueCause;
        QString issueCorrection;
        int priority;
    };

    const QVector<StandardSeed> standards = {
        {QStringLiteral("std_basic_edge_glide"), QStringLiteral("basic_edge_glide"), QStringLiteral("基础外刃滑行"),
         QStringLiteral("cat_basic_glide"), QStringLiteral("基础"), QStringLiteral("建立单脚滑行重心和刃感"), 12, 78, 2, 45,
         0.26, 0.17, 850, 0.24, 0.20, 0.28, 0.16, 0.12, 58, 62, 66, 58, 45,
         QStringLiteral("准备|压刃|滑行保持|换刃恢复"), QStringLiteral("髋肩保持同向，膝踝持续柔和，重心落在支撑脚上方"),
         QStringLiteral("重心偏外"), QStringLiteral("髋-膝-踝"), QStringLiteral("支撑侧髋线与身体中心偏离"),
         QStringLiteral("降低速度，先做单脚支撑和髋部对齐，再恢复滑行节奏"), 1},
        {QStringLiteral("std_push_extension"), QStringLiteral("push_extension"), QStringLiteral("蹬冰伸展"),
         QStringLiteral("cat_push"), QStringLiteral("基础"), QStringLiteral("提升蹬伸完整度和左右发力一致性"), 16, 80, 3, 45,
         0.30, 0.18, 800, 0.22, 0.24, 0.22, 0.18, 0.14, 58, 65, 62, 60, 45,
         QStringLiteral("准备|屈膝蓄力|蹬伸|收腿恢复"), QStringLiteral("蹬冰腿充分伸展，肩胯稳定，回收腿不拖沓"),
         QStringLiteral("蹬伸不完整"), QStringLiteral("膝关节"), QStringLiteral("屈伸幅度不足或左右节奏不一致"),
         QStringLiteral("放慢节奏，完整做出屈膝蓄力到蹬伸，再逐步提速"), 1},
        {QStringLiteral("std_cross_under"), QStringLiteral("cross_under"), QStringLiteral("压步重心转换"),
         QStringLiteral("cat_cross"), QStringLiteral("进阶"), QStringLiteral("强化压步时肩胯同步和内外侧支撑转换"), 12, 82, 3, 60,
         0.31, 0.19, 950, 0.22, 0.24, 0.26, 0.16, 0.12, 58, 66, 66, 58, 45,
         QStringLiteral("入弯|交叉压步|重心转移|出弯恢复"), QStringLiteral("肩胯转向一致，交叉腿落点清晰，身体轴线稳定"),
         QStringLiteral("肩胯不同步"), QStringLiteral("肩胯"), QStringLiteral("上身提前或滞后导致重心转移不稳"),
         QStringLiteral("先降低入弯速度，盯住肩胯同向后再增加压步频率"), 1},
        {QStringLiteral("std_turn_preparation"), QStringLiteral("turn_preparation"), QStringLiteral("转体准备姿态"),
         QStringLiteral("cat_turn_prep"), QStringLiteral("进阶"), QStringLiteral("稳定转体前轴线和上肢预备位置"), 10, 80, 2, 60,
         0.25, 0.17, 1000, 0.24, 0.24, 0.24, 0.18, 0.10, 58, 66, 64, 60, 45,
         QStringLiteral("滑入|预备收紧|轴线建立|释放恢复"), QStringLiteral("肩线水平，核心收紧，头肩髋保持轴线"),
         QStringLiteral("身体轴线偏移"), QStringLiteral("头肩髋"), QStringLiteral("转体准备时上身摆动过大"),
         QStringLiteral("先做低速轴线保持，再加入转体预备摆臂"), 1},
        {QStringLiteral("std_jump_takeoff_prep"), QStringLiteral("jump_takeoff_prep"), QStringLiteral("跳跃起跳准备"),
         QStringLiteral("cat_jump_prep"), QStringLiteral("进阶"), QStringLiteral("建立起跳前屈膝、摆臂和重心组织"), 10, 82, 3, 75,
         0.32, 0.18, 1000, 0.24, 0.18, 0.28, 0.18, 0.12, 60, 62, 68, 60, 45,
         QStringLiteral("滑入|屈膝蓄力|摆臂协同|起跳释放"), QStringLiteral("支撑腿屈膝充分，摆臂不过早，重心不过后"),
         QStringLiteral("摆臂提前"), QStringLiteral("肩臂"), QStringLiteral("上肢先于下肢释放导致起跳重心散"),
         QStringLiteral("用节拍口令重做屈膝-摆臂-释放顺序"), 1},
        {QStringLiteral("std_landing_control"), QStringLiteral("landing_control"), QStringLiteral("落冰控制"),
         QStringLiteral("cat_landing"), QStringLiteral("进阶"), QStringLiteral("提升落冰后单脚支撑和缓冲稳定性"), 8, 84, 3, 75,
         0.30, 0.19, 1100, 0.24, 0.18, 0.30, 0.18, 0.10, 60, 60, 70, 62, 45,
         QStringLiteral("落冰前|触冰缓冲|滑出保持|恢复"), QStringLiteral("落冰膝踝缓冲，髋不过度外甩，滑出保持三拍"),
         QStringLiteral("落冰不稳"), QStringLiteral("支撑腿"), QStringLiteral("落冰后重心漂移或缓冲不足"),
         QStringLiteral("减少进入速度，先把落冰滑出保持到三拍"), 1},
        {QStringLiteral("std_spin_axis_hold"), QStringLiteral("spin_axis_hold"), QStringLiteral("旋转轴线保持"),
         QStringLiteral("cat_spin"), QStringLiteral("进阶"), QStringLiteral("稳定旋转姿态轴线和上肢收紧"), 8, 82, 2, 75,
         0.24, 0.16, 1200, 0.24, 0.26, 0.22, 0.18, 0.10, 58, 68, 62, 60, 45,
         QStringLiteral("进入|轴线建立|保持|退出"), QStringLiteral("肩髋垂直对齐，手臂收紧，头部不抢转"),
         QStringLiteral("轴线晃动"), QStringLiteral("头肩髋"), QStringLiteral("旋转中肩髋宽度变化和身体摆动过大"),
         QStringLiteral("先做慢速轴线保持，稳定后再增加旋转速度"), 1},
        {QStringLiteral("std_step_sequence"), QStringLiteral("step_sequence"), QStringLiteral("步法节奏控制"),
         QStringLiteral("cat_steps"), QStringLiteral("基础"), QStringLiteral("保持步法节奏和左右连接稳定"), 18, 78, 2, 45,
         0.27, 0.18, 700, 0.24, 0.22, 0.22, 0.20, 0.12, 56, 62, 60, 62, 45,
         QStringLiteral("准备|换步|连接|恢复"), QStringLiteral("换步落点清晰，节奏均匀，身体不过度摆动"),
         QStringLiteral("节奏不稳"), QStringLiteral("脚步/躯干"), QStringLiteral("步法连接中稳定分下降"),
         QStringLiteral("用固定节拍完成同一组步法，再逐步提高速度"), 2}
    };

    for (const StandardSeed &standard : standards) {
        query.prepare(QStringLiteral(
            "INSERT INTO action_standards ("
            "id, code, name, category_id, level, purpose, version, target_reps, target_score, set_count, rest_seconds,"
            "arm_threshold, release_threshold, debounce_ms,"
            "detection_weight, symmetry_weight, balance_weight, stability_weight, depth_weight,"
            "detection_min, symmetry_min, balance_min, stability_min, depth_min,"
            "phases, key_points, issue_title, issue_body_part, issue_cause, issue_correction, issue_priority,"
            "active, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?) "
            "ON CONFLICT(code) DO UPDATE SET "
            "name=excluded.name, category_id=excluded.category_id, level=excluded.level, purpose=excluded.purpose,"
            "target_reps=excluded.target_reps, target_score=excluded.target_score, set_count=excluded.set_count,"
            "rest_seconds=excluded.rest_seconds, arm_threshold=excluded.arm_threshold, release_threshold=excluded.release_threshold,"
            "debounce_ms=excluded.debounce_ms, detection_weight=excluded.detection_weight, symmetry_weight=excluded.symmetry_weight,"
            "balance_weight=excluded.balance_weight, stability_weight=excluded.stability_weight, depth_weight=excluded.depth_weight,"
            "detection_min=excluded.detection_min, symmetry_min=excluded.symmetry_min, balance_min=excluded.balance_min,"
            "stability_min=excluded.stability_min, depth_min=excluded.depth_min, phases=excluded.phases,"
            "key_points=excluded.key_points, issue_title=excluded.issue_title, issue_body_part=excluded.issue_body_part,"
            "issue_cause=excluded.issue_cause, issue_correction=excluded.issue_correction, issue_priority=excluded.issue_priority,"
            "active=1, updated_at=excluded.updated_at"));
        if (!bindAndExec(query,
                         {standard.id,
                          standard.code,
                          standard.name,
                          standard.categoryId,
                          standard.level,
                          standard.purpose,
                          standard.targetReps,
                          standard.targetScore,
                          standard.setCount,
                          standard.restSeconds,
                          standard.arm,
                          standard.release,
                          standard.debounce,
                          standard.detectionWeight,
                          standard.symmetryWeight,
                          standard.balanceWeight,
                          standard.stabilityWeight,
                          standard.depthWeight,
                          standard.detectionMin,
                          standard.symmetryMin,
                          standard.balanceMin,
                          standard.stabilityMin,
                          standard.depthMin,
                          standard.phases,
                          standard.keyPoints,
                          standard.issueTitle,
                          standard.issueBodyPart,
                          standard.issueCause,
                          standard.issueCorrection,
                          standard.priority,
                          nowIso(),
                          nowIso()})) {
            return rollback(query.lastError().text());
        }
    }

    if (!m_db.commit()) {
        return rollback(m_db.lastError().text());
    }

    return setMetaValue(QStringLiteral("seedVersion"), QStringLiteral("1"), errorMessage);
}

QVector<AthleteProfile> TrainingRepository::athletes() const
{
    QVector<AthleteProfile> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id, name, code, age_group, height_cm, weight_kg, discipline, level,"
                              "preferred_rotation, preferred_takeoff_foot, injury_notes, goals "
                              "FROM athletes ORDER BY name COLLATE NOCASE"));
    while (query.next()) {
        AthleteProfile athlete;
        athlete.id = query.value(0).toString();
        athlete.name = query.value(1).toString();
        athlete.code = query.value(2).toString();
        athlete.ageGroup = query.value(3).toString();
        athlete.heightCm = query.value(4).toDouble();
        athlete.weightKg = query.value(5).toDouble();
        athlete.discipline = query.value(6).toString();
        athlete.level = query.value(7).toString();
        athlete.preferredRotation = query.value(8).toString();
        athlete.preferredTakeoffFoot = query.value(9).toString();
        athlete.injuryNotes = query.value(10).toString();
        athlete.goals = query.value(11).toString();
        result.append(athlete);
    }
    return result;
}

QVector<CoachProfile> TrainingRepository::coaches() const
{
    QVector<CoachProfile> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id, name, code FROM coaches ORDER BY name COLLATE NOCASE"));
    while (query.next()) {
        CoachProfile coach;
        coach.id = query.value(0).toString();
        coach.name = query.value(1).toString();
        coach.code = query.value(2).toString();
        result.append(coach);
    }
    return result;
}

QVector<ActionStandard> TrainingRepository::actionStandards() const
{
    QVector<ActionStandard> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral(
        "SELECT s.id, s.code, s.name, s.category_id, c.name, s.level, s.purpose, s.version,"
        "s.target_reps, s.target_score, s.set_count, s.rest_seconds, s.arm_threshold, s.release_threshold,"
        "s.debounce_ms, s.detection_weight, s.symmetry_weight, s.balance_weight, s.stability_weight,"
        "s.depth_weight, s.detection_min, s.symmetry_min, s.balance_min, s.stability_min, s.depth_min,"
        "s.phases, s.key_points, s.issue_title, s.issue_body_part, s.issue_cause, s.issue_correction,"
        "s.issue_priority "
        "FROM action_standards s JOIN action_categories c ON c.id = s.category_id "
        "WHERE s.active = 1 ORDER BY c.sort_order, s.name COLLATE NOCASE"));
    while (query.next()) {
        ActionStandard standard;
        int col = 0;
        standard.id = query.value(col++).toString();
        standard.code = query.value(col++).toString();
        standard.name = query.value(col++).toString();
        standard.categoryId = query.value(col++).toString();
        standard.categoryName = query.value(col++).toString();
        standard.level = query.value(col++).toString();
        standard.purpose = query.value(col++).toString();
        standard.version = query.value(col++).toInt();
        standard.targetReps = query.value(col++).toInt();
        standard.targetScore = query.value(col++).toInt();
        standard.setCount = query.value(col++).toInt();
        standard.restSeconds = query.value(col++).toInt();
        standard.armThreshold = query.value(col++).toDouble();
        standard.releaseThreshold = query.value(col++).toDouble();
        standard.debounceMs = query.value(col++).toInt();
        standard.detectionWeight = query.value(col++).toDouble();
        standard.symmetryWeight = query.value(col++).toDouble();
        standard.balanceWeight = query.value(col++).toDouble();
        standard.stabilityWeight = query.value(col++).toDouble();
        standard.depthWeight = query.value(col++).toDouble();
        standard.detectionMin = query.value(col++).toInt();
        standard.symmetryMin = query.value(col++).toInt();
        standard.balanceMin = query.value(col++).toInt();
        standard.stabilityMin = query.value(col++).toInt();
        standard.depthMin = query.value(col++).toInt();
        standard.phases = query.value(col++).toString();
        standard.keyPoints = query.value(col++).toString();
        standard.issueTitle = query.value(col++).toString();
        standard.issueBodyPart = query.value(col++).toString();
        standard.issueCause = query.value(col++).toString();
        standard.issueCorrection = query.value(col++).toString();
        standard.issuePriority = query.value(col++).toInt();
        result.append(standard);
    }
    return result;
}

QVector<SessionHistoryItem> TrainingRepository::recentSessions(int limit) const
{
    QVector<SessionHistoryItem> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT ts.id, ts.athlete_id, COALESCE(ts.coach_id, ''), COALESCE(ts.plan_id, ''), COALESCE(ts.task_id, ''),"
        "ts.action_standard_id, a.name, COALESCE(c.name, ''), s.name, ac.name, ts.standard_version, ts.saved_at,"
        "ts.duration_sec, ts.total_reps, ts.valid_reps, ts.target_reps, ts.target_score, ts.average_score,"
        "ts.best_score, ts.camera, ts.model_precision, ts.fps, ts.detection_score, ts.symmetry_score,"
        "ts.balance_score, ts.stability_score, ts.depth_score, ts.site, ts.training_phase, ts.goal,"
        "COALESCE(ts.video_source, ''), COALESCE(ts.video_fallback_source, ''), COALESCE(ts.video_camera_name, ''),"
        "ts.feedback, COALESCE(ts.notes, ''), COALESCE(ts.coach_comment, '') "
        "FROM training_sessions ts "
        "JOIN athletes a ON a.id = ts.athlete_id "
        "LEFT JOIN coaches c ON c.id = ts.coach_id "
        "JOIN action_standards s ON s.id = ts.action_standard_id "
        "JOIN action_categories ac ON ac.id = s.category_id "
        "ORDER BY ts.saved_at DESC LIMIT ?"));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        SessionHistoryItem item;
        int col = 0;
        item.id = query.value(col++).toString();
        item.athleteId = query.value(col++).toString();
        item.coachId = query.value(col++).toString();
        item.planId = query.value(col++).toString();
        item.taskId = query.value(col++).toString();
        item.actionStandardId = query.value(col++).toString();
        item.athleteName = query.value(col++).toString();
        item.coachName = query.value(col++).toString();
        item.actionName = query.value(col++).toString();
        item.actionCategory = query.value(col++).toString();
        item.standardVersion = query.value(col++).toInt();
        const QDateTime savedAt = QDateTime::fromString(query.value(col++).toString(), Qt::ISODate);
        item.time = savedAt.isValid() ? savedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
                                      : query.value(col - 1).toString();
        item.duration = query.value(col++).toInt();
        item.totalReps = query.value(col++).toInt();
        item.validReps = query.value(col++).toInt();
        item.targetReps = query.value(col++).toInt();
        item.targetScore = query.value(col++).toInt();
        item.score = query.value(col++).toInt();
        item.bestScore = query.value(col++).toInt();
        item.camera = query.value(col++).toInt();
        item.modelPrecision = query.value(col++).toString();
        item.fps = query.value(col++).toInt();
        item.detectionScore = query.value(col++).toInt();
        item.symmetryScore = query.value(col++).toInt();
        item.balanceScore = query.value(col++).toInt();
        item.stabilityScore = query.value(col++).toInt();
        item.depthScore = query.value(col++).toInt();
        item.site = query.value(col++).toString();
        item.trainingPhase = query.value(col++).toString();
        item.goal = query.value(col++).toString();
        item.videoSource = query.value(col++).toString();
        item.videoFallbackSource = query.value(col++).toString();
        item.videoCameraName = query.value(col++).toString();
        item.feedback = query.value(col++).toString();
        item.notes = query.value(col++).toString();
        item.coachComment = query.value(col++).toString();
        result.append(item);
    }
    return result;
}

QVector<ActionRepetition> TrainingRepository::repetitionsForSession(const QString &sessionId) const
{
    QVector<ActionRepetition> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, session_id, action_standard_id, standard_version, started_ms, ended_ms,"
                                 "valid, score, detection_score, symmetry_score, balance_score, stability_score,"
                                 "depth_score, error_codes, feedback, key_frame_ms, video_clip_start_ms, video_clip_end_ms "
                                 "FROM action_repetitions WHERE session_id = ? ORDER BY started_ms"));
    query.addBindValue(sessionId);
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        ActionRepetition repetition;
        int col = 0;
        repetition.id = query.value(col++).toString();
        repetition.sessionId = query.value(col++).toString();
        repetition.actionStandardId = query.value(col++).toString();
        repetition.standardVersion = query.value(col++).toInt();
        repetition.startedMs = query.value(col++).toInt();
        repetition.endedMs = query.value(col++).toInt();
        repetition.valid = query.value(col++).toInt() != 0;
        repetition.score = query.value(col++).toInt();
        repetition.detectionScore = query.value(col++).toInt();
        repetition.symmetryScore = query.value(col++).toInt();
        repetition.balanceScore = query.value(col++).toInt();
        repetition.stabilityScore = query.value(col++).toInt();
        repetition.depthScore = query.value(col++).toInt();
        repetition.errorCodes = query.value(col++).toString();
        repetition.feedback = query.value(col++).toString();
        repetition.keyFrameMs = query.value(col++).toInt();
        repetition.videoClipStartMs = query.value(col++).toInt();
        repetition.videoClipEndMs = query.value(col++).toInt();
        result.append(repetition);
    }
    return result;
}

TrainingTrendWindow TrainingRepository::trendForRecentDays(int days) const
{
    TrainingTrendWindow trend;
    trend.days = std::max(1, days);
    if (!m_db.isOpen()) {
        return trend;
    }

    const QString since = QDateTime::currentDateTime()
                              .addDays(-trend.days)
                              .toString(Qt::ISODate);

    QSqlQuery sessionQuery(m_db);
    sessionQuery.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(ROUND(AVG(average_score)), 0), COALESCE(MAX(best_score), 0), "
        "COALESCE(SUM(total_reps), 0) "
        "FROM training_sessions "
        "WHERE saved_at >= ?"));
    sessionQuery.addBindValue(since);
    int sessionTotalReps = 0;
    if (sessionQuery.exec() && sessionQuery.next()) {
        trend.sessionCount = sessionQuery.value(0).toInt();
        trend.averageScore = sessionQuery.value(1).toInt();
        trend.bestScore = sessionQuery.value(2).toInt();
        sessionTotalReps = sessionQuery.value(3).toInt();
    }

    QSqlQuery repetitionQuery(m_db);
    repetitionQuery.prepare(QStringLiteral(
        "SELECT COUNT(ar.id), "
        "COALESCE(ROUND(AVG(ar.detection_score)), 0), "
        "COALESCE(ROUND(AVG(ar.symmetry_score)), 0), "
        "COALESCE(ROUND(AVG(ar.balance_score)), 0), "
        "COALESCE(ROUND(AVG(ar.stability_score)), 0), "
        "COALESCE(ROUND(AVG(ar.depth_score)), 0) "
        "FROM action_repetitions ar "
        "JOIN training_sessions ts ON ts.id = ar.session_id "
        "WHERE ts.saved_at >= ?"));
    repetitionQuery.addBindValue(since);
    if (repetitionQuery.exec() && repetitionQuery.next()) {
        trend.completedReps = repetitionQuery.value(0).toInt();
        trend.detectionScore = repetitionQuery.value(1).toInt();
        trend.symmetryScore = repetitionQuery.value(2).toInt();
        trend.balanceScore = repetitionQuery.value(3).toInt();
        trend.stabilityScore = repetitionQuery.value(4).toInt();
        trend.depthScore = repetitionQuery.value(5).toInt();
    }

    if (trend.completedReps <= 0) {
        trend.completedReps = sessionTotalReps;
    }

    if (trend.completedReps <= 0 || (trend.detectionScore == 0
                                     && trend.symmetryScore == 0
                                     && trend.balanceScore == 0
                                     && trend.stabilityScore == 0
                                     && trend.depthScore == 0)) {
        QSqlQuery fallbackMetricQuery(m_db);
        fallbackMetricQuery.prepare(QStringLiteral(
            "SELECT "
            "COALESCE(ROUND(AVG(detection_score)), 0), "
            "COALESCE(ROUND(AVG(symmetry_score)), 0), "
            "COALESCE(ROUND(AVG(balance_score)), 0), "
            "COALESCE(ROUND(AVG(stability_score)), 0), "
            "COALESCE(ROUND(AVG(depth_score)), 0) "
            "FROM training_sessions "
            "WHERE saved_at >= ?"));
        fallbackMetricQuery.addBindValue(since);
        if (fallbackMetricQuery.exec() && fallbackMetricQuery.next()) {
            trend.detectionScore = fallbackMetricQuery.value(0).toInt();
            trend.symmetryScore = fallbackMetricQuery.value(1).toInt();
            trend.balanceScore = fallbackMetricQuery.value(2).toInt();
            trend.stabilityScore = fallbackMetricQuery.value(3).toInt();
            trend.depthScore = fallbackMetricQuery.value(4).toInt();
        }
    }

    updateWeakestMetric(&trend);
    return trend;
}

TrainingBaseline TrainingRepository::baselineFor(const QString &athleteId, const QString &actionStandardId) const
{
    TrainingBaseline baseline;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT session_count, average_score, average_valid_reps "
                                 "FROM athlete_action_baselines "
                                 "WHERE athlete_id = ? AND action_standard_id = ?"));
    query.addBindValue(athleteId);
    query.addBindValue(actionStandardId);
    if (query.exec() && query.next()) {
        baseline.sessionCount = query.value(0).toInt();
        baseline.averageScore = query.value(1).toInt();
        baseline.averageValidReps = query.value(2).toInt();
    }
    return baseline;
}

bool TrainingRepository::saveCoachComment(const QString &sessionId,
                                          const QString &comment,
                                          QString *errorMessage)
{
    if (sessionId.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练记录不存在。");
        }
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE training_sessions SET coach_comment = ? WHERE id = ?"));
    if (!bindAndExec(query, {comment.trimmed(), sessionId})) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未找到对应训练记录。");
        }
        return false;
    }
    return true;
}

bool TrainingRepository::createAthlete(const QString &name, QString *athleteId, QString *errorMessage)
{
    const QString id = newId();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO athletes "
                                 "(id, name, code, age_group, discipline, level, goals, created_at, updated_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    if (!bindAndExec(query,
                     {id,
                      safeText(name, QStringLiteral("新运动员")),
                      QStringLiteral("ATH-%1").arg(QDateTime::currentMSecsSinceEpoch()),
                      QStringLiteral("未分组"),
                      QStringLiteral("滑冰"),
                      QStringLiteral("基础"),
                      QStringLiteral("动作标准训练"),
                      nowIso(),
                      nowIso()})) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    const QString coachId = scalarString(QStringLiteral("SELECT id FROM coaches ORDER BY created_at LIMIT 1"));
    if (!coachId.isEmpty()) {
        QSqlQuery relation(m_db);
        relation.prepare(QStringLiteral("INSERT OR IGNORE INTO coach_athletes "
                                        "(coach_id, athlete_id, created_at) VALUES (?, ?, ?)"));
        bindAndExec(relation, {coachId, id, nowIso()});
    }

    if (athleteId) {
        *athleteId = id;
    }
    return true;
}

bool TrainingRepository::createCoach(const QString &name, QString *coachId, QString *errorMessage)
{
    const QString id = newId();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO coaches (id, name, code, created_at, updated_at) "
                                 "VALUES (?, ?, ?, ?, ?)"));
    if (!bindAndExec(query,
                     {id,
                      safeText(name, QStringLiteral("新教练")),
                      QStringLiteral("COACH-%1").arg(QDateTime::currentMSecsSinceEpoch()),
                      nowIso(),
                      nowIso()})) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (coachId) {
        *coachId = id;
    }
    return true;
}

bool TrainingRepository::ensureDailyTask(const QString &athleteId,
                                         const QString &coachId,
                                         const QString &actionStandardId,
                                         int standardVersion,
                                         int targetReps,
                                         int targetScore,
                                         int setCount,
                                         int restSeconds,
                                         const QString &site,
                                         const QString &trainingPhase,
                                         const QString &goal,
                                         QString *planId,
                                         QString *taskId,
                                         QString *errorMessage)
{
    const QString today = dateOnly(QDate::currentDate());
    const QString planKeySql = QStringLiteral("SELECT id FROM training_plans "
                                             "WHERE athlete_id = ? AND training_date = ? AND status = 'active' "
                                             "ORDER BY created_at DESC LIMIT 1");
    QString existingPlanId = scalarString(planKeySql, {athleteId, today});
    if (existingPlanId.isEmpty()) {
        existingPlanId = newId();
        QSqlQuery insertPlan(m_db);
        insertPlan.prepare(QStringLiteral("INSERT INTO training_plans "
                                          "(id, athlete_id, coach_id, name, training_date, site, training_phase, goal, status, created_at, updated_at) "
                                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'active', ?, ?)"));
        if (!bindAndExec(insertPlan,
                         {existingPlanId,
                          athleteId,
                          coachId,
                          QStringLiteral("今日训练计划"),
                          today,
                          site,
                          trainingPhase,
                          goal,
                          nowIso(),
                          nowIso()})) {
            if (errorMessage) {
                *errorMessage = insertPlan.lastError().text();
            }
            return false;
        }
    } else {
        QSqlQuery updatePlan(m_db);
        updatePlan.prepare(QStringLiteral("UPDATE training_plans SET coach_id = ?, site = ?, training_phase = ?, goal = ?, updated_at = ? "
                                          "WHERE id = ?"));
        bindAndExec(updatePlan, {coachId, site, trainingPhase, goal, nowIso(), existingPlanId});
    }

    QString existingTaskId = scalarString(QStringLiteral("SELECT id FROM training_tasks "
                                                        "WHERE plan_id = ? AND action_standard_id = ? "
                                                        "ORDER BY created_at DESC LIMIT 1"),
                                          {existingPlanId, actionStandardId});
    if (existingTaskId.isEmpty()) {
        existingTaskId = newId();
        QSqlQuery insertTask(m_db);
        insertTask.prepare(QStringLiteral("INSERT INTO training_tasks "
                                          "(id, plan_id, action_standard_id, standard_version, target_reps, target_score,"
                                          "set_count, rest_seconds, status, sort_order, created_at, updated_at) "
                                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'active', 0, ?, ?)"));
        if (!bindAndExec(insertTask,
                         {existingTaskId,
                          existingPlanId,
                          actionStandardId,
                          standardVersion,
                          targetReps,
                          targetScore,
                          setCount,
                          restSeconds,
                          nowIso(),
                          nowIso()})) {
            if (errorMessage) {
                *errorMessage = insertTask.lastError().text();
            }
            return false;
        }
    } else {
        QSqlQuery updateTask(m_db);
        updateTask.prepare(QStringLiteral("UPDATE training_tasks SET standard_version = ?, target_reps = ?, target_score = ?,"
                                          "set_count = ?, rest_seconds = ?, status = 'active', updated_at = ? WHERE id = ?"));
        if (!bindAndExec(updateTask,
                         {standardVersion,
                          targetReps,
                          targetScore,
                          setCount,
                          restSeconds,
                          nowIso(),
                          existingTaskId})) {
            if (errorMessage) {
                *errorMessage = updateTask.lastError().text();
            }
            return false;
        }
    }

    if (planId) {
        *planId = existingPlanId;
    }
    if (taskId) {
        *taskId = existingTaskId;
    }
    return true;
}

bool TrainingRepository::saveTrainingSession(TrainingSession *session,
                                             const QVector<ActionRepetition> &repetitions,
                                             QString *errorMessage)
{
    if (!session) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练记录为空。");
        }
        return false;
    }

    session->id = ensureId(session->id);
    if (!m_db.transaction()) {
        if (errorMessage) {
            *errorMessage = m_db.lastError().text();
        }
        return false;
    }

    auto rollback = [this, errorMessage](const QString &message) {
        m_db.rollback();
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO training_sessions ("
        "id, athlete_id, coach_id, plan_id, task_id, action_standard_id, standard_version, legacy_qsettings_id,"
        "started_at, saved_at, duration_sec, total_reps, valid_reps, average_score, best_score, camera,"
        "model_precision, fps, detection_score, symmetry_score, balance_score, stability_score, depth_score,"
        "site, training_phase, goal, target_reps, target_score, set_count, rest_seconds,"
        "video_source, video_fallback_source, video_camera_name, feedback, notes, coach_comment) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    if (!bindAndExec(query,
                     {session->id,
                      session->athleteId,
                      session->coachId,
                      session->planId,
                      session->taskId,
                      session->actionStandardId,
                      session->standardVersion,
                      session->legacyQsettingsId,
                      session->startedAt.toString(Qt::ISODate),
                      session->savedAt.toString(Qt::ISODate),
                      session->durationSec,
                      session->totalReps,
                      session->validReps,
                      session->averageScore,
                      session->bestScore,
                      session->camera,
                      session->modelPrecision,
                      session->fps,
                      session->detectionScore,
                      session->symmetryScore,
                      session->balanceScore,
                      session->stabilityScore,
                      session->depthScore,
                      session->site,
                      session->trainingPhase,
                      session->goal,
                      session->targetReps,
                      session->targetScore,
                      session->setCount,
                      session->restSeconds,
                      session->videoSource,
                      session->videoFallbackSource,
                      session->videoCameraName,
                      session->feedback,
                      session->notes,
                      session->coachComment})) {
        return rollback(query.lastError().text());
    }

    for (ActionRepetition repetition : repetitions) {
        repetition.id = ensureId(repetition.id);
        QSqlQuery insertRep(m_db);
        insertRep.prepare(QStringLiteral(
            "INSERT INTO action_repetitions ("
            "id, session_id, action_standard_id, standard_version, started_ms, ended_ms, valid, score,"
            "detection_score, symmetry_score, balance_score, stability_score, depth_score, error_codes, feedback, key_frame_ms,"
            "video_clip_start_ms, video_clip_end_ms) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        if (!bindAndExec(insertRep,
                         {repetition.id,
                          session->id,
                          repetition.actionStandardId,
                          repetition.standardVersion,
                          repetition.startedMs,
                          repetition.endedMs,
                          repetition.valid ? 1 : 0,
                          repetition.score,
                          repetition.detectionScore,
                          repetition.symmetryScore,
                          repetition.balanceScore,
                          repetition.stabilityScore,
                          repetition.depthScore,
                          repetition.errorCodes,
                          repetition.feedback,
                          repetition.keyFrameMs,
                          repetition.videoClipStartMs,
                          repetition.videoClipEndMs})) {
            return rollback(insertRep.lastError().text());
        }
    }

    if (!session->taskId.isEmpty()) {
        QSqlQuery updateTask(m_db);
        updateTask.prepare(QStringLiteral("UPDATE training_tasks SET status = ?, updated_at = ? WHERE id = ?"));
        const QString status = session->targetReps > 0 && session->validReps >= session->targetReps
                                   ? QStringLiteral("done")
                                   : QStringLiteral("active");
        if (!bindAndExec(updateTask, {status, nowIso(), session->taskId})) {
            return rollback(updateTask.lastError().text());
        }
    }

    if (!m_db.commit()) {
        return rollback(m_db.lastError().text());
    }

    refreshBaseline(session->athleteId, session->actionStandardId);
    return true;
}

bool TrainingRepository::migrateLegacyTrainingHistory(QString *errorMessage)
{
    if (hasMetaValue(QStringLiteral("legacyTrainingHistoryMigrated"))) {
        return true;
    }

    const QString defaultAthleteId = scalarString(QStringLiteral("SELECT id FROM athletes ORDER BY created_at LIMIT 1"));
    const QString defaultCoachId = scalarString(QStringLiteral("SELECT id FROM coaches ORDER BY created_at LIMIT 1"));
    const QString defaultStandardId = scalarString(QStringLiteral("SELECT id FROM action_standards ORDER BY rowid LIMIT 1"));
    const int defaultStandardVersion = scalarInt(QStringLiteral("SELECT version FROM action_standards WHERE id = ?"),
                                                 {defaultStandardId},
                                                 1);
    if (defaultAthleteId.isEmpty() || defaultStandardId.isEmpty()) {
        return setMetaValue(QStringLiteral("legacyTrainingHistoryMigrated"), QStringLiteral("1"), errorMessage);
    }

    QSettings settings;
    const int recordCount = settings.beginReadArray(QStringLiteral("trainingHistory"));
    QVector<TrainingSession> legacySessions;
    legacySessions.reserve(recordCount);
    for (int i = 0; i < recordCount; ++i) {
        settings.setArrayIndex(i);
        TrainingSession session;
        session.id = newId();
        session.athleteId = defaultAthleteId;
        session.coachId = defaultCoachId;
        session.actionStandardId = defaultStandardId;
        session.standardVersion = defaultStandardVersion;
        session.legacyQsettingsId = settings.value(QStringLiteral("id")).toLongLong();
        const QString timeText = settings.value(QStringLiteral("time")).toString();
        QDateTime savedAt = QDateTime::fromString(timeText, QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        if (!savedAt.isValid() && session.legacyQsettingsId > 0) {
            savedAt = QDateTime::fromMSecsSinceEpoch(session.legacyQsettingsId);
        }
        if (!savedAt.isValid()) {
            savedAt = QDateTime::currentDateTime();
        }
        session.savedAt = savedAt;
        session.startedAt = savedAt.addSecs(-settings.value(QStringLiteral("duration")).toInt());
        session.durationSec = settings.value(QStringLiteral("duration")).toInt();
        session.totalReps = settings.value(QStringLiteral("actions")).toInt();
        session.validReps = session.totalReps;
        session.averageScore = settings.value(QStringLiteral("score")).toInt();
        session.bestScore = session.averageScore;
        session.camera = settings.value(QStringLiteral("camera"), 1).toInt();
        session.modelPrecision = settings.value(QStringLiteral("modelPrecision"), QStringLiteral("balanced")).toString();
        session.fps = settings.value(QStringLiteral("fps"), 30).toInt();
        session.detectionScore = settings.value(QStringLiteral("detectionScore")).toInt();
        session.symmetryScore = settings.value(QStringLiteral("symmetryScore")).toInt();
        session.balanceScore = settings.value(QStringLiteral("balanceScore")).toInt();
        session.stabilityScore = settings.value(QStringLiteral("stabilityScore")).toInt();
        session.depthScore = settings.value(QStringLiteral("depthScore")).toInt();
        session.site = QStringLiteral("旧记录");
        session.trainingPhase = QStringLiteral("历史迁移");
        session.goal = QStringLiteral("兼容旧 trainingHistory");
        session.targetReps = 0;
        session.targetScore = 0;
        session.feedback = settings.value(QStringLiteral("feedback"), QStringLiteral("等待姿态")).toString();
        legacySessions.append(session);
    }
    settings.endArray();

    for (TrainingSession &session : legacySessions) {
        const bool exists = scalarInt(QStringLiteral("SELECT COUNT(*) FROM training_sessions WHERE legacy_qsettings_id = ?"),
                                      {session.legacyQsettingsId},
                                      0) > 0;
        if (!exists && !saveTrainingSession(&session, {}, errorMessage)) {
            return false;
        }
    }

    return setMetaValue(QStringLiteral("legacyTrainingHistoryMigrated"), QStringLiteral("1"), errorMessage);
}

bool TrainingRepository::hasMetaValue(const QString &key) const
{
    return scalarInt(QStringLiteral("SELECT COUNT(*) FROM schema_meta WHERE key = ?"), {key}, 0) > 0;
}

bool TrainingRepository::setMetaValue(const QString &key, const QString &value, QString *errorMessage) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO schema_meta (key, value) VALUES (?, ?) "
                                 "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    if (!bindAndExec(query, {key, value})) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool TrainingRepository::ensureColumn(const QString &tableName,
                                      const QString &columnName,
                                      const QString &definition,
                                      QString *errorMessage) const
{
    QSqlQuery pragma(m_db);
    if (!pragma.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        if (errorMessage) {
            *errorMessage = pragma.lastError().text();
        }
        return false;
    }

    while (pragma.next()) {
        if (pragma.value(1).toString().compare(columnName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return execute(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                       .arg(tableName, columnName, definition),
                   errorMessage);
}

QString TrainingRepository::ensureId(const QString &id) const
{
    const QString trimmed = id.trimmed();
    return trimmed.isEmpty() ? newId() : trimmed;
}

QString TrainingRepository::scalarString(const QString &sql, const QVariantList &args) const
{
    QSqlQuery query(m_db);
    query.prepare(sql);
    if (!bindAndExec(query, args) || !query.next()) {
        return QString();
    }
    return query.value(0).toString();
}

int TrainingRepository::scalarInt(const QString &sql, const QVariantList &args, int defaultValue) const
{
    QSqlQuery query(m_db);
    query.prepare(sql);
    if (!bindAndExec(query, args) || !query.next()) {
        return defaultValue;
    }
    return query.value(0).toInt();
}

void TrainingRepository::refreshBaseline(const QString &athleteId, const QString &actionStandardId)
{
    QSqlQuery aggregate(m_db);
    aggregate.prepare(QStringLiteral("SELECT COUNT(*), COALESCE(ROUND(AVG(average_score)), 0), COALESCE(ROUND(AVG(valid_reps)), 0) "
                                     "FROM training_sessions WHERE athlete_id = ? AND action_standard_id = ?"));
    aggregate.addBindValue(athleteId);
    aggregate.addBindValue(actionStandardId);
    if (!aggregate.exec() || !aggregate.next()) {
        return;
    }

    QSqlQuery upsert(m_db);
    upsert.prepare(QStringLiteral("INSERT INTO athlete_action_baselines "
                                  "(athlete_id, action_standard_id, session_count, average_score, average_valid_reps, updated_at) "
                                  "VALUES (?, ?, ?, ?, ?, ?) "
                                  "ON CONFLICT(athlete_id, action_standard_id) DO UPDATE SET "
                                  "session_count=excluded.session_count, average_score=excluded.average_score,"
                                  "average_valid_reps=excluded.average_valid_reps, updated_at=excluded.updated_at"));
    bindAndExec(upsert,
                {athleteId,
                 actionStandardId,
                 aggregate.value(0).toInt(),
                 aggregate.value(1).toInt(),
                 aggregate.value(2).toInt(),
                 nowIso()});
}
