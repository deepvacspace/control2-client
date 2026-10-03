#include "experimentreport.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QSaveFile>
#include <QTextDocument>
#include <QUrl>
#include <cmath>

#include "actionmanager.h"
#include "experimentinput.h"

namespace deepvaccontrol::experimentreport {

namespace {
using namespace analysistext;

constexpr int MAX_EVENTS{300};

QString esc(QString const &text) {
  return text.toHtmlEscaped();
}

QString dateTime(double epoch) {
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epoch)).toString("yyyy-MM-dd HH:mm:ss");
}

// table with header row; cells are already HTML
QString table(QStringList const &headers, QVector<QStringList> const &rows) {
  QString html = QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\" width=\"100%\"><tr>");
  for (QString const &header : headers) {
    html += QStringLiteral("<th bgcolor=\"#e2e8f0\" align=\"left\">%1</th>").arg(esc(header));
  }
  html += QStringLiteral("</tr>");
  for (QStringList const &row : rows) {
    html += QStringLiteral("<tr>");
    for (QString const &value : row) {
      html += QStringLiteral("<td>%1</td>").arg(value.isEmpty() ? QStringLiteral("&nbsp;") : value);
    }
    html += QStringLiteral("</tr>");
  }
  return html + QStringLiteral("</table>");
}

// two-column key / value table
QString facts(QVector<QPair<QString, QString>> const &items) {
  QVector<QStringList> rows;
  for (auto const &[key, value] : items) {
    rows.push_back({QStringLiteral("<b>%1</b>").arg(esc(key)), esc(value)});
  }
  QString html = QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">");
  for (QStringList const &row : rows) {
    html += QStringLiteral("<tr><td bgcolor=\"#f1f5f9\">%1</td><td>%2</td></tr>").arg(row[0], row[1]);
  }
  return html + QStringLiteral("</table>");
}

QString colored(QString const &text, QColor const &color) {
  return QStringLiteral("<font color=\"%1\">%2</font>").arg(color.name(), esc(text));
}

QString imageSource(QImage const &image, int index, bool embed) {
  if (!embed) {
    return QStringLiteral("chart%1.png").arg(index);
  }
  QByteArray bytes;
  QBuffer    buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
}

QString eventType(int type) {
  switch (static_cast<ActionManager::ActionType>(type)) {
    case ActionManager::ActionType::PLC:
      return QObject::tr("PLC");
    case ActionManager::ActionType::ERR:
      return QObject::tr("Error");
    default:
      return QObject::tr("Application");
  }
}
} // namespace

