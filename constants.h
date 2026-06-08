#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <QObject>
#include <QString>
#include <QStringList>

#include "qtypes.h"
namespace control2 {

constexpr inline quint8 const  AWAIT_BIT{8U}; // Flag in manual/program job
constexpr inline quint8 const  RAMP_BIT{9U};  // Flag in manual/program job
inline QString const           PROGS_DIR{"data/prog/"};
inline QString const           INTERVALS_DIR{"data/"};
inline QString const           EXPORT_DIR{"export/"};
inline QString const           SETTINGS_FILE("settings.json");
inline QString const           ACTIONS_FILE("data/event/actions.dat");
inline QString const           USER_FILE{"users.dat"};
constexpr inline quint32 const INTERVAL_DURATION{24 * 60 * 60};
constexpr inline quint32 const UI_BTN_HEIGHT{40};
constexpr inline quint32 const UI_MENU_BTN_HEIGHT{60};

inline static QStringList const APP_STATE_LIST{
  "IDLE", "CONNECTING", "CONNECTED", "ONLINE", "OFFLINE", "DUMMY", "WAIT_IN_DEBUG", "UNKNOWN=ERROR"};

static QStringList hardwareStatusList() {
  return {QObject::tr("Нет связи"),
          QObject::tr("Ожидание"),
          QObject::tr("Работа"),
          QObject::tr("Завершено"),
          QObject::tr("Ошибка")};
}

static QList<QPair<QString, QString>> statusBarStatesList() {
  return {
    {"temp", QObject::tr("Температура")},
    { "hum",     QObject::tr("Влажность")},
    {"pres",       QObject::tr("Давление")}
  };
} // <state name, display text>

} // namespace control2
#endif // CONSTANTS_H
