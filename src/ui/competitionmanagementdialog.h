#ifndef COMPETITIONMANAGEMENTDIALOG_H
#define COMPETITIONMANAGEMENTDIALOG_H

#include "framelessdialog.h"
#include "trainingdomain.h"

#include <QVector>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QWidget;
class TrainingRepository;

class CompetitionManagementDialog : public FramelessDialog
{
  public:
    explicit CompetitionManagementDialog(TrainingRepository *repository, QWidget *parent = nullptr);
    bool changed() const;

  private:
    void buildUi();
    void reloadCompetitions(const QString &selectedId = {});
    void reloadEvents(const QString &selectedId = {});
    void reloadEventAthletes(const QString &selectedId = {});
    void applyCompetitionFilter();
    void selectCompetition(int row);
    void selectEvent(int row);
    void selectEventAthlete(int row);
    void newCompetition();
    void newEvent();
    void newEventAthlete();
    void saveCompetition();
    void saveEvent();
    void saveEventAthlete();
    void archiveCompetition();
    void archiveEvent();
    void removeEventAthlete();
    void setCompetitionForm(const Competition &competition);
    void setEventForm(const CompetitionEvent &event);
    void setEventAthleteForm(const EventAthlete &eventAthlete);
    void populateAthleteChoices();
    void updateAvailability();
    void setStatus(const QString &text);
    int competitionRowForId(const QString &id) const;
    int eventRowForId(const QString &id) const;
    int eventAthleteRowForId(const QString &id) const;

    TrainingRepository *m_repository = nullptr;
    bool m_changed = false;
    bool m_creatingEvent = false;
    bool m_creatingEventAthlete = false;
    QVector<Competition> m_competitions;
    QVector<CompetitionEvent> m_events;
    QVector<EventAthlete> m_eventAthletes;
    QVector<AthleteProfile> m_athletes;
    QString m_currentCompetitionId;
    QString m_currentEventId;
    QString m_currentEventAthleteId;

    QLabel *m_serviceWarning = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_competitionCount = nullptr;
    QStackedWidget *m_competitionStack = nullptr;
    QTableWidget *m_competitionTable = nullptr;
    QPushButton *m_newCompetitionButton = nullptr;
    QPushButton *m_emptyCompetitionButton = nullptr;

    QLabel *m_competitionTitle = nullptr;
    QScrollArea *m_detailScroll = nullptr;
    QWidget *m_competitionEditors = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_typeEdit = nullptr;
    QCheckBox *m_dateEnabled = nullptr;
    QDateEdit *m_dateEdit = nullptr;
    QLineEdit *m_locationEdit = nullptr;
    QPlainTextEdit *m_competitionNotes = nullptr;
    QPushButton *m_saveCompetitionButton = nullptr;
    QPushButton *m_archiveCompetitionButton = nullptr;

    QLabel *m_eventCount = nullptr;
    QLabel *m_eventParentHint = nullptr;
    QWidget *m_eventBody = nullptr;
    QStackedWidget *m_eventStack = nullptr;
    QTableWidget *m_eventTable = nullptr;
    QPushButton *m_newEventButton = nullptr;
    QPushButton *m_emptyEventButton = nullptr;
    QLabel *m_eventTitle = nullptr;
    QWidget *m_eventDetail = nullptr;
    QWidget *m_eventEditors = nullptr;
    QLineEdit *m_raceEdit = nullptr;
    QLineEdit *m_eventNameEdit = nullptr;
    QLineEdit *m_groupEdit = nullptr;
    QLineEdit *m_heatEdit = nullptr;
    QCheckBox *m_scheduledEnabled = nullptr;
    QDateTimeEdit *m_scheduledEdit = nullptr;
    QPlainTextEdit *m_eventNotes = nullptr;
    QPushButton *m_saveEventButton = nullptr;
    QPushButton *m_archiveEventButton = nullptr;

    QLabel *m_eventAthleteCount = nullptr;
    QLabel *m_athleteParentHint = nullptr;
    QWidget *m_athleteBody = nullptr;
    QStackedWidget *m_eventAthleteStack = nullptr;
    QTableWidget *m_eventAthleteTable = nullptr;
    QPushButton *m_newEventAthleteButton = nullptr;
    QPushButton *m_emptyEventAthleteButton = nullptr;
    QWidget *m_eventAthleteEditors = nullptr;
    QComboBox *m_athleteCombo = nullptr;
    QLineEdit *m_bibEdit = nullptr;
    QLineEdit *m_laneEdit = nullptr;
    QSpinBox *m_sortSpin = nullptr;
    QSpinBox *m_scoreSpin = nullptr;
    QSpinBox *m_rankSpin = nullptr;
    QPlainTextEdit *m_eventAthleteNotes = nullptr;
    QPushButton *m_saveEventAthleteButton = nullptr;
    QPushButton *m_removeEventAthleteButton = nullptr;
};

#endif
