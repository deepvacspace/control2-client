
#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>
#include <QTimer>
#include <QStyleFactory>
#include <QTranslator>

#include "app.h"
#include "mainwindow.h"
#include "startupsplash.h"
#include "uilanguage.h"
#include "theme.h"
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
#include <openssl/crypto.h>
#endif
#include "utils.h"

static void setupLanguage(QApplication &app, deepvaccontrol::UiLanguage lang, QTranslator &translator) {
  switch (lang) {
    case deepvaccontrol::UiLanguage::Russian: {
      qDebug() << "Set UiLanguage::Russian";
      // the texts in the code are English; res/language_ru.ts holds their Russian
      if (translator.load(":/language_ru.qm")) {
        app.installTranslator(&translator);
      }
      // Set global locale for entire app
      QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
      break;
    }
    case deepvaccontrol::UiLanguage::German: {
      qDebug() << "Set UiLanguage::German";
      // res/language_de.ts holds the German of the texts in the code
      if (translator.load(":/language_de.qm")) {
        app.installTranslator(&translator);
      }
      QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
      break;
    }
    case deepvaccontrol::UiLanguage::English:
    default: {
      qDebug() << "Set UiLanguage::English";
      QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); // the texts in the code
      break;
    }
  }
}

int main(int argc, char *argv[]) {
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
  // OpenSSL's own atexit clean-up races with the unloading of Qt's OpenSSL TLS plugin and crashed the
  // process on exit now and then; the operating system frees everything at exit anyway
  OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr);
#endif
  QApplication a(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Deepvac Control"));
#ifdef DEEPVACCONTROL_VERSION
  QApplication::setApplicationVersion(QStringLiteral(DEEPVACCONTROL_VERSION));
#endif
  // Keep the display name empty so the platform plugin does not append
  // " - Deepvac Control" to the title that MainWindow already provides.
  QApplication::setApplicationDisplayName(QString());
  QApplication::setOrganizationName(QStringLiteral("Deepvac"));

  // Options parser
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Deepvac Control"));
  parser.addHelpOption();

  QCommandLineOption windowModeOption("w", "Enable windowed mode");
  QCommandLineOption debugOption("debug", "Enable debug output");

  QCommandLineOption exportOption("export-ui", "Export every screen as PNG and SVG into <dir>, then quit", "dir");

  parser.addOption(windowModeOption);
  parser.addOption(debugOption);
  QCommandLineOption demoOption("demo-shot",
                                "Show an example run in the Live tab, save the window into <file> (PNG, twice the window's "
                                "resolution), then quit. Run it from a scratch working directory with a copy of settings.json",
                                "file");

  QCommandLineOption assistantOption("assistant-check",
                                     "Ask the assistant a fixed list of questions and write its answers, with how long each took "
                                     "and how long it is, into <file>, then quit. Run it from a scratch working directory",
                                     "file");

  parser.addOption(exportOption);
  parser.addOption(demoOption);
  parser.addOption(assistantOption);
  parser.process(a);

  bool       windowedMode{parser.isSet(windowModeOption)};
  bool const debugMode{parser.isSet(debugOption)};

#ifndef NDEBUG // debug
  // windowedMode = true;
#endif

  // Style and sizes
  a.setStyle(QStyleFactory::create("fusion"));

  if (!windowedMode) {
    auto        *screen = a.primaryScreen();
    double const ssf{qMax(1.0, static_cast<double>(screen->size().width()) / 1024.0)};

    deepvaccontrol::utils::setSsf(ssf);

    auto font     = a.font();
    auto fontSize = font.pointSize();
    fontSize *= ssf;
    font.setPointSize(fontSize);
    a.setFont(font);
  }
  // after the scale and the font: the style sheet's sizes and fonts follow them
  deepvaccontrol::theme::apply(false); // light design by default (System -> OS switches to dark)

  // while it starts (settings, databases, the pages): the logo and what is being done, not a half-drawn window
  auto *splash = new deepvaccontrol::StartupSplash();
  splash->show();
  splash->setMessage(QStringLiteral("Starting…"));
  splash->setProgress(0.06);

  // Control application
  deepvaccontrol::App m_app(windowedMode, debugMode);

  // Translator must live until the end of the application
  QTranslator translator;
  setupLanguage(a, m_app.uiLanguage(), translator);

  splash->setMessage(QCoreApplication::translate("StartupSplash", "Loading the application…"));
  splash->setProgress(0.22);
  m_app.runtimeSetups(); // the pages move the bar on as they are built (StartupSplash::advance)
  splash->setProgress(0.88);
  bool const demo{parser.isSet(demoOption)};
  if (!demo) {
    m_app.start();
  }
  splash->finishWhenShown(m_app.showWindow());
  if (demo) {
    QTimer::singleShot(4000, &a, [&m_app, file = parser.value(demoOption)] { m_app.runDemoShot(file); });
  }

  // --export-ui <dir>: once connected and drawn, every screen into dir (PNG and SVG), then quit
  if (parser.isSet(exportOption)) {
    QString const dir = parser.value(exportOption);
    QTimer::singleShot(10'000, &a, [dir] {
      for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<deepvaccontrol::MainWindow *>(widget)) {
          QObject::connect(window, &deepvaccontrol::MainWindow::screensExported, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
          window->exportScreens(dir);
          return;
        }
      }
    });
  }
  // --assistant-check <file>: once started, the assistant's answers to a list of questions into file, then quit
  if (parser.isSet(assistantOption)) {
    QString const file = parser.value(assistantOption);
    QTimer::singleShot(5'000, &a, [file] {
      for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<deepvaccontrol::MainWindow *>(widget)) {
          QObject::connect(window, &deepvaccontrol::MainWindow::screensExported, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
          window->runAssistantCheck(file);
          return;
        }
      }
    });
  }
  return a.exec();
}
