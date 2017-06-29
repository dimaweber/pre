#include <QApplication>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDateTime>
#include <QTime>
#include <QVariant>
#include <QChart>
#include <QChartView>
#include <QCandlestickSeries>
#include <QCandlestickSet>
#include <QBarSeries>
#include <QBarSet>
#include <QBarCategoryAxis>
#include <QBoxLayout>
#include <iostream>
#include <iomanip>

typedef qreal Rate;

struct Period
{
    Rate open;
    Rate close;
    Rate min;
    Rate max;
    double vol;
    double cur_vol;
    QDateTime periodStart;
    QDateTime periodEnd;

    double body_size() const
    {
        return qAbs(open-close);
    }

    double upper_tail_size() const
    {
        return qAbs(max - qMax(open,close));
    }

    double lower_tail_size() const
    {
        return qAbs(min - qMin(open,close));
    }

    bool isRise() const
    {
        return close > open;
    }
};

using namespace QtCharts;


#define DAYS 3
#define granularity_hours 0
#define granularity_minutes 30

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // GET DATA
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "conn");
    db.setHostName("192.168.10.4");
    db.setDatabaseName("trade");
    db.setUserName("trader");
    db.setPassword("traderpassword");

    if (!db.open())
    {
        std::cerr << qPrintable(db.lastError().text()) << std::endl;
    }

    QSqlQuery query(db);
    QString sql  = "select time,last_rate,goods_volume from rates where currency='usd' and goods='btc' and time >= now() - interval :int day  order by time asc";
    if (!query.prepare(sql))
    {
        std::cerr << qPrintable(query.lastError().text()) << std::endl;
    }
    query.bindValue(":int", DAYS);

    if (!query.exec())
    {
        std::cerr << qPrintable(query.lastError().text()) << std::endl;
    }

    // GROUP DATA
    QDateTime periodStart = QDateTime::currentDateTime().addDays(-DAYS - 2);
    QTime granularity (granularity_hours, granularity_minutes, 0);
    int granularitySec = QTime(0,0,0).secsTo(granularity);

    Period* period = nullptr;
    QList<Period*> periods;

    double prevVolume = 0;

    while (query.next())
    {
        QDateTime time = query.value(0).toDateTime();
        Rate rate = query.value(1).toDouble();
        double volume = query.value(2).toDouble();

        if (period)
        {
            period->periodEnd = time;
            period->close = rate;
            period->max = qMax(period->max, rate);
            period->min = qMin(period->min, rate);
            period->vol += volume;

        }

        if (periodStart.secsTo(time) > granularitySec)
        {

            // new period
            period = new Period;
            periods.append(period);

            period->periodStart = time;
            period->periodEnd = time;
            period->open = rate;
            period->max = rate;
            period->min = rate;
            period->vol = 0;

            periodStart = time;
        }
        prevVolume = volume;
    }


    // DISPLAY
    QStringList categories;
    QCandlestickSeries series;
    QCandlestickSet* candle = nullptr;
    QBarSeries bars;
    QBarSet volume("Volume");

    for (Period* period: periods)
    {
        std::cout << qPrintable(period->periodStart.toString(Qt::ISODate)) << "     "
                  << qPrintable(period->periodEnd.toString(Qt::ISODate)) << "     "
                  << std::setw(8) << qPrintable(QString::number(period->upper_tail_size(), 'f', 3)) << "   "
                  << std::setw(8) << qPrintable(QString::number(period->body_size(), 'f', 3)) << "   "
                  << std::setw(8) << qPrintable(QString::number(period->lower_tail_size(), 'f', 3)) << "   "
                  << std::setw(8) << period->isRise()
                  << std::endl;

        candle = new QCandlestickSet(period->open, period->max, period->min, period->close, period->periodStart.toSecsSinceEpoch());
        categories << period->periodStart.toString("dd HH");
        series.append(candle);
        volume.append(period->vol);
    }

    series.setName("BTC / USD");
    series.setIncreasingColor(QColor(Qt::green));
    series.setDecreasingColor(QColor(Qt::red));

    bars.append(&volume);

    QWidget* widget = new QWidget;
    QChart chart;
    QChart chartV;
    chart.addSeries(&series);
    chartV.addSeries(&bars);
    chart.setTitle("BTC / USD");
    chart.setAnimationOptions(QChart::SeriesAnimations);
    chart.createDefaultAxes();
    chartV.setTitle("BTC / USD");
    chartV.setAnimationOptions(QChart::SeriesAnimations);
    chartV.createDefaultAxes();

    QChartView *chartView = new QChartView(widget);
    chartView->setRenderHint(QPainter::Antialiasing);
    QChartView *chartVView = new QChartView(widget);
    chartVView->setRenderHint(QPainter::Antialiasing);

    chartView->setChart(&chart);
    chartVView->setChart(&chartV);

    QBarCategoryAxis *axisX = qobject_cast<QBarCategoryAxis *>(chart.axes(Qt::Horizontal).at(0));
    QBarCategoryAxis *axisXV = qobject_cast<QBarCategoryAxis *>(chartV.axes(Qt::Horizontal).at(0));
    axisX->setCategories(categories);
    axisX->setLabelsAngle(60);
    axisXV->setCategories(categories);
    axisXV->setLabelsAngle(60);

    widget->setLayout(new QVBoxLayout);
    widget->layout()->addWidget(chartView);
    widget->layout()->addWidget(chartVView);

    widget->show();
    return a.exec();
}
