#include "Qt_File_Redaction.h"
#include <QtWidgets/QApplication>
int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
    Qt_File_Redaction window;
    window.show();
    return app.exec();
}