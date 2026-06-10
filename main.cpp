
#include <QApplication>
#include <QLocale>
#include <QStyleFactory>
#include <QTranslator>

#include "app.h"
#include "uilanguage.h"
#include "utils.h"

static void setupLanguage(QApplication &app, control2::UiLanguage lang, QTranslator &translator) {
  switch (lang) {
    case control2::UiLanguage::Russian: {
      qDebug() << "Set UiLanguage::Russian";
      // if (translator.load(":/language_ru.qm")) {
      //   app.installTranslator(&translator);
      // }
      // Set global locale for entire app
      QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
      break;
    }
    case control2::UiLanguage::English:
    default: {
      qDebug() << "Set UiLanguage::English";
      if (translator.load(":/language_en.qm")) {
        app.installTranslator(&translator);
      }
      QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
      break;
    }
  }
}

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);

  // Options parser
  QCommandLineParser parser;
  parser.setApplicationDescription("TestaApp2");
  parser.addHelpOption();

  QCommandLineOption windowModeOption("w", "Enable windowed mode");
  QCommandLineOption debugOption("debug", "Enable debug output");

  parser.addOption(windowModeOption);
  parser.addOption(debugOption);
  parser.process(a);

  bool       windowedMode{parser.isSet(windowModeOption)};
  bool const debugMode{parser.isSet(debugOption)};

#ifndef NDEBUG // debug
  // windowedMode = true;
#endif

  // Style and sizes
  a.setStyle(QStyleFactory::create("fusion"));
  a.setPalette(control2::utils::lightPalette());

  if (!windowedMode) {
    auto        *screen = a.primaryScreen();
    double const ssf{qMax(1.0, static_cast<double>(screen->size().width()) / 1024.0)};

    control2::utils::setSsf(ssf);

    auto font     = a.font();
    auto fontSize = font.pointSize();
    fontSize *= ssf;
    font.setPointSize(fontSize);
    a.setFont(font);
  }

  // Control application
  control2::App m_app(windowedMode, debugMode);

  // Translator must live until the end of the application
  QTranslator translator;
  setupLanguage(a, m_app.uiLanguage(), translator);

  m_app.runtimeSetups();
  m_app.start();
  return a.exec();
}
