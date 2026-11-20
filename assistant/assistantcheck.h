#ifndef ASSISTANTCHECK_H
#define ASSISTANTCHECK_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

namespace deepvaccontrol {
class AnalysisServices;
}

namespace deepvaccontrol::assistant {

class AssistantSession;

/**
 * @brief --assistant-check <file>: asks the assistant a fixed list of questions, as the Assistant panel does (the
 * local model, the tools, the checks of the answer), and writes each answer into the file with how long it took and
 * how long it is (characters, sentences, list items). For checking that the answers stay short and that every kind of
 * question is answered; nothing of it is shown in the window.
 */
class AssistantCheck : public QObject {
  Q_OBJECT
public:
  AssistantCheck(AssistantSession *session, AnalysisServices *analysis, QString path, QObject *parent = nullptr);
  void start();

signals:
  void finished();

private:
  struct Case {
    QString question;
    QString intent;         // a quick button of the panel: explain, similar, improve, report
    bool    shown{false};   // asked with an experiment shown on the chart
    bool    newChat{false}; // a new conversation first
  };

  void begin(); // once the experiment list is read
  void next();
  void onExchange(QString const &status, QString const &answer, bool done);
  void closeCase(QString const &answer, bool timedOut);
  void write() const;

  AssistantSession *m_session;
  AnalysisServices *m_analysis;
  QString           m_path;
  QVector<Case>     m_cases;
  QJsonObject       m_view; // the latest experiment as if shown whole on the chart
  int               m_index{-1};
  bool              m_waiting{false};
  QStringList       m_steps;    // what the panel showed meanwhile
  QString           m_partial;  // the answer as far as it was written
  QElapsedTimer     m_clock;
  QTimer            m_timeout;
  QStringList       m_report;
  int               m_longest{0};
  int               m_tooLong{0};
  int               m_warned{0};
  int               m_failed{0};
};

}

#endif // ASSISTANTCHECK_H
