#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "DigiKamLibrary.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    DigiKamLibrary photoLibrary;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("photoLibrary", &photoLibrary);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("fotoframe", "Main");

    return app.exec();
}
