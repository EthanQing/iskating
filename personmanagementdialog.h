#ifndef PERSONMANAGEMENTDIALOG_H
#define PERSONMANAGEMENTDIALOG_H

#include "trainingdomain.h"
#include "tensortrtathletebackend.h"

#include <QDialog>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;
class QDoubleSpinBox;
class QPushButton;
class TrainingRepository;

class PersonManagementDialog : public QDialog
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

    QLabel *m_statusLabel = nullptr;

    QTableWidget *m_athleteTable = nullptr;
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
    QTableWidget *m_identitySampleTable = nullptr;
    QPushButton *m_addIdentitySampleButton = nullptr;
    QPushButton *m_deleteIdentitySampleButton = nullptr;
    std::unique_ptr<TensorRtAthleteBackend> m_identityBackend;
    bool m_identityBackendInitialized = false;

    QTableWidget *m_coachTable = nullptr;
    QLineEdit *m_coachNameEdit = nullptr;
    QLineEdit *m_coachCodeEdit = nullptr;
    QLineEdit *m_specialtyEdit = nullptr;
    QLineEdit *m_phoneEdit = nullptr;
    QPlainTextEdit *m_coachNotesEdit = nullptr;
    QListWidget *m_coachAthleteList = nullptr;
};

#endif // PERSONMANAGEMENTDIALOG_H
