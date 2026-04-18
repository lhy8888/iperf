#include "MainWindow.h"

#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("IperfWin");

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 24, 24, 24);

    auto *title = new QLabel("IperfWin", central);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title, 1);

    setCentralWidget(central);
    statusBar()->showMessage("Ready");
    resize(1200, 800);
}
