#include "../../src/nodeqml/engine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QQmlEngine>

int main(int argc, char *argv[])
{
    QScopedPointer<QCoreApplication> app(new QCoreApplication(argc, argv));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("NodeQml"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("script"), QStringLiteral("script to run"));
    parser.process(app->arguments());

    if (parser.positionalArguments().isEmpty())
        return 0;

    const QString script = parser.positionalArguments().first();

    QScopedPointer<QQmlEngine> engine(new QQmlEngine());
    QScopedPointer<NodeQml::Engine> node(new NodeQml::Engine(engine.data()));

    QJSValue object = node->require(script);
    if (object.isUndefined()) {
        return 1;
    }

    return app->exec();
}
