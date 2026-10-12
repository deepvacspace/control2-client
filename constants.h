#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <QObject>
#include <QString>
#include <QStringList>

#include "qtypes.h"
namespace deepvaccontrol {

constexpr inline quint8 const  AWAIT_BIT{8U}; // Flag in manual/program job
constexpr inline quint8 const  RAMP_BIT{9U};  // Flag in manual/program job
inline QString const           PROGS_DIR{"data/prog/"};
inline QString const           INTERVALS_DIR{"data/"};
inline QString const           EXPORT_DIR{"export/"};
inline QString const           SETTINGS_FILE("settings.json");
// copy of settings.json kept each time it loads correctly; restored automatically when settings.json cannot be used
inline QString const           SETTINGS_BACKUP_FILE("settings_backup.json");
inline QString const           ACTIONS_FILE("data/event/actions.dat");
inline QString const           ANALYSIS_NOTES_FILE{"data/analysis/notes.json"};
inline QString const           AI_DIR{"data/ai/"};
inline QString const           USER_FILE{"users.dat"};
constexpr inline quint32 const INTERVAL_DURATION{24 * 60 * 60};
constexpr inline quint32 const UI_BTN_HEIGHT{40};
constexpr inline quint32 const UI_MENU_BTN_HEIGHT{46};

inline static QStringList const APP_STATE_LIST{
  "IDLE", "CONNECTING", "CONNECTED", "ONLINE", "OFFLINE", "DUMMY", "WAIT_IN_DEBUG", "UNKNOWN=ERROR"};

static QStringList hardwareStatusList() {
  return {QObject::tr("No connection"),
          QObject::tr("Idle"),
          QObject::tr("Running"),
          QObject::tr("Completed"),
          QObject::tr("Error")};
}

static QList<QPair<QString, QString>> statusBarStatesList() {
  return {
    {"temp", QObject::tr("Temperature")},
    { "hum",     QObject::tr("Humidity")},
    {"pres",       QObject::tr("Pressure")}
  };
} // <state name, display text>

} // namespace deepvaccontrol
#endif // CONSTANTS_H
