#include <QApplication>
#include <QMetaType>

#include "MainWindow.h"
#include "IperfGuiTypes.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("IperfWin"));
    QApplication::setOrganizationName(QStringLiteral("iperf"));

    qRegisterMetaType<IperfGuiConfig>("IperfGuiConfig");
    qRegisterMetaType<IperfGuiEvent>("IperfGuiEvent");
    qRegisterMetaType<IperfSessionRecord>("IperfSessionRecord");
    qRegisterMetaType<IperfEventKind>("IperfEventKind");

    MainWindow window;
    window.show();

    return app.exec();
}
