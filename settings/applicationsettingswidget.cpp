#include "applicationsettingswidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "lineeditnum.h"
#include "lineedittime.h"
#include "messagedialog.h"
#include "systemvalue.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
constexpr int DEFAULT_GRAPH_PERIOD_S{10};
constexpr int DEFAULT_POLL_PERIOD_S{5};

// the "temp" entry of control_params (the setpoint of program intervals and manual control)
QVariantMap tempParam(QVariantMap const &settings) {
  for (QVariant const &entry : settings.value("control_params").toList()) {
    if (entry.toMap().value("name").toString() == QLatin1String("temp")) {
      return entry.toMap();
    }
  }
  return {};
}
} // namespace

ApplicationSettingsWidget::ApplicationSettingsWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder} {
  double const      ssf{utils::ssf()};
  QVariantMap const settings = m_resHolder->settings->settingsMap();
  QVariantMap const appSettings = settings.value("app_settings").toMap();
  QVariantMap const temp        = tempParam(settings);
  QVariantMap const program     = settings.value("default_program").toMap();

  auto makeValue = [this](char const *name, double value, double min, double max, int prec) {
    auto *sysValue = new SystemValue(QString::fromLatin1(name), static_cast<float>(value), this);
    sysValue->setMin(static_cast<float>(min));
    sysValue->setMax(static_cast<float>(max));
    sysValue->setPrec(prec);
    return sysValue;
  };
  m_graphPeriod = makeValue("graph_period", appSettings.value("graphUpdatePeriod", DEFAULT_GRAPH_PERIOD_S).toDouble(), 1, 60, 0);
  m_pollPeriod  = makeValue("poll_period",
                           settings.value("communication").toMap().value("base_params_request_period_sec", DEFAULT_POLL_PERIOD_S).toDouble(),
                           1, 60, 0);
  int const prec{temp.value("prec", 1).toInt()};
  m_tempMin     = makeValue("temp_min", temp.value("min", -75).toDouble(), -200, 500, prec);
  m_tempMax     = makeValue("temp_max", temp.value("max", 150).toDouble(), -200, 500, prec);
  m_defaultTemp = makeValue("default_temp", program.value("temp", 20).toDouble(), -200, 500, prec);

  auto *pageLay = new QVBoxLayout(this);
  pageLay->setContentsMargins(0, 8 * ssf, 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  pageLay->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  cardLay->setSpacing(8 * ssf);
  auto *title = new QLabel(tr("Application"), card);
  title->setObjectName("cardTitle");
  cardLay->addWidget(title);

  auto *list    = new QWidget();
  auto *listLay = new QVBoxLayout(list);
  listLay->setContentsMargins(0, 0, 4 * ssf, 0);
  listLay->setSpacing(6 * ssf);
  QFont const fieldFont = font();

  auto section = [&](QString const &text) {
    auto *label = new QLabel(text.toUpper(), list);
    label->setProperty("muted", true);
    listLay->addSpacing(6 * ssf);
    listLay->addWidget(label);
  };
  // a row: what it is, when it applies, the field and its unit
  auto addRow = [&](QString const &label, QString const &note, QWidget *field, QString const &unit) {
    auto *row = new QFrame(list);
    row->setObjectName("outputRow");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(12 * ssf, 6 * ssf, 10 * ssf, 6 * ssf);
    rowLay->setSpacing(10 * ssf);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(1 * ssf);
    auto *labelWidget = new QLabel(label, row);
    labelWidget->setObjectName("rowTitle");
    auto *noteWidget = new QLabel(note, row);
    noteWidget->setProperty("muted", true);
    noteWidget->setWordWrap(true);
    texts->addWidget(labelWidget);
    texts->addWidget(noteWidget);
    rowLay->addLayout(texts, 1);
    field->setParent(row);
    rowLay->addWidget(field);
    auto *unitWidget = new QLabel(unit, row);
    unitWidget->setProperty("muted", true);
    unitWidget->setFixedWidth(40 * ssf);
    rowLay->addWidget(unitWidget);
    listLay->addWidget(row);
  };
  auto numField = [&](SystemValue *value) {
    auto *field = new LineEditNum(value);
    field->setFont(fieldFont);
    field->setFixedSize(130 * ssf, 30 * ssf);
    return field;
  };

  section(tr("Data"));
  addRow(tr("Chart refresh period"), tr("How often the live chart redraws. Applies immediately."), numField(m_graphPeriod), tr("s"));
  addRow(tr("PLC status request period"),
         tr("How often the PLC state list and settings are requested while connecting. Applies immediately."),
         numField(m_pollPeriod), tr("s"));

  section(tr("Setpoint limits"));
  addRow(tr("Lowest setpoint"), tr("Lowest temperature accepted in Manual control and program intervals. Applies on the next start."),
         numField(m_tempMin), QStringLiteral("°C"));
  addRow(tr("Highest setpoint"), tr("Highest temperature accepted in Manual control and program intervals. Applies on the next start."),
         numField(m_tempMax), QStringLiteral("°C"));

  section(tr("Program at start"));
  addRow(tr("Setpoint"), tr("Temperature of the interval Program control shows when the application starts. Applies on the next start."),
         numField(m_defaultTemp), QStringLiteral("°C"));
  m_defaultDuration = new LineEditTime(static_cast<quint64>(program.value("duration", 3600).toULongLong()), 1, 3599999, true);
  m_defaultDuration->setFont(fieldFont);
  m_defaultDuration->setFixedSize(130 * ssf, 30 * ssf);
  for (LineEditNum *part : m_defaultDuration->findChildren<LineEditNum *>()) {
    part->setFont(fieldFont);
    part->setFixedHeight(30 * ssf);
  }
  addRow(tr("Duration"), tr("Length of that interval. Applies on the next start."), m_defaultDuration, QString());
  listLay->addStretch(1);

  auto *scroll = new QScrollArea(card);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(list);
  scroll->viewport()->setAutoFillBackground(false);
  list->setAutoFillBackground(false);
  scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  cardLay->addWidget(scroll, 1);

  auto *buttons = new QHBoxLayout();
  buttons->addStretch();
  auto *saveBtn = new QPushButton(tr("Save"), card);
  saveBtn->setObjectName("primaryButton");
  saveBtn->setFixedSize(190 * ssf, 32 * ssf);
  buttons->addWidget(saveBtn);
  cardLay->addLayout(buttons);
  connect(saveBtn, &QPushButton::clicked, this, &ApplicationSettingsWidget::save);
}

void ApplicationSettingsWidget::save() {
  double const lo{m_tempMin->getValue()};
  double const hi{m_tempMax->getValue()};
  double const start{m_defaultTemp->getValue()};
  if (lo >= hi) {
    (new MessageDialog(tr("Warning"), tr("The lowest setpoint must be below the highest one."), this))->open();
    return;
  }
  if ((start < lo) || (start > hi)) {
    (new MessageDialog(tr("Warning"), tr("The setpoint of the program at start must lie between the setpoint limits."), this))->open();
    return;
  }

  QVariantMap const settings    = m_resHolder->settings->settingsMap();
  QVariantMap       appSettings = settings.value("app_settings").toMap();
  appSettings["graphUpdatePeriod"] = qRound(m_graphPeriod->getValue());
  QVariantMap communication                     = settings.value("communication").toMap();
  communication["base_params_request_period_sec"] = qRound(m_pollPeriod->getValue());
  QVariantList controlParams = settings.value("control_params").toList();
  for (QVariant &entry : controlParams) {
    QVariantMap param = entry.toMap();
    if (param.value("name").toString() == QLatin1String("temp")) {
      param["min"] = lo;
      param["max"] = hi;
      entry        = param;
    }
  }
  QVariantMap program = settings.value("default_program").toMap();
  program["temp"]     = start;
  program["duration"] = static_cast<qlonglong>(m_defaultDuration->getSeconds());

  bool const saved = m_resHolder->settings->setEntries({{"app_settings", appSettings},
                                                       {"communication", communication},
                                                       {"control_params", controlParams},
                                                       {"default_program", program}});
  (new MessageDialog(saved ? tr("Saved") : tr("Error"),
                     saved ? tr("The application settings are saved. Setpoint limits and the program at start apply on the next start.")
                           : tr("The settings file could not be written."),
                     this))
    ->open();
}

} // namespace deepvaccontrol
