#include "analysisnotesstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <cmath>

namespace deepvaccontrol {

namespace {
QJsonValue jsonNumber(double value) {
  return std::isnan(value) ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

double numberOrNaN(QJsonValue const &value) {
  return value.isDouble() ? value.toDouble() : qQNaN();
}

QString newId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace

AnalysisNotesStore::AnalysisNotesStore(QString const &filePath, QObject *parent)
    : QObject{parent}, m_filePath{filePath} {
  load();
}

QVector<AnalysisNotesStore::Annotation> AnalysisNotesStore::annotations(double start, double end) const {
  QVector<Annotation> result;
  for (Annotation const &annotation : m_annotations) {
    if ((annotation.end >= start) && (annotation.start <= end)) {
      result.push_back(annotation);
    }
  }
  return result;
}

void AnalysisNotesStore::addAnnotation(Annotation annotation) {
  annotation.id = newId();
  m_annotations.push_back(annotation);
  save();
  emit annotationsChanged();
}

void AnalysisNotesStore::removeAnnotation(QString const &id) {
  auto const removed = m_annotations.removeIf([&id](Annotation const &annotation) {
    return annotation.id == id;
  });
  if (removed > 0) {
    save();
    emit annotationsChanged();
  }
}

QVector<AnalysisNotesStore::Rule> AnalysisNotesStore::rules(QString const &runKey) const {
  return m_rules.value(runKey);
}

void AnalysisNotesStore::addRule(QString const &runKey, Rule rule) {
  rule.id = newId();
  m_rules[runKey].push_back(rule);
  save();
  emit rulesChanged(runKey);
}

void AnalysisNotesStore::removeRule(QString const &runKey, QString const &id) {
  auto it = m_rules.find(runKey);
  if (it == m_rules.end()) {
    return;
  }
  auto const removed = it->removeIf([&id](Rule const &rule) {
    return rule.id == id;
  });
  if (removed > 0) {
    save();
    emit rulesChanged(runKey);
  }
}

QStringList AnalysisNotesStore::channels(QString const &runKey) const {
  return m_channels.value(runKey);
}

void AnalysisNotesStore::setChannels(QString const &runKey, QStringList const &channels) {
  if (m_channels.value(runKey) == channels) {
    return;
  }
  m_channels[runKey] = channels;
  save();
}

void AnalysisNotesStore::load() {
  QFile file(m_filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }
  QJsonObject const root = QJsonDocument::fromJson(file.readAll()).object();

  for (QJsonValue const &value : root["annotations"].toArray()) {
    QJsonObject const object = value.toObject();
    m_annotations.push_back({object["id"].toString(),
                             object["start"].toDouble(),
                             object["end"].toDouble(),
                             object["label"].toString(),
                             QColor(object["color"].toString()),
                             object["user"].toString()});
  }

  QJsonObject const rules = root["rules"].toObject();
  for (auto it = rules.begin(); it != rules.end(); ++it) {
    for (QJsonValue const &value : it.value().toArray()) {
      QJsonObject const object = value.toObject();
      m_rules[it.key()].push_back({object["id"].toString(),
                                   object["channel"].toString(),
                                   numberOrNaN(object["lo"]),
                                   numberOrNaN(object["hi"]),
                                   object["label"].toString(),
                                   QColor(object["color"].toString()),
                                   object["user"].toString()});
    }
  }

  QJsonObject const channels = root["channels"].toObject();
  for (auto it = channels.begin(); it != channels.end(); ++it) {
    m_channels.insert(it.key(), it.value().toVariant().toStringList());
  }
}

void AnalysisNotesStore::save() const {
  QJsonArray annotations;
  for (Annotation const &annotation : m_annotations) {
    annotations.push_back(QJsonObject{{"id", annotation.id},
                                      {"start", annotation.start},
                                      {"end", annotation.end},
                                      {"label", annotation.label},
                                      {"color", annotation.color.name()},
                                      {"user", annotation.user}});
  }

  QJsonObject rules;
  for (auto it = m_rules.cbegin(); it != m_rules.cend(); ++it) {
    if (it->isEmpty()) {
      continue;
    }
    QJsonArray list;
    for (Rule const &rule : it.value()) {
      list.push_back(QJsonObject{{"id", rule.id},
                                 {"channel", rule.channel},
                                 {"lo", jsonNumber(rule.lo)},
                                 {"hi", jsonNumber(rule.hi)},
                                 {"label", rule.label},
                                 {"color", rule.color.name()},
                                 {"user", rule.user}});
    }
    rules.insert(it.key(), list);
  }

  QJsonObject channels;
  for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) {
    channels.insert(it.key(), QJsonArray::fromStringList(it.value()));
  }

  QDir().mkpath(QFileInfo(m_filePath).absolutePath());
  QSaveFile file(m_filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }
  QJsonObject const root{{"annotations", annotations}, {"rules", rules}, {"channels", channels}};
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

} // namespace deepvaccontrol