QString html(ExperimentReportData const &data, bool embedImages) {
  core::ExperimentAnalysis const *a = data.analysis;
  QString                         body;
  auto section = [&body](QString const &title) { body += QStringLiteral("<h2>%1</h2>").arg(esc(title)); };

  // ---- header and summary
  body += QStringLiteral("<h1>%1</h1>").arg(esc(QObject::tr("Experiment report: %1").arg(data.run.title)));
  body += QStringLiteral("<p><font color=\"#64748b\">%1</font></p>")
            .arg(esc(QObject::tr("Generated %1%2 by Deepvac Control")
                       .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"),
                            data.author.isEmpty() ? QString() : QObject::tr(" for %1").arg(data.author))));
  section(QObject::tr("Summary"));
  QVector<QPair<QString, QString>> summary{
    {QObject::tr("Start"), dateTime(data.run.start)},
    {QObject::tr("End"), dateTime(data.run.end)},
    {QObject::tr("Duration"), seconds(data.run.duration())},
    {QObject::tr("Samples"), QString::number(data.run.samples)},
  };
  if (!data.run.chamber.isEmpty()) {
    summary.push_back({QObject::tr("Chamber"), data.run.chamber});
  }
  if (a) {
    summary.push_back({QObject::tr("Anomalies"), QObject::tr("%1 critical, %2 warnings, %3 info")
                                                   .arg(a->anomalyCount(core::Anomaly::Severity::CRITICAL))
                                                   .arg(a->anomalyCount(core::Anomaly::Severity::WARNING) - a->anomalyCount(core::Anomaly::Severity::CRITICAL))
                                                   .arg(a->anomalies.size() - a->anomalyCount(core::Anomaly::Severity::WARNING))});
  }
  body += facts(summary);

  if (a && a->hasControl) {
    core::ControlKpis const &k = a->run.overall;
    section(QObject::tr("Control KPIs (temp vs temp_ref)"));
    body += facts({{QObject::tr("MAE"), number(k.mae)},
                   {QObject::tr("RMSE"), number(k.rmse)},
                   {QObject::tr("Max |error|"), number(k.maxAbsError)},
                   {QObject::tr("Overshoot (worst step)"), number(a->run.overshoot)},
                   {QObject::tr("Undershoot (worst step)"), number(a->run.undershoot)},
                   {QObject::tr("Settling of the last setpoint"), seconds(a->run.settlingTimeS)},
                   {QObject::tr("Steady-state error"), number(k.steadyStateError)},
                   {QObject::tr("Tail MAE / std"), QStringLiteral("%1 / %2").arg(number(k.tailMae), number(k.tailStd))},
                   {QObject::tr("Control cost"), number(a->run.cost)},
                   {QObject::tr("Output saturated"), percent(a->saturation.fraction)},
                   {QObject::tr("Integrator windup"), seconds(a->saturation.windupS)},
                   {QObject::tr("P / I / D share"), pidShares(a->pid)}});
  } else {
    body += QStringLiteral("<p>%1</p>").arg(esc(QObject::tr("No temp / temp_ref recorded: no control KPIs or stages.")));
  }

  // ---- configuration
  if (a) {
    core::RunConfig const &c = a->config;
    QStringList            setpoints;
    for (double const setpoint : c.setpoints) {
      setpoints << number(setpoint, 2);
    }
    section(QObject::tr("Configuration"));
    body += facts({{QObject::tr("Sample period"), QObject::tr("%1 s").arg(number(c.samplePeriodS, 3))},
                   {QObject::tr("PID gains (most used)"), QStringLiteral("Kp %1 · Ki %2 · Kd %3").arg(number(c.kp), number(c.ki), number(c.kd))},
                   {QObject::tr("PID gain sets used"), QString::number(c.gainSets)},
                   {QObject::tr("Setpoints"), setpoints.isEmpty() ? QStringLiteral("—") : setpoints.join(QStringLiteral(" → "))},
                   {QObject::tr("Reference range"), QStringLiteral("%1 … %2").arg(number(c.minReference, 2), number(c.maxReference, 2))},
                   {QObject::tr("Max ramp rate"), QObject::tr("%1 /min").arg(number(c.maxRampRatePerMin, 3))},
                   {QObject::tr("Stages (step / ramp / hold)"),
                    QStringLiteral("%1 (%2 / %3 / %4)").arg(c.stages).arg(c.steps).arg(c.ramps).arg(c.holds)},
                   {QObject::tr("Program intervals / cycles"),
                    QStringLiteral("%1 / %2").arg(c.programIntervals > 0 ? QString::number(c.programIntervals) : QStringLiteral("—"),
                                                  c.cycles > 0 ? QString::number(c.cycles) : QStringLiteral("—"))},
                   {QObject::tr("Recorded channels"), c.channels.join(QStringLiteral(", "))}});
  }

  // ---- charts
  if (!data.charts.isEmpty()) {
    section(QObject::tr("Charts"));
    for (int i{0}; i < data.charts.size(); ++i) {
      body += QStringLiteral("<p><img src=\"%1\" width=\"680\"><br><font color=\"#64748b\">%2</font></p>")
                .arg(imageSource(data.charts[i].second, i, embedImages), esc(data.charts[i].first));
    }
  }

  // ---- stages: control KPIs, then behaviour (steady state, ramp tracking, saturation, P/I/D, gains)
  if (a && !a->run.stages.isEmpty()) {
    section(QObject::tr("Stages: control KPIs"));
    QVector<QStringList> kpiRows;
    QVector<QStringList> behaviourRows;
    for (int r{0}; r < a->run.stages.size(); ++r) {
      core::StageAnalysis const &stage = a->run.stages[r];
      core::StageDetail const   &d     = a->stageDetails.value(r);
      core::Segment const       &s     = stage.segment;
      bool const                 step{s.kind == core::Segment::Kind::SETPOINT};
      bool const                 ramp{s.kind == core::Segment::Kind::RAMP};
      QString const              setpoint = (std::fabs(s.referenceEnd - s.referenceStart) < 0.05)
                                              ? number(s.referenceEnd, 2)
                                              : QStringLiteral("%1 → %2").arg(number(s.referenceStart, 2), number(s.referenceEnd, 2));
      double const transition{std::isfinite(stage.kpis.riseTimeS) ? stage.kpis.riseTimeS : stage.kpis.fallTimeS};
      QString const name = colored(stageName(s.kind), stageColor(s.kind));
      kpiRows.push_back({QString::number(r + 1),
                         name,
                         s.interval >= 0 ? QString::number(s.interval) : QStringLiteral("—"),
                         seconds(s.start),
                         seconds(s.duration()),
                         esc(setpoint),
                         number(stage.kpis.mae, 3),
                         step ? number(stage.kpis.overshoot, 3) : QStringLiteral("—"),
                         step ? seconds(stage.kpis.settlingTimeS) : QStringLiteral("—"),
                         step ? seconds(transition) : QStringLiteral("—"),
                         step ? number(stage.kpis.steadyStateError, 3) : QStringLiteral("—")});
      auto gain = [](double value) { return std::isfinite(value) ? QString::number(value, 'g', 4) : QStringLiteral("—"); };
      behaviourRows.push_back({QString::number(r + 1),
                               name,
                               seconds(d.timeToSteadyS),
                               percent(d.steadyFraction),
                               ramp ? QStringLiteral("%1 / %2").arg(number(d.referenceRatePerMin, 3), number(d.valueRatePerMin, 3))
                                    : QStringLiteral("—"),
                               ramp ? seconds(d.trackingLagS) : QStringLiteral("—"),
                               percent(d.saturation.fraction),
                               seconds(d.saturation.windupS),
                               esc(pidShares(d.pid)),
                               QStringLiteral("%1 / %2 / %3").arg(gain(d.kp), gain(d.ki), gain(d.kd))});
    }
    body += table({QObject::tr("#"), QObject::tr("Stage"), QObject::tr("Interval"), QObject::tr("Start"), QObject::tr("Duration"),
                   QObject::tr("Setpoint"), QObject::tr("MAE"), QObject::tr("Overshoot"), QObject::tr("Settling"),
                   QObject::tr("Rise / fall"), QObject::tr("Steady-state error")},
                  kpiRows);
    section(QObject::tr("Stages: behaviour"));
    body += table({QObject::tr("#"), QObject::tr("Stage"), QObject::tr("Steady after"), QObject::tr("Time steady"),
                   QObject::tr("Rate ref / value, /min"), QObject::tr("Tracking lag"), QObject::tr("Output saturated"),
                   QObject::tr("Windup"), QObject::tr("P / I / D share"), QObject::tr("Kp / Ki / Kd")},
                  behaviourRows);
  }

  // ---- steady states
  if (a && !a->steady.isEmpty()) {
    section(QObject::tr("Steady states"));
    QVector<QStringList> rows;
    for (core::SteadyInterval const &s : a->steady) {
      rows.push_back({seconds(s.start), seconds(s.duration()), number(s.mean, 3), number(s.std, 4), number(s.meanError, 3)});
    }
    body += table({QObject::tr("Start"), QObject::tr("Duration"), QObject::tr("Mean"), QObject::tr("Std"), QObject::tr("Mean error")}, rows);
  }

  // ---- anomalies
  if (a) {
    section(QObject::tr("Anomalies"));
    if (a->anomalies.isEmpty()) {
      body += QStringLiteral("<p>%1</p>").arg(esc(QObject::tr("None detected.")));
    } else {
      QVector<QStringList> rows;
      for (core::Anomaly const &x : a->anomalies) {
        rows.push_back({colored(severityName(x.severity), severityColor(x.severity)), esc(anomalyName(x.kind)), esc(x.channel),
                        seconds(x.start), seconds(x.end - x.start), esc(x.detail)});
      }
      body += table({QObject::tr("Severity"), QObject::tr("Type"), QObject::tr("Channel"), QObject::tr("Start"), QObject::tr("Duration"),
                     QObject::tr("Detail")},
                    rows);
    }
  }

  // ---- events, annotations, rules
  if (!data.events.isEmpty()) {
    section(QObject::tr("Events"));
    QVector<QStringList> rows;
    for (int i{0}; (i < data.events.size()) && (i < MAX_EVENTS); ++i) {
      ExperimentEvent const &event = data.events[i];
      rows.push_back({seconds(event.time - data.run.start), dateTime(event.time), esc(eventType(event.type)), esc(event.msg)});
    }
    body += table({QObject::tr("Elapsed"), QObject::tr("Time"), QObject::tr("Source"), QObject::tr("Message")}, rows);
    if (data.events.size() > MAX_EVENTS) {
      body += QStringLiteral("<p>%1</p>").arg(esc(QObject::tr("… and %1 more").arg(data.events.size() - MAX_EVENTS)));
    }
  }
  if (!data.annotations.isEmpty() || !data.rules.isEmpty()) {
    section(QObject::tr("Notes"));
    QVector<QStringList> rows;
    for (AnalysisNotesStore::Annotation const &note : data.annotations) {
      rows.push_back({QObject::tr("Annotation"), esc(note.label),
                      QStringLiteral("%1 – %2").arg(seconds(note.start - data.run.start), seconds(note.end - data.run.start)), esc(note.user)});
    }
    for (AnalysisNotesStore::Rule const &rule : data.rules) {
      rows.push_back({QObject::tr("Rule"), esc(rule.label),
                      QStringLiteral("%1 in [%2, %3]").arg(esc(rule.channel), std::isnan(rule.lo) ? QStringLiteral("−∞") : number(rule.lo),
                                                         std::isnan(rule.hi) ? QStringLiteral("+∞") : number(rule.hi)),
                      esc(rule.user)});
    }
    body += table({QObject::tr("Kind"), QObject::tr("Label"), QObject::tr("Where"), QObject::tr("User")}, rows);
  }

  // ---- comparison
  if (!data.comparisonHtml.isEmpty()) {
    section(QObject::tr("Comparison"));
    body += data.comparisonHtml;
  }

  return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title>"
                        "<style>body{font-family:'Segoe UI',Arial,sans-serif;font-size:10pt;color:#0f172a;max-width:1100px;margin:16px;}"
                        "h1{font-size:16pt;}h2{font-size:13pt;margin-top:18px;border-bottom:1px solid #cbd5e1;}h3{font-size:11pt;}"
                        "table{border-collapse:collapse;margin:4px 0;}td,th{border:1px solid #cbd5e1;padding:3px 6px;font-size:9pt;}"
                        "th{background:#e2e8f0;text-align:left;}</style></head><body>%2</body></html>")
    .arg(esc(data.run.title), body);
}

bool writeHtml(QString const &path, ExperimentReportData const &data) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(html(data, true).toUtf8());
  return file.commit();
}

bool writePdf(QString const &path, ExperimentReportData const &data) {
  QTextDocument document;
  for (int i{0}; i < data.charts.size(); ++i) {
    document.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("chart%1.png").arg(i)), data.charts[i].second);
  }
  document.setHtml(html(data, false));

  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  printer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Landscape, QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter));
  printer.setDocName(data.run.title);
  document.print(&printer);
  return QFile::exists(path) && (QFile(path).size() > 0);
}

} // namespace deepvaccontrol::experimentreport
