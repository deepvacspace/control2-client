#ifndef ADVISORSAFETY_H
#define ADVISORSAFETY_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "accessmanager.h"
#include "modelenvelope.h"

class QWidget;

namespace deepvaccontrol {
struct ResHolder;

namespace ai {

// Writing PID gains to the chamber (and starting / stopping a job on the
// user's behalf) is the advisor's only way to act on the plant. It needs
// more than the "start / stop" level: an observer or start-only operator
// can still look at the AI pages, simulate and see recommendations.
constexpr AccessManager::AccessLevel ADVISOR_CONTROL_LEVEL{AccessManager::AccessLevel::USER_EDIT_2};

// Empty when the current user may let the advisor act on the chamber,
// otherwise the reason shown to the user.
QString advisorControlDenied(ResHolder *resHolder);

// Snapshot of the operator's ("manual") temperature PID table taken before
// the advisor writes anything, and the means to put it back.
//
// restore() writes the whole table (every gain-scheduled band, not only the
// one the advisor last touched) to settings.json and the PLC, optionally
// followed by "stop" for the job the advisor started. If the chamber is not
// online the restore is kept pending and sent as soon as it is again; it is
// confirmed against the settings the PLC echoes back and resent (a few
// times) when they don't match.
//
// While armed, the snapshot is also journaled to disk, so that a crash or a
// forced close with advisor gains on the chamber is undone the next time
// the app reaches the chamber.
class ManualPidGuard : public QObject {
  Q_OBJECT
public:
  ManualPidGuard(ResHolder *resHolder, QString const &owner, QObject *parent = nullptr);

  // Takes the snapshot (and journals it). False when there is no valid
  // temperature PID table to come back to.
  bool capture();
  bool isArmed() const;
  bool isPending() const;

  // Manual gains of the band `temp` falls in (or the live temp_pid_idx).
  bool manualPidForBand(double temp, double &kp, double &ki, double &kd) const;

  enum class Result {
    RESTORED, // PID table sent to the chamber (confirmation follows via the echo)
    PENDING,  // chamber offline -- PID table sent when it is back
    NOTHING   // no PID table to restore (a requested stop is still sent / kept pending)
  };
  // stopJob: also stop the job the advisor started (sent after the gains);
  // kept pending like the gains when the chamber is offline.
  Result restore(bool stopJob, QString const &reason);

  // Stops the chamber job (now, or on the next connection).
  void stopJob(QString const &reason);

  // Last-chance restore on destruction / app close: sends and flushes.
  void restoreNow(bool stopJob, QString const &reason);

signals:
  void restoreConfirmed();
  void restoreFailed(QString const &message);
  // A journal from a previous session (crash / forced close) was replayed:
  // the manual PID is back. jobMayBeRunning: the advisor's job was still
  // running then -- the user decides whether to stop it (stopJob()).
  void recoveredPreviousSession(QString const &reason, bool jobMayBeRunning);

private:
  void   onAppStateChanged();
  void   onSettingsEcho(QMap<QString, float> const &plcSettings);
  void   onConfirmTimeout();
  bool   send();
  bool   echoMatches(QMap<QString, float> const &plc) const;
  void   writeJournal() const;
  void   removeJournal() const;
  void   loadJournal();
  void   finish();
  QString journalPath() const;

  ResHolder  *m_resHolder;
  QString     m_owner;
  QVariantMap m_snapshot; // plc_settings.pids[0] as it was before the advisor
  bool        m_armed{false};
  bool        m_pending{false};
  bool        m_awaitingEcho{false};
  bool        m_stopJob{false};
  bool        m_restorePid{false}; // the outstanding request includes the PID table (not only "stop")
  bool        m_sentOnce{false};   // the outstanding request reached the chamber at least once
  bool        m_fromJournal{false};
  bool        m_journalJobRunning{false}; // the recovered session still had the advisor's job running
  int         m_attempts{0};
  QString     m_reason;
  QTimer      m_confirmTimer;
};

// Tells the user the previous session ended with the AI Advisor in control
// (crash / forced close), that the manual PID has been restored, and -- if
// the advisor's job may still be running -- asks whether to stop it.
void notifyRecoveredSession(QWidget *parent, ManualPidGuard *guard, QString const &reason, bool jobMayBeRunning);

} // namespace ai
} // namespace deepvaccontrol

#endif // ADVISORSAFETY_H
