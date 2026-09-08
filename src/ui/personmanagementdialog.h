#ifndef PERSONMANAGEMENTDIALOG_H
#define PERSONMANAGEMENTDIALOG_H

#include "framelessdialog.h"
#include "trainingdomain.h"
#include "tensortrtathletebackend.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTableWidget;
class QDoubleSpinBox;
class QWidget;
class TrainingRepository;

class PersonManagementDialog : public FramelessDialog
{
public:
    explicit PersonManagementDialog(TrainingRepository *repository, QWidget *parent = nullptr);
    bool changed() const;

private:
    void buildUi();
    QWidget *buildAthletePage();
    QWidget *buildCoachPage();
    void reload();
    void populateAthleteTable();
    void populateCoachTable();
    void populateCoachAthleteList(const QVector<QString> &checkedAthleteIds = {});
    void applyAthleteFilter();
    void applyCoachFilter();
    void updateIdentityActions();
    void updateCoachAthleteCount();
    void updateRepositoryAvailability();
    void selectAthleteRow(int row);
    void selectCoachRow(int row);
    void newAthlete();
    void saveAthlete();
    void archiveAthlete();
    void addIdentitySample();
    void deleteIdentitySample();
    void populateIdentitySamples();
    void newCoach();
    void saveCoach();
    void archiveCoach();
    void setAthleteForm(const AthleteProfile &athlete);
    void setCoachForm(const CoachProfile &coach);
    AthleteProfile athleteFromForm() const;
    CoachProfile coachFromForm() const;
    QVector<QString> selectedCoachAthleteIds() const;
    int athleteRowForId(const QString &athleteId) const;
    int coachRowForId(const QString &coachId) const;
    void setStatus(const QString &text);

    TrainingRepository *m_repository = nullptr;
    bool m_changed = false;
    QVector<AthleteProfile> m_athletes;
    QVector<CoachProfile> m_coaches;
    QString m_currentAthleteId;
    QString m_currentCoachId;

    QStackedWidget *m_pages = nullptr;
    QLabel *m_serviceWarningLabel = nullptr;
    QLabel *m_statusLabel = nullptr;

    QLineEdit *m_athleteSearchEdit = nullptr;
    QLabel *m_athleteCountLabel = nullptr;
    QStackedWidget *m_athleteListStack = nullptr;
    QTableWidget *m_athleteTable = nullptr;
    QWidget *m_athleteDetailContent = nullptr;
    QScrollArea *m_athleteDetailScroll = nullptr;
    QLabel *m_athleteDetailTitle = nullptr;
    QLineEdit *m_athleteNameEdit = nullptr;
    QLineEdit *m_athleteCodeEdit = nullptr;
    QLineEdit *m_ageGroupEdit = nullptr;
    QDoubleSpinBox *m_heightSpinBox = nullptr;
    QDoubleSpinBox *m_weightSpinBox = nullptr;
    QLineEdit *m_disciplineEdit = nullptr;
    QLineEdit *m_levelEdit = nullptr;
    QLineEdit *m_rotationEdit = nullptr;
    QLineEdit *m_takeoffFootEdit = nullptr;
    QPlainTextEdit *m_injuryNotesEdit = nullptr;
    QPlainTextEdit *m_goalsEdit = nullptr;
    QLabel *m_identitySampleCountLabel = nullptr;
    QTableWidget *m_identitySampleTable = nullptr;
    QPushButton *m_newAthleteButton = nullptr;
    QPushButton *m_emptyNewAthleteButton = nullptr;
    QPushButton *m_saveAthleteButton = nullptr;
    QPushButton *m_archiveAthleteButton = nullptr;
    QPushButton *m_addIdentitySampleButton = nullptr;
    QPushButton *m_deleteIdentitySampleButton = nullptr;
    std::unique_ptr<TensorRtAthleteBackend> m_identityBackend;
    bool m_identityBackendInitialized = false;

    QLineEdit *m_coachSearchEdit = nullptr;
    QLabel *m_coachCountLabel = nullptr;
    QStackedWidget *m_coachListStack = nullptr;
    QTableWidget *m_coachTable = nullptr;
    QWidget *m_coachDetailContent = nullptr;
    QScrollArea *m_coachDetailScroll = nullptr;
    QLabel *m_coachDetailTitle = nullptr;
    QLineEdit *m_coachNameEdit = nullptr;
    QLineEdit *m_coachCodeEdit = nullptr;
    QLineEdit *m_specialtyEdit = nullptr;
    QLineEdit *m_phoneEdit = nullptr;
    QPlainTextEdit *m_coachNotesEdit = nullptr;
    QLabel *m_coachAthleteCountLabel = nullptr;
    QListWidget *m_coachAthleteList = nullptr;
    QPushButton *m_newCoachButton = nullptr;
    QPushButton *m_emptyNewCoachButton = nullptr;
    QPushButton *m_saveCoachButton = nullptr;
    QPushButton *m_archiveCoachButton = nullptr;
};

#endif // PERSONMANAGEMENTDIALOG_H
