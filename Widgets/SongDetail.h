#ifndef SONGDETAIL_H
#define SONGDETAIL_H

#include <QWidget>

#include "Song.h"

namespace Ui {
class SongDetail;
}

class SongDetail : public QWidget
{
    Q_OBJECT

public:
    explicit SongDetail(QWidget *parent = 0);
    ~SongDetail();

public slots:
    void setDetail(Song *song);

private:
    Ui::SongDetail *ui;
};

#endif // SONGDETAIL_H
